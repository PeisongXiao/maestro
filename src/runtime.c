#include "maestro_int.h"

static void vm_log_error(maestro_ctx *ctx, const char *fmt, ...) {
	char buf[512];
	va_list ap;
	size_t off = 0;
	int n;

	if (!ctx || !ctx->vm_logger)
		return;

	memcpy(buf, "ERROR: ", 7);
	off = 7;
	va_start(ap, fmt);
	n = vsnprintf(buf + off, sizeof(buf) - off, fmt, ap);
	va_end(ap);

	if (n < 0)
		return;

	if ((size_t)n + off >= sizeof(buf)) {
		buf[sizeof(buf) - 2] = '\n';
		buf[sizeof(buf) - 1] = '\0';
	} else if (!n || buf[off + (size_t)n - 1] != '\n') {
		buf[off + (size_t)n] = '\n';
		buf[off + (size_t)n + 1] = '\0';
	}

	ctx->vm_logger(ctx, buf);
}

static void vm_log_builtin_error(struct run_ctx *rctx, const char *op,
				 const char *msg) {
	if (!rctx)
		return;

	vm_log_error(rctx->ctx, "builtin %s: %s", op ? op : "<unknown>",
		     msg ? msg : "invalid use");
}

static const char *img_str(maestro_ctx *ctx, uint32_t off) {
	return ctx->img_strs + off;
}

static const char *img_ident_name(maestro_ctx *ctx, uint32_t id) {
        const struct img_ident *idents = (const struct img_ident *)ctx->img_idents;

        if (id >= ctx->img_ident_nr)
                return "";

        return img_str(ctx, idents[id].name_off);
}

static uint32_t find_ident_id_by_name(maestro_ctx *ctx, const char *name) {
        uint32_t i;

        for (i = 0; i < ctx->img_ident_nr; i++) {
                if (!strcmp(img_ident_name(ctx, i), name))
                        return i;
        }

        return UINT32_MAX;
}

static const char *node_ident_name(maestro_ctx *ctx, struct img_node *node) {
        return img_ident_name(ctx, node->str_off);
}

static const char *node_text(maestro_ctx *ctx, struct img_node *node) {
        if (node->type == IMG_NODE_STRING)
                return img_str(ctx, node->str_off);

        return img_ident_name(ctx, node->str_off);
}

static struct img_node *img_node(maestro_ctx *ctx, uint32_t idx) {
        return &((struct img_node *)ctx->img_nodes)[idx];
}

static struct img_mod *img_mod_by_idx(maestro_ctx *ctx, uint32_t idx) {
        if (idx >= ctx->img_mod_nr)
                return NULL;

        return &((struct img_mod *)ctx->img_mods)[idx];
}

static const struct img_path *img_path_by_idx(maestro_ctx *ctx, uint32_t idx) {
        if (idx >= ctx->img_path_nr)
                return NULL;

        return &((const struct img_path *)ctx->img_paths)[idx];
}

static int mod_path_eq(maestro_ctx *ctx, struct img_mod *mod, const char **seg,
                       size_t nr) {
        size_t i;

        if (!mod || mod->path_nr != nr)
                return 0;

        for (i = 0; i < nr; i++) {
                const struct img_path *path = img_path_by_idx(ctx, mod->path_first + i);

                if (!path || strcmp(img_ident_name(ctx, path->ident_id), seg[i]))
                        return 0;
        }

        return 1;
}

static int split_path_text(const char *text, char ***seg_out, size_t *nr_out) {
        char *copy;
        char *tok;
        char *save;
        char **segv = NULL;
        size_t nr = 0;
        size_t cap = 0;

        *seg_out = NULL;
        *nr_out = 0;

        if (!text)
                return -1;

        copy = xstrdup(text);

        if (!copy)
                return -1;

        for (tok = strtok_r(copy, " \t\r\n", &save); tok;
             tok = strtok_r(NULL, " \t\r\n", &save)) {
                if (nr == cap) {
                        size_t ncap = cap ? cap * 2 : 8;
                        char **nseg = realloc(segv, ncap * sizeof(*nseg));

                        if (!nseg) {
                                free(copy);

                                while (nr)
                                        free(segv[--nr]);

                                free(segv);
                                return -1;
                        }

                        segv = nseg;
                        cap = ncap;
                }

                segv[nr] = xstrdup(tok);

                if (!segv[nr]) {
                        free(copy);

                        while (nr)
                                free(segv[--nr]);

                        free(segv);
                        return -1;
                }

                nr++;
        }

        free(copy);
        *seg_out = segv;
        *nr_out = nr;
        return nr ? 0 : -1;
}

static void free_path_text(char **segv, size_t nr) {
        while (nr)
                free(segv[--nr]);

        free(segv);
}

static struct img_mod *find_mod_by_seg(maestro_ctx *ctx, const char **seg,
                                       size_t nr) {
        uint32_t i;
        struct img_mod *mods = (struct img_mod *)ctx->img_mods;

        for (i = 0; i < ctx->img_mod_nr; i++) {
                if (mod_path_eq(ctx, &mods[i], seg, nr))
                        return &mods[i];
        }

        return NULL;
}

static struct img_mod *find_mod(maestro_ctx *ctx, const char *path) {
        struct img_mod *mod;
        char **segv = NULL;
        size_t nr = 0;

        if (split_path_text(path, &segv, &nr))
                return NULL;

        mod = find_mod_by_seg(ctx, (const char **)segv, nr);
        free_path_text(segv, nr);
        return mod;
}

static uint32_t find_mod_idx(maestro_ctx *ctx, struct img_mod *mod) {
        uint32_t i;
        struct img_mod *mods = (struct img_mod *)ctx->img_mods;

        for (i = 0; i < ctx->img_mod_nr; i++) {
                if (&mods[i] == mod)
                        return i;
        }

        return UINT32_MAX;
}

static maestro_value v_invalid(void) {
        maestro_value v;

        memset(&v, 0, sizeof(v));
        return v;
}

static maestro_value runtime_error_value(struct eval_res *eres) {
	if (eres)
		eres->ctrl = CTRL_ERROR;

	return v_invalid();
}

static int eval_failed(struct eval_res *eres, maestro_value v) {
	return v.type == MAESTRO_VAL_INVALID ||
	       (eres && eres->ctrl == CTRL_ERROR);
}

static maestro_value v_int(maestro_int_t i) {
        maestro_value v = v_invalid();

        v.type = MAESTRO_VAL_INT;
        v.v.i = i;
        return v;
}

static maestro_value v_float(maestro_float_t f) {
        maestro_value v = v_invalid();

        v.type = MAESTRO_VAL_FLOAT;
        v.v.f = f;
        return v;
}

static maestro_value v_bool(bool b) {
        maestro_value v = v_invalid();

        v.type = MAESTRO_VAL_BOOL;
        v.v.b = b;
        return v;
}

static maestro_value v_string(maestro_ctx *ctx, const char *s) {
        maestro_value v = v_invalid();

        v.type = MAESTRO_VAL_STRING;
        v.v.s = ctx->alloc(strlen(s) + 1);

        if (v.v.s)
                strcpy(v.v.s, s);

        return v;
}

static maestro_value v_string_borrow(maestro_ctx *ctx, const char *s) {
        maestro_value v = v_invalid();

        (void)ctx;
        v.type = MAESTRO_VAL_STRING;
        v.flags = MAESTRO_VALUE_F_BORROWED;
        v.v.s = (char *)s;
        return v;
}

static maestro_value v_symbol_borrow(maestro_ctx *ctx, const char *s) {
        maestro_value v = v_invalid();

        (void)ctx;
        v.type = MAESTRO_VAL_SYMBOL;
        v.flags = MAESTRO_VALUE_F_BORROWED;
        v.v.s = (char *)s;
        return v;
}

static maestro_value v_symbol(maestro_ctx *ctx, const char *s) {
        maestro_value v = v_invalid();

        v.type = MAESTRO_VAL_SYMBOL;
        v.v.s = ctx->alloc(strlen(s) + 1);

        if (v.v.s)
                strcpy(v.v.s, s);

        return v;
}

static maestro_value v_list(maestro_ctx *ctx) {
        maestro_value v = v_invalid();

        v.type = MAESTRO_VAL_LIST;
        v.v.list = ctx->alloc(sizeof(*v.v.list));

        if (v.v.list)
                memset(v.v.list, 0, sizeof(*v.v.list));

        return v;
}

static maestro_value v_object(maestro_ctx *ctx) {
        maestro_value v = v_invalid();

        v.type = MAESTRO_VAL_OBJECT;
        v.v.obj = ctx->alloc(sizeof(*v.v.obj));

        if (v.v.obj)
                memset(v.v.obj, 0, sizeof(*v.v.obj));

        return v;
}

static maestro_value v_handle(struct maestro_handle *h, int type) {
        maestro_value v = v_invalid();

        v.type = (uint32_t)type;
        v.v.handle = h;
        return v;
}

static maestro_value v_ref(struct maestro_ref *r) {
        maestro_value v = v_invalid();

        v.type = MAESTRO_VAL_REF;
        v.v.ref = r;
        return v;
}

struct json_cursor {
        const char *s;
};

static maestro_value clone_value(maestro_ctx *ctx, maestro_value v);
static int obj_set(maestro_ctx *ctx, struct maestro_object *obj,
                   const char *key,
                   maestro_value v);
static int parse_json_value(maestro_ctx *ctx, struct json_cursor *cur,
                            maestro_value *out);
static int json_snippet_value_ok(maestro_value v);
static int json_serialize_value_ok(maestro_value v);

static int list_push(maestro_ctx *ctx, struct maestro_list *list,
                     maestro_value v) {
        if (list->nr == list->cap) {
                size_t ncap = list->cap ? list->cap * 2 : 8;
                maestro_value *nv = xrealloc(ctx, list->item, ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                list->item = nv;
                list->cap = ncap;
        }

        list->item[list->nr++] = v;
        return 0;
}

static maestro_value clone_value(maestro_ctx *ctx, maestro_value v) {
        maestro_value out;
        size_t i;

        switch (v.type) {
        case MAESTRO_VAL_INVALID:
                return v_invalid();

        case MAESTRO_VAL_INT:
                return v_int(v.v.i);

        case MAESTRO_VAL_FLOAT:
                return v_float(v.v.f);

        case MAESTRO_VAL_BOOL:
                return v_bool(v.v.b);

        case MAESTRO_VAL_STRING:
                return v_string(ctx, v.v.s ? v.v.s : "");

        case MAESTRO_VAL_SYMBOL:
                out = v_invalid();
                out.type = MAESTRO_VAL_SYMBOL;
                out.v.s = ctx->alloc(strlen(v.v.s ? v.v.s : "") + 1);

                if (out.v.s)
                        strcpy(out.v.s, v.v.s ? v.v.s : "");

                return out;

        case MAESTRO_VAL_LIST:
                out = v_list(ctx);

                for (i = 0; out.v.list && i < v.v.list->nr; i++) {
                        maestro_value item = clone_value(ctx, v.v.list->item[i]);

                        if (list_push(ctx, out.v.list, item))
                                return v_invalid();
                }

                return out;

        case MAESTRO_VAL_OBJECT:
                out = v_object(ctx);

                for (i = 0; out.v.obj && i < v.v.obj->nr; i++) {
                        maestro_value item = clone_value(ctx, v.v.obj->val[i]);

                        if (obj_set(ctx, out.v.obj, v.v.obj->key[i], item))
                                return v_invalid();
                }

                return out;

        default:
                return v;
        }
}

static ssize_t obj_find(struct maestro_object *obj, const char *key) {
        size_t i;

        for (i = 0; i < obj->nr; i++) {
                if (!strcmp(obj->key[i], key))
                        return (ssize_t)i;
        }

        return -1;
}

static int obj_set(maestro_ctx *ctx, struct maestro_object *obj,
                   const char *key,
                   maestro_value v) {
        ssize_t idx = obj_find(obj, key);

        if (idx >= 0) {
                obj->val[idx] = v;
                return 0;
        }

        if (obj->nr == obj->cap) {
                size_t ncap = obj->cap ? obj->cap * 2 : 8;
                char **nk = xrealloc(ctx, obj->key, ncap * sizeof(*nk));
                maestro_value *nv = xrealloc(ctx, obj->val, ncap * sizeof(*nv));

                if (!nk || !nv)
                        return -1;

                obj->key = nk;
                obj->val = nv;
                obj->cap = ncap;
        }

        obj->key[obj->nr] = ctx->alloc(strlen(key) + 1);

        if (!obj->key[obj->nr])
                return -1;

        strcpy(obj->key[obj->nr], key);
        obj->val[obj->nr++] = v;
        return 0;
}

static maestro_value obj_get(struct maestro_object *obj, const char *key) {
        ssize_t idx = obj_find(obj, key);

        if (idx < 0)
                return v_invalid();

        return obj->val[idx];
}

void maestro_value_reset(maestro_ctx *ctx, maestro_value *v) {
        size_t i;

        if (!v)
                return;

        switch (v->type) {
        case MAESTRO_VAL_STRING:
        case MAESTRO_VAL_SYMBOL:
                if (v->v.s && !(v->flags & MAESTRO_VALUE_F_BORROWED))
                        ctx->dealloc(v->v.s);

                break;

        case MAESTRO_VAL_LIST:
                if (v->v.list) {
                        for (i = 0; i < v->v.list->nr; i++)
                                maestro_value_reset(ctx, &v->v.list->item[i]);

                        ctx->dealloc(v->v.list->item);
                        ctx->dealloc(v->v.list);
                }

                break;

        case MAESTRO_VAL_OBJECT:
                if (v->v.obj) {
                        for (i = 0; i < v->v.obj->nr; i++) {
                                ctx->dealloc(v->v.obj->key[i]);
                                maestro_value_reset(ctx, &v->v.obj->val[i]);
                        }

                        ctx->dealloc(v->v.obj->key);
                        ctx->dealloc(v->v.obj->val);
                        ctx->dealloc(v->v.obj);
                }

                break;

        default:
                break;
        }

        *v = v_invalid();
}

static char *fmt_string(const char *fmt, ...) {
        va_list ap;
        va_list aq;
        int need;
        char *buf;

        va_start(ap, fmt);
        va_copy(aq, ap);
        need = vsnprintf(NULL, 0, fmt, aq);
        va_end(aq);

        if (need < 0) {
                va_end(ap);
                return NULL;
        }

        buf = malloc((size_t)need + 1);

        if (!buf) {
                va_end(ap);
                return NULL;
        }

        vsnprintf(buf, (size_t)need + 1, fmt, ap);
        va_end(ap);
        return buf;
}

char *maestro_value_stringify(maestro_ctx *ctx, maestro_value v) {
        size_t i;
        char *buf, *tmp;

        (void)ctx;

        switch (v.type) {
        case MAESTRO_VAL_INT:
                return fmt_string("%lld", (long long)v.v.i);

        case MAESTRO_VAL_FLOAT:
                return fmt_string("%g", (double)v.v.f);

        case MAESTRO_VAL_STRING:
        case MAESTRO_VAL_SYMBOL:
                return xstrdup(v.v.s ? v.v.s : "");

        case MAESTRO_VAL_BOOL:
                return xstrdup(v.v.b ? "true" : "false");

        case MAESTRO_VAL_LIST:
                buf = xstrdup("[");

                if (!buf)
                        return NULL;

                for (i = 0; i < v.v.list->nr; i++) {
                        tmp = maestro_value_stringify(ctx, v.v.list->item[i]);

                        if (!tmp)
                                return buf;

                        {
                                char *next = fmt_string("%s%s%s", buf, i ? "," : "", tmp);
                                free(buf);
                                free(tmp);
                                buf = next;
                        }
                }

                tmp = fmt_string("%s]", buf);
                free(buf);
                return tmp;

        case MAESTRO_VAL_OBJECT:
                buf = xstrdup("{");

                if (!buf)
                        return NULL;

                for (i = 0; i < v.v.obj->nr; i++) {
                        char *val = maestro_value_stringify(ctx, v.v.obj->val[i]);
                        char *next;

                        if (!val)
                                return buf;

                        next = fmt_string("%s%s\"%s\":%s", buf, i ? "," : "",
                                          v.v.obj->key[i], val);
                        free(val);
                        free(buf);
                        buf = next;
                }

                tmp = fmt_string("%s}", buf);
                free(buf);
                return tmp;

        default:
                return xstrdup("<value>");
        }
}

static void json_skip_ws(struct json_cursor *cur) {
        while (*cur->s && isspace((unsigned char) * cur->s))
                cur->s++;
}

static int json_take(struct json_cursor *cur, char ch) {
        json_skip_ws(cur);

        if (*cur->s != ch)
                return -1;

        cur->s++;
        return 0;
}

static char *json_parse_string_raw(maestro_ctx *ctx, struct json_cursor *cur) {
        size_t cap = 16;
        size_t nr = 0;
        char *buf;

        json_skip_ws(cur);

        if (*cur->s != '"')
                return NULL;

        cur->s++;
        buf = ctx->alloc(cap);

        if (!buf)
                return NULL;

        while (*cur->s && *cur->s != '"') {
                char ch = *cur->s++;

                if (ch == '\\') {
                        ch = *cur->s++;

                        switch (ch) {
                        case '"':
                        case '\\':
                        case '/':
                                break;

                        case 'b':
                                ch = '\b';
                                break;

                        case 'f':
                                ch = '\f';
                                break;

                        case 'n':
                                ch = '\n';
                                break;

                        case 'r':
                                ch = '\r';
                                break;

                        case 't':
                                ch = '\t';
                                break;

                        default:
                                ctx->dealloc(buf);
                                return NULL;
                        }
                }

                if (nr + 2 > cap) {
                        size_t ncap = cap * 2;
                        char *nbuf = xrealloc(ctx, buf, ncap);

                        if (!nbuf) {
                                ctx->dealloc(buf);
                                return NULL;
                        }

                        buf = nbuf;
                        cap = ncap;
                }

                buf[nr++] = ch;
        }

        if (*cur->s != '"') {
                ctx->dealloc(buf);
                return NULL;
        }

        cur->s++;
        buf[nr] = 0;
        return buf;
}

static int json_parse_number(maestro_ctx *ctx, struct json_cursor *cur,
                             maestro_value *out) {
        const char *start;
        char *end;

        json_skip_ws(cur);
        start = cur->s;

        if (*cur->s == '-')
                cur->s++;

        if (!isdigit((unsigned char) * cur->s))
                return -1;

        if (*cur->s == '0') {
                cur->s++;
        } else {
                while (isdigit((unsigned char) * cur->s))
                        cur->s++;
        }

        if (*cur->s == '.') {
                cur->s++;

                if (!isdigit((unsigned char) * cur->s))
                        return -1;

                while (isdigit((unsigned char) * cur->s))
                        cur->s++;
        }

        if (*cur->s == 'e' || *cur->s == 'E') {
                cur->s++;

                if (*cur->s == '+' || *cur->s == '-')
                        cur->s++;

                if (!isdigit((unsigned char) * cur->s))
                        return -1;

                while (isdigit((unsigned char) * cur->s))
                        cur->s++;
        }

        if (memchr(start, '.', (size_t)(cur->s - start)) ||
            memchr(start, 'e', (size_t)(cur->s - start)) ||
            memchr(start, 'E', (size_t)(cur->s - start))) {
                float f = strtof(start, &end);

                if (end != cur->s)
                        return -1;

                *out = v_float((maestro_float_t)f);
        } else {
                long long i = strtoll(start, &end, 10);

                if (end != cur->s)
                        return -1;

                *out = v_int((maestro_int_t)i);
        }

        (void)ctx;
        return 0;
}

static int json_parse_array(maestro_ctx *ctx, struct json_cursor *cur,
                            maestro_value *out) {
        maestro_value list = v_list(ctx);

        if (list.type != MAESTRO_VAL_LIST || !list.v.list)
                return -1;

        if (json_take(cur, '[')) {
                maestro_value_reset(ctx, &list);
                return -1;
        }

        json_skip_ws(cur);

        if (*cur->s == ']') {
                cur->s++;
                *out = list;
                return 0;
        }

        for (;;) {
                maestro_value item = v_invalid();

                if (parse_json_value(ctx, cur, &item)) {
                        maestro_value_reset(ctx, &list);
                        return -1;
                }

                if (list_push(ctx, list.v.list, item)) {
                        maestro_value_reset(ctx, &item);
                        maestro_value_reset(ctx, &list);
                        return -1;
                }

                json_skip_ws(cur);

                if (*cur->s == ']') {
                        cur->s++;
                        *out = list;
                        return 0;
                }

                if (*cur->s != ',') {
                        maestro_value_reset(ctx, &list);
                        return -1;
                }

                cur->s++;
        }
}

static int json_parse_object(maestro_ctx *ctx, struct json_cursor *cur,
                             maestro_value *out) {
        maestro_value obj = v_object(ctx);

        if (obj.type != MAESTRO_VAL_OBJECT || !obj.v.obj)
                return -1;

        if (json_take(cur, '{')) {
                maestro_value_reset(ctx, &obj);
                return -1;
        }

        json_skip_ws(cur);

        if (*cur->s == '}') {
                cur->s++;
                *out = obj;
                return 0;
        }

        for (;;) {
                char *key;
                maestro_value val = v_invalid();

                key = json_parse_string_raw(ctx, cur);

                if (!key || json_take(cur, ':') || parse_json_value(ctx, cur, &val) ||
                    obj_set(ctx, obj.v.obj, key, val)) {
                        if (key)
                                ctx->dealloc(key);

                        maestro_value_reset(ctx, &val);
                        maestro_value_reset(ctx, &obj);
                        return -1;
                }

                ctx->dealloc(key);
                json_skip_ws(cur);

                if (*cur->s == '}') {
                        cur->s++;
                        *out = obj;
                        return 0;
                }

                if (*cur->s != ',') {
                        maestro_value_reset(ctx, &obj);
                        return -1;
                }

                cur->s++;
        }
}

static int parse_json_value(maestro_ctx *ctx, struct json_cursor *cur,
                            maestro_value *out) {
        json_skip_ws(cur);

        switch (*cur->s) {
        case '{':
                return json_parse_object(ctx, cur, out);

        case '[':
                return json_parse_array(ctx, cur, out);

        case '"': {
                char *s = json_parse_string_raw(ctx, cur);

                if (!s)
                        return -1;

                *out = v_string(ctx, s);
                ctx->dealloc(s);
                return out->v.s ? 0 : -1;
        }

        case 't':
                if (!strncmp(cur->s, "true", 4)) {
                        cur->s += 4;
                        *out = v_bool(true);
                        return 0;
                }

                return -1;

        case 'f':
                if (!strncmp(cur->s, "false", 5)) {
                        cur->s += 5;
                        *out = v_bool(false);
                        return 0;
                }

                return -1;

        default:
                if (*cur->s == '-' || isdigit((unsigned char) * cur->s))
                        return json_parse_number(ctx, cur, out);

                return -1;
        }
}

static int json_parse_text(maestro_ctx *ctx, const char *text,
                           maestro_value *out) {
        struct json_cursor cur = { .s = text ? text : "" };

        if (!ctx || !out || parse_json_value(ctx, &cur, out))
                return -1;

        json_skip_ws(&cur);
        return *cur.s ? -1 : 0;
}

static int json_parse_object_text(maestro_ctx *ctx, const char *text,
                                  maestro_value *out) {
        if (json_parse_text(ctx, text, out))
                return -1;

        return out->type == MAESTRO_VAL_OBJECT ? 0 : -1;
}

static int json_snippet_value_ok(maestro_value v) {
        size_t i;

        switch (v.type) {
        case MAESTRO_VAL_INT:
        case MAESTRO_VAL_FLOAT:
        case MAESTRO_VAL_STRING:
                return 1;

        case MAESTRO_VAL_LIST:
                if (!v.v.list)
                        return 0;

                for (i = 0; i < v.v.list->nr; i++) {
                        if (v.v.list->item[i].type == MAESTRO_VAL_SYMBOL ||
                            !json_snippet_value_ok(v.v.list->item[i]))
                                return 0;
                }

                return 1;

        case MAESTRO_VAL_OBJECT:
                if (!v.v.obj)
                        return 0;

                for (i = 0; i < v.v.obj->nr; i++) {
                        if (!json_snippet_value_ok(v.v.obj->val[i]))
                                return 0;
                }

                return 1;

        default:
                return 0;
        }
}

static int json_serialize_value_ok(maestro_value v) {
        size_t i;

        switch (v.type) {
        case MAESTRO_VAL_INT:
        case MAESTRO_VAL_FLOAT:
        case MAESTRO_VAL_STRING:
        case MAESTRO_VAL_BOOL:
                return 1;

        case MAESTRO_VAL_LIST:
                if (!v.v.list)
                        return 0;

                for (i = 0; i < v.v.list->nr; i++) {
                        if (!json_serialize_value_ok(v.v.list->item[i]))
                                return 0;
                }

                return 1;

        case MAESTRO_VAL_OBJECT:
                if (!v.v.obj)
                        return 0;

                for (i = 0; i < v.v.obj->nr; i++) {
                        if (!json_serialize_value_ok(v.v.obj->val[i]))
                                return 0;
                }

                return 1;

        default:
                return 0;
        }
}

struct json_buf {
        char *buf;
        size_t len;
        size_t cap;
};

static int json_buf_append(struct json_buf *buf, const char *s) {
        size_t add = strlen(s);
        char *nbuf;

        if (buf->len + add + 1 <= buf->cap) {
                memcpy(buf->buf + buf->len, s, add + 1);
                buf->len += add;
                return 0;
        }

        {
                size_t ncap = buf->cap ? buf->cap * 2 : 64;

                while (ncap < buf->len + add + 1)
                        ncap *= 2;

                nbuf = realloc(buf->buf, ncap);

                if (!nbuf)
                        return -1;

                buf->buf = nbuf;
                buf->cap = ncap;
        }

        memcpy(buf->buf + buf->len, s, add + 1);
        buf->len += add;
        return 0;
}

static int json_buf_append_ch(struct json_buf *buf, char ch) {
        char tmp[2] = { ch, 0 };

        return json_buf_append(buf, tmp);
}

static int json_buf_append_escaped(struct json_buf *buf, const char *s) {
        if (json_buf_append_ch(buf, '"'))
                return -1;

        while (*s) {
                switch (*s) {
                case '"':
                        if (json_buf_append(buf, "\\\""))
                                return -1;

                        break;

                case '\\':
                        if (json_buf_append(buf, "\\\\"))
                                return -1;

                        break;

                case '\b':
                        if (json_buf_append(buf, "\\b"))
                                return -1;

                        break;

                case '\f':
                        if (json_buf_append(buf, "\\f"))
                                return -1;

                        break;

                case '\n':
                        if (json_buf_append(buf, "\\n"))
                                return -1;

                        break;

                case '\r':
                        if (json_buf_append(buf, "\\r"))
                                return -1;

                        break;

                case '\t':
                        if (json_buf_append(buf, "\\t"))
                                return -1;

                        break;

                default:
                        if (json_buf_append_ch(buf, *s))
                                return -1;

                        break;
                }

                s++;
        }

        return json_buf_append_ch(buf, '"');
}

static int json_serialize_value(struct json_buf *buf, maestro_value v) {
        size_t i;
        char tmp[64];

        switch (v.type) {
        case MAESTRO_VAL_INT:
                snprintf(tmp, sizeof(tmp), "%lld", (long long)v.v.i);
                return json_buf_append(buf, tmp);

        case MAESTRO_VAL_FLOAT:
                snprintf(tmp, sizeof(tmp), "%g", (double)v.v.f);
                return json_buf_append(buf, tmp);

        case MAESTRO_VAL_BOOL:
                return json_buf_append(buf, v.v.b ? "true" : "false");

        case MAESTRO_VAL_STRING:
                return json_buf_append_escaped(buf, v.v.s ? v.v.s : "");

        case MAESTRO_VAL_LIST:
                if (json_buf_append_ch(buf, '['))
                        return -1;

                for (i = 0; i < v.v.list->nr; i++) {
                        if ((i && json_buf_append_ch(buf, ',')) ||
                            json_serialize_value(buf, v.v.list->item[i]))
                                return -1;
                }

                return json_buf_append_ch(buf, ']');

        case MAESTRO_VAL_OBJECT:
                if (json_buf_append_ch(buf, '{'))
                        return -1;

                for (i = 0; i < v.v.obj->nr; i++) {
                        if ((i && json_buf_append_ch(buf, ',')) ||
                            json_buf_append_escaped(buf, v.v.obj->key[i]) ||
                            json_buf_append_ch(buf, ':') ||
                            json_serialize_value(buf, v.v.obj->val[i]))
                                return -1;
                }

                return json_buf_append_ch(buf, '}');

        default:
                return -1;
        }
}

static char *json_serialize_object(maestro_value v) {
        struct json_buf buf = {0};

        if (v.type != MAESTRO_VAL_OBJECT || !json_serialize_value_ok(v))
                return NULL;

        if (json_serialize_value(&buf, v)) {
                free(buf.buf);
                return NULL;
        }

        return buf.buf;
}

static bool value_truthy(maestro_value v) {
        switch (v.type) {
        case MAESTRO_VAL_BOOL:
                return v.v.b;

        case MAESTRO_VAL_INT:
                return v.v.i != 0;

        case MAESTRO_VAL_FLOAT:
                return v.v.f != 0.0f;

        case MAESTRO_VAL_STRING:
        case MAESTRO_VAL_SYMBOL:
                return v.v.s && v.v.s[0] != 0;

        case MAESTRO_VAL_LIST:
                return v.v.list && v.v.list->nr != 0;

        case MAESTRO_VAL_OBJECT:
                return v.v.obj && v.v.obj->nr != 0;

        case MAESTRO_VAL_STATE:
        case MAESTRO_VAL_MACRO:
        case MAESTRO_VAL_REF:
                return true;

        default:
                return false;
        }
}

static bool value_eq(maestro_value a, maestro_value b);
static maestro_value ref_read(struct maestro_ref *ref);

static maestro_value deref_value(maestro_value v) {
        if (v.type == MAESTRO_VAL_REF)
                return ref_read(v.v.ref);

        return v;
}
static int ref_write(maestro_ctx *ctx, struct maestro_ref *ref,
                     maestro_value v);

static bool num_eq(maestro_value a, maestro_value b) {
        double da = (a.type == MAESTRO_VAL_INT) ? (double)a.v.i : (double)a.v.f;
        double db = (b.type == MAESTRO_VAL_INT) ? (double)b.v.i : (double)b.v.f;

        return da == db;
}

static bool value_eq(maestro_value a, maestro_value b) {
        size_t i;

        if ((a.type == MAESTRO_VAL_INT || a.type == MAESTRO_VAL_FLOAT) &&
            (b.type == MAESTRO_VAL_INT || b.type == MAESTRO_VAL_FLOAT))
                return num_eq(a, b);

        if (a.type != b.type)
                return false;

        switch (a.type) {
        case MAESTRO_VAL_INT:
                return a.v.i == b.v.i;

        case MAESTRO_VAL_FLOAT:
                return a.v.f == b.v.f;

        case MAESTRO_VAL_STRING:
        case MAESTRO_VAL_SYMBOL:
                return !strcmp(a.v.s, b.v.s);

        case MAESTRO_VAL_BOOL:
                return a.v.b == b.v.b;

        case MAESTRO_VAL_REF:
                return value_eq(ref_read(a.v.ref), ref_read(b.v.ref));

        case MAESTRO_VAL_STATE:
        case MAESTRO_VAL_MACRO:
                return a.v.handle->module_idx == b.v.handle->module_idx &&
                       a.v.handle->name_id == b.v.handle->name_id;

        case MAESTRO_VAL_LIST:
                if (a.v.list->nr != b.v.list->nr)
                        return false;

                for (i = 0; i < a.v.list->nr; i++) {
                        if (!value_eq(a.v.list->item[i], b.v.list->item[i]))
                                return false;
                }

                return true;

        case MAESTRO_VAL_OBJECT:
                if (a.v.obj->nr != b.v.obj->nr)
                        return false;

                for (i = 0; i < a.v.obj->nr; i++) {
                        maestro_value bv = obj_get(b.v.obj, a.v.obj->key[i]);

                        if (!bv.type || !value_eq(a.v.obj->val[i], bv))
                                return false;
                }

                return true;

        default:
                return false;
        }
}

static struct maestro_scope *scope_new(maestro_ctx *ctx,
                                       struct maestro_scope *up) {
        struct maestro_scope *s = ctx->alloc(sizeof(*s));

        if (!s)
                return NULL;

        memset(s, 0, sizeof(*s));
        s->up = up;
        return s;
}

static struct maestro_binding *scope_find_here(struct maestro_scope *s,
                uint32_t ident_id) {
        size_t i;

        for (i = 0; i < s->nr; i++) {
                if (s->bind[i].ident_id == ident_id)
                        return &s->bind[i];
        }

        return NULL;
}

static struct maestro_binding *scope_find(struct maestro_scope *s,
                uint32_t ident_id) {
        for (; s; s = s->up) {
                struct maestro_binding *b = scope_find_here(s, ident_id);

                if (b)
                        return b;
        }

        return NULL;
}

static int scope_set(maestro_ctx *ctx, struct maestro_scope *s,
                     uint32_t ident_id,
                     maestro_value v) {
        struct maestro_binding *b = scope_find_here(s, ident_id);

        if (b) {
                b->value = v;
                return 0;
        }

        if (s->nr == s->cap) {
                size_t ncap = s->cap ? s->cap * 2 : 8;
                struct maestro_binding *nv = xrealloc(ctx, s->bind,
                                                      ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                s->bind = nv;
                s->cap = ncap;
        }

        s->bind[s->nr].ident_id = ident_id;
        s->bind[s->nr].value = v;
        s->nr++;
        return 0;
}

static maestro_value eval_node(struct run_ctx *rctx, struct img_mod *mod,
                               struct maestro_scope *scope, uint32_t idx,
                               struct eval_res *eres);
static maestro_value run_program(struct run_ctx *parent, struct img_mod *mod,
                                 maestro_value *args, size_t argc);
static maestro_value run_state(struct run_ctx *rctx, struct img_mod *mod,
                               struct maestro_handle *state, maestro_value *args,
                               size_t argc);

static bool is_builtin_name(const char *name) {
        static const char *const builtins[] = {
                "list", "cons", "json", "json-parse",
                "and", "or", "not", "+", "-", "*", "/", "%",
                "=", "!=", "<", "<=", ">", ">=", "ref=?",
                "concat", "append", "substr", "to-string", "floor", "ceil",
                "map", "filter", "foldl", "foldr", "any?", "all?", "probe",
                "log", "print", "empty?", "true?", "false?",
                "number?", "integer?", "float?", "string?", "list?",
                "object?", "symbol?", "boolean?", "ref?", "state?",
                "macro?", "get", "ref", "set", "let", "steps",
                "case", "transition", "run", "import", "import-program"
        };
        size_t i;

        for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
                if (!strcmp(builtins[i], name))
                        return true;
        }

        return false;
}

static struct module_scope *module_scope_get(struct run_ctx *rctx,
                struct img_mod *mod) {
        struct module_scope *ms;

        for (ms = rctx->mods_cache; ms; ms = ms->next) {
                if (ms->mod == mod)
                        return ms;
        }

        ms = rctx->ctx->alloc(sizeof(*ms));

        if (!ms)
                return NULL;

        memset(ms, 0, sizeof(*ms));
        ms->mod = mod;
        ms->globals = scope_new(rctx->ctx, NULL);

        if (!ms->globals)
                return NULL;

        ms->next = rctx->mods_cache;
        rctx->mods_cache = ms;
        return ms;
}

static int mod_path_eq_form(struct run_ctx *rctx, struct img_mod *mod,
                            struct img_node *form, uint32_t start, uint32_t end) {
        uint32_t i;

        if (!mod || end <= start || end > form->nr || mod->path_nr != end - start)
                return 0;

        for (i = start; i < end; i++) {
                struct img_node *seg = img_node(rctx->ctx, form->first + i);

                if (seg->type != IMG_NODE_IDENT && seg->type != IMG_NODE_STRING &&
                    seg->type != IMG_NODE_SYMBOL)
                        return 0;

                if (strcmp(node_text(rctx->ctx, seg),
                           img_ident_name(rctx->ctx,
                                          img_path_by_idx(rctx->ctx,
                                                          mod->path_first + i - start)->ident_id)))
                        return 0;
        }

        return 1;
}

static struct img_mod *find_mod_from_form(struct run_ctx *rctx,
                struct img_node *form,
                uint32_t start, uint32_t end) {
        uint32_t i;

        for (i = 0; i < rctx->ctx->img_mod_nr; i++) {
                struct img_mod *mod = img_mod_by_idx(rctx->ctx, i);

                if (mod_path_eq_form(rctx, mod, form, start, end))
                        return mod;
        }

        return NULL;
}

static maestro_value make_handle(struct run_ctx *rctx, struct img_mod *mod,
                                 int kind,
                                 uint32_t name_id, uint32_t sig_idx, uint32_t body_idx) {
        struct maestro_handle *h;
        struct img_node *sig = img_node(rctx->ctx, sig_idx);
        uint32_t j;

        h = rctx->ctx->alloc(sizeof(*h));

        if (!h)
                return v_invalid();

        memset(h, 0, sizeof(*h));
        h->kind = kind;
        h->module_idx = find_mod_idx(rctx->ctx, mod);
        h->name_id = name_id;
        h->node_idx = sig_idx;
        h->body_idx = body_idx;

        if (sig->type == IMG_NODE_FORM && sig->nr > 1) {
                h->argc = sig->nr - 1;
                h->argv_id = rctx->ctx->alloc(h->argc * sizeof(*h->argv_id));
                h->arg_ref = rctx->ctx->alloc(h->argc);

                if (!h->argv_id || !h->arg_ref)
                        return v_invalid();

                memset(h->arg_ref, 0, h->argc);

                for (j = 1; j < sig->nr; j++) {
                        struct img_node *arg = img_node(rctx->ctx, sig->first + j);

                        if (arg->type == IMG_NODE_FORM && arg->nr == 2 &&
                            img_node(rctx->ctx, arg->first)->type == IMG_NODE_IDENT &&
                            !strcmp(node_ident_name(rctx->ctx,
                                                    img_node(rctx->ctx, arg->first)), "ref")) {
                                h->argv_id[j - 1] = img_node(rctx->ctx, arg->first + 1)->str_off;
                                h->arg_ref[j - 1] = 1;
                        } else {
                                h->argv_id[j - 1] = arg->str_off;
                        }
                }
        }

        return v_handle(h, kind == HANDLE_STATE ? MAESTRO_VAL_STATE :
                        kind == HANDLE_PROGRAM ? MAESTRO_VAL_PROGRAM :
                        kind == HANDLE_BUILTIN ? MAESTRO_VAL_BUILTIN :
                        MAESTRO_VAL_MACRO);
}

static bool resolving(struct resolve_frame *frame, uint32_t mod_idx,
                      const char *name) {
        for (; frame; frame = frame->up) {
                if (frame->mod_idx == mod_idx && !strcmp(frame->name, name))
                        return true;
        }

        return false;
}

static maestro_value resolve_global(struct run_ctx *rctx, struct img_mod *mod,
                                    const char *name, struct resolve_frame *frame);

static maestro_value invoke_callback_binding(struct run_ctx *rctx,
                                             maestro_value fn,
                                             maestro_value *argv,
                                             size_t argc);

static maestro_value resolve_import(struct run_ctx *rctx,
                                    struct img_mod *mod_unused,
                                    struct img_node *form, const char *name,
                                    struct resolve_frame *frame) {
        struct img_mod *target;
        const char *import_name;
        maestro_value v = v_invalid();

        if (form->nr < 3)
                return v_invalid();

        (void)mod_unused;
        target = find_mod_from_form(rctx, form, 1, form->nr - 1);
        import_name = node_text(rctx->ctx,
                                img_node(rctx->ctx, form->first + form->nr - 1));

        if (target) {
                if (!strcmp(import_name, "*"))
                        v = resolve_global(rctx, target, name, frame);
                else if (!strcmp(import_name, name))
                        v = resolve_global(rctx, target, import_name, frame);
        }

        return v;
}

static maestro_value resolve_global(struct run_ctx *rctx, struct img_mod *mod,
                                    const char *name, struct resolve_frame *frame) {
        struct module_scope *ms = module_scope_get(rctx, mod);
        struct maestro_binding *b;
        struct img_node *root;
        uint32_t ident_id = find_ident_id_by_name(rctx->ctx, name);
        uint32_t i;
        struct resolve_frame next = {
                .mod_idx = find_mod_idx(rctx->ctx, mod),
                .name = name,
                .up = frame,
        };

        if (!ms)
                return v_invalid();

        b = ident_id != UINT32_MAX ? scope_find_here(ms->globals, ident_id) : NULL;

        if (b)
                return b->value;

        if (resolving(frame, next.mod_idx, name))
                return v_invalid();

        if (is_builtin_name(name))
                return make_handle(rctx, mod, HANDLE_BUILTIN, ident_id, 0, 0);

        root = img_node(rctx->ctx, mod->root_idx);

        for (i = 0; i < root->nr; i++) {
                struct img_node *f = img_node(rctx->ctx, root->first + i);
                struct img_node *head;

                if (f->type != IMG_NODE_FORM || f->nr < 1)
                        continue;

                head = img_node(rctx->ctx, f->first);

                if (head->type != IMG_NODE_IDENT)
                        continue;

                if (!strcmp(node_ident_name(rctx->ctx, head), "define") && f->nr >= 3) {
                        struct img_node *sig = img_node(rctx->ctx, f->first + 1);

                        if (sig->type == IMG_NODE_IDENT &&
                            !strcmp(node_ident_name(rctx->ctx, sig), name)) {
                                maestro_value v = eval_node(rctx, mod, ms->globals,
                                f->first + 2, &(struct eval_res) {
                                        0
                                });
                                scope_set(rctx->ctx, ms->globals, sig->str_off, v);
                                return v;
                        }

                        if (sig->type == IMG_NODE_FORM && sig->nr >= 1) {
                                struct img_node *n = img_node(rctx->ctx, sig->first);

                                if (n->type == IMG_NODE_IDENT &&
                                    !strcmp(node_ident_name(rctx->ctx, n), name)) {
                                        maestro_value v = make_handle(rctx, mod, HANDLE_MACRO,
                                                                      n->str_off, f->first + 1, f->first + 2);
                                        scope_set(rctx->ctx, ms->globals, n->str_off, v);
                                        return v;
                                }
                        }
                }

                if (!strcmp(node_ident_name(rctx->ctx, head), "state") && f->nr >= 3) {
                        struct img_node *sig = img_node(rctx->ctx, f->first + 1);
                        struct img_node *n;

                        if (sig->type != IMG_NODE_FORM || sig->nr < 1)
                                continue;

                        n = img_node(rctx->ctx, sig->first);

                        if (n->type == IMG_NODE_IDENT &&
                            !strcmp(node_ident_name(rctx->ctx, n), name)) {
                                maestro_value v = make_handle(rctx, mod, HANDLE_STATE,
                                                              n->str_off, f->first + 1, f->first + 2);
                                scope_set(rctx->ctx, ms->globals, n->str_off, v);
                                return v;
                        }
                }

                if (!strcmp(node_ident_name(rctx->ctx, head), "import")) {
                        maestro_value v = resolve_import(rctx, mod, f, name, &next);

                        if (v.type) {
                                scope_set(rctx->ctx, ms->globals, ident_id, v);
                                return v;
                        }
                }
        }

        return v_invalid();
}

static maestro_value eval_ident(struct run_ctx *rctx, struct img_mod *mod,
                                struct maestro_scope *scope, const char *name) {
        struct maestro_binding *b;
        uint32_t ident_id = find_ident_id_by_name(rctx->ctx, name);

        if (!strcmp(name, "true"))
                return v_bool(true);

        if (!strcmp(name, "false"))
                return v_bool(false);

        if (!strcmp(name, "empty-string"))
                return v_string_borrow(rctx->ctx, "");

        if (!strcmp(name, "empty-list"))
                return v_list(rctx->ctx);

        if (!strcmp(name, "empty-object"))
                return v_object(rctx->ctx);

        if (!strcmp(name, "last-state"))
                return rctx->last_state;

        b = ident_id != UINT32_MAX ? scope_find(scope, ident_id) : NULL;

        if (b)
                return b->value;

        return resolve_global(rctx, mod, name, NULL);
}

static int ensure_object_path(maestro_ctx *ctx, maestro_value *root,
                              char **path,
                              size_t nr, maestro_value **slot) {
        size_t i;
        maestro_value *cur = root;

        for (i = 0; i < nr; i++) {
                maestro_value child;

                if (cur->type != MAESTRO_VAL_OBJECT)
                        return -1;

                child = obj_get(cur->v.obj, path[i]);

                if (!child.type) {
                        child = v_object(ctx);

                        if (obj_set(ctx, cur->v.obj, path[i], child))
                                return -1;
                }

                cur = &cur->v.obj->val[obj_find(cur->v.obj, path[i])];
        }

        *slot = cur;
        return 0;
}

static int lookup_object_path(maestro_value *root, char **path, size_t nr,
                              maestro_value **slot) {
        size_t i;
        maestro_value *cur = root;

        for (i = 0; i < nr; i++) {
                if (cur->type != MAESTRO_VAL_OBJECT)
                        return -1;

                if (obj_find(cur->v.obj, path[i]) < 0)
                        return -1;

                cur = &cur->v.obj->val[obj_find(cur->v.obj, path[i])];
        }

        *slot = cur;
        return 0;
}

enum path_eval_result {
        PATH_EVAL_OK = 0,
        PATH_EVAL_INVALID_SEG = -1,
        PATH_EVAL_ABORT = -2,
        PATH_EVAL_OOM = -3,
};

static void free_owned_path(maestro_ctx *ctx, char **path, size_t nr) {
        if (!ctx || !path)
                return;

        while (nr) {
                nr--;
                if (path[nr])
                        ctx->dealloc(path[nr]);
        }

        ctx->dealloc(path);
}

static enum path_eval_result eval_symbol_path(struct run_ctx *rctx,
                                              struct img_mod *mod,
                                              struct maestro_scope *scope,
                                              const char *op, uint32_t first,
                                              size_t nr, struct eval_res *eres,
                                              char ***path_out) {
        char **path;
        size_t i;

        *path_out = NULL;
        path = rctx->ctx->alloc(nr * sizeof(*path));

        if (!path)
                return PATH_EVAL_OOM;

        memset(path, 0, nr * sizeof(*path));

        for (i = 0; i < nr; i++) {
                struct eval_res inner = {0};
                maestro_value seg = eval_node(rctx, mod, scope, first + i, &inner);
                const char *sym = maestro_value_as_symbol(&seg);

                if (eval_failed(&inner, seg) || inner.ctrl != CTRL_OK) {
                        if (eres)
                                *eres = inner;
                        free_owned_path(rctx->ctx, path, nr);
                        return PATH_EVAL_ABORT;
                }

                if (!sym) {
                        free_owned_path(rctx->ctx, path, nr);
                        vm_log_builtin_error(rctx, op,
                                             "path segments must evaluate to symbols");
                        return PATH_EVAL_INVALID_SEG;
                }

                path[i] = rctx->ctx->alloc(strlen(sym) + 1);

                if (!path[i]) {
                        free_owned_path(rctx->ctx, path, nr);
                        return PATH_EVAL_OOM;
                }

                strcpy(path[i], sym);
        }

        *path_out = path;
        return PATH_EVAL_OK;
}

static maestro_value ref_read(struct maestro_ref *ref) {
        maestro_value *slot;

        if (ref->kind == REF_BINDING && ref->binding)
                return ref->binding->value;

        if (ref->kind == REF_PATH && ref->binding) {
                slot = &ref->binding->value;

                if (!lookup_object_path(slot, ref->path, ref->path_nr, &slot))
                        return *slot;
        }

        return v_invalid();
}

static int ref_write(maestro_ctx *ctx, struct maestro_ref *ref,
                     maestro_value v) {
        maestro_value *slot;

        if (ref->kind == REF_BINDING && ref->binding) {
                ref->binding->value = v;
                return 0;
        }

        if (ref->kind == REF_PATH && ref->binding) {
                slot = &ref->binding->value;

                if (lookup_object_path(slot, ref->path, ref->path_nr, &slot))
                        return -1;

                *slot = v;
                (void)ctx;
                return 0;
        }

        return -1;
}

static maestro_value eval_json(struct run_ctx *rctx, struct img_mod *mod,
                               struct maestro_scope *scope, struct img_node *node,
                               struct eval_res *eres) {
        maestro_value obj = v_object(rctx->ctx);
        uint32_t i;

        (void)mod;

        for (i = 0; i < node->nr; i++) {
                struct img_kv *kv = &((struct img_kv *)rctx->ctx->img_kv)[node->first + i];
                maestro_value v = eval_node(rctx, mod, scope, kv->val_idx, eres);

                if (!json_snippet_value_ok(v)) {
                        vm_log_error(rctx->ctx,
                                     "json snippet field \"%s\" is not JSON-compatible",
                                     img_str(rctx->ctx, kv->key_off));
                        maestro_value_reset(rctx->ctx, &obj);
                        return v_invalid();
                }

                if (obj_set(rctx->ctx, obj.v.obj, img_str(rctx->ctx, kv->key_off), v)) {
                        vm_log_error(rctx->ctx,
                                     "unable to store json snippet field \"%s\"",
                                     img_str(rctx->ctx, kv->key_off));
                        maestro_value_reset(rctx->ctx, &obj);
                        return v_invalid();
                }
        }

        return obj;
}

static maestro_value invoke_callback_binding(struct run_ctx *rctx,
                                             maestro_value fn,
                                             maestro_value *argv,
                                             size_t argc) {
        struct maestro_handle *h;
        struct img_mod *call_mod;
        struct module_scope *ms;
        struct maestro_scope *call = NULL;
        struct img_node *body;
        struct eval_res inner = {0};
        maestro_value out = v_invalid();
        size_t i;

        if (!rctx || fn.type != MAESTRO_VAL_MACRO || !fn.v.handle)
                return v_invalid();

        h = fn.v.handle;
        call_mod = img_mod_by_idx(rctx->ctx, h->module_idx);

        if (!call_mod || h->argc != argc)
                return v_invalid();

        for (i = 0; i < argc; i++) {
                if (h->arg_ref && h->arg_ref[i]) {
                        vm_log_error(rctx->ctx,
                                     "higher-order callback \"%s\" may not use ref parameters",
                                     img_ident_name(rctx->ctx, h->name_id));
                        return v_invalid();
                }
        }

        body = img_node(rctx->ctx, h->body_idx);

        if (body->type == IMG_NODE_IDENT &&
            !strcmp(node_ident_name(rctx->ctx, body), "external")) {
                maestro_value **argp = NULL;
                maestro_value *ext_result = NULL;
                const char *fn_name = img_ident_name(rctx->ctx, h->name_id);
                int rc = MAESTRO_ERR_RUNTIME;

                if (argc) {
                        argp = rctx->ctx->alloc(argc * sizeof(*argp));

                        if (!argp)
                                return v_invalid();
                }

                for (i = 0; i < argc; i++)
                        argp[i] = &argv[i];

                for (i = 0; i < rctx->ctx->fn_nr; i++) {
                        if (!strcmp(rctx->ctx->fns[i].name, fn_name)) {
                                rc = rctx->ctx->fns[i].fn(rctx->ctx, argp, argc,
                                                          &ext_result);
                                break;
                        }
                }

                if (argp)
                        rctx->ctx->dealloc(argp);

                if (rc || !ext_result)
                        return v_invalid();

                out = clone_value(rctx->ctx, *ext_result);
                maestro_value_free(rctx->ctx, ext_result);
                return out;
        }

        ms = module_scope_get(rctx, call_mod);

        if (!ms)
                return v_invalid();

        call = scope_new(rctx->ctx, ms->globals);

        if (!call)
                return v_invalid();

        for (i = 0; i < argc; i++) {
                if (scope_set(rctx->ctx, call, h->argv_id[i], deref_value(argv[i])))
                        return v_invalid();
        }

        out = eval_node(rctx, call_mod, call, h->body_idx, &inner);

        if (inner.ctrl != CTRL_OK)
                return v_invalid();

        return out;
}

static int builtin_cmp(maestro_ctx *ctx, const char *op, maestro_value *argv,
                       size_t argc,
                       maestro_value *out) {
        bool ok = true;
        maestro_value lhs = deref_value(argv[0]);
        maestro_value rhs = deref_value(argv[1]);

        if (!strcmp(op, "="))
                ok = value_eq(argv[0], argv[1]);
        else if (!strcmp(op, "!="))
                ok = !value_eq(argv[0], argv[1]);
        else {
                double a, b;

                if (!((lhs.type == MAESTRO_VAL_INT || lhs.type == MAESTRO_VAL_FLOAT) &&
                      (rhs.type == MAESTRO_VAL_INT || rhs.type == MAESTRO_VAL_FLOAT)))
                        return -1;

                a = lhs.type == MAESTRO_VAL_INT ? (double)lhs.v.i : lhs.v.f;
                b = rhs.type == MAESTRO_VAL_INT ? (double)rhs.v.i : rhs.v.f;

                if (!strcmp(op, "<"))
                        ok = a < b;
                else if (!strcmp(op, "<="))
                        ok = a <= b;
                else if (!strcmp(op, ">"))
                        ok = a > b;
                else if (!strcmp(op, ">="))
                        ok = a >= b;
                else
                        return -1;
        }

        *out = v_bool(ok);
        (void)ctx;
        (void)argc;
        return 0;
}

static int builtin_numeric(maestro_ctx *ctx, const char *op,
                           maestro_value *argv, size_t argc,
                           maestro_value *out) {
        size_t i;
        bool all_int = true;
        double acc;

        for (i = 0; i < argc; i++) {
                argv[i] = deref_value(argv[i]);

                if (!(argv[i].type == MAESTRO_VAL_INT || argv[i].type == MAESTRO_VAL_FLOAT))
                        return -1;

                if (argv[i].type != MAESTRO_VAL_INT)
                        all_int = false;
        }

        if (!strcmp(op, "%")) {
                maestro_int_t acci;

                for (i = 0; i < argc; i++) {
                        if (argv[i].type != MAESTRO_VAL_INT)
                                return -1;
                }

                acci = argv[0].v.i;

                for (i = 1; i < argc; i++) {
                        if (!argv[i].v.i)
                                return -1;

                        acci %= argv[i].v.i;
                }

                *out = v_int(acci);
                (void)ctx;
                return 0;
        }

        if (!strcmp(op, "/")) {
                acc = argv[0].type == MAESTRO_VAL_INT ? (double)argv[0].v.i : argv[0].v.f;

                for (i = 1; i < argc; i++) {
                        double b = argv[i].type == MAESTRO_VAL_INT ? (double)argv[i].v.i : argv[i].v.f;

                        if (b == 0.0)
                                return -1;

                        acc /= b;
                }

                *out = v_float((maestro_float_t)acc);
                return 0;
        }

        acc = argv[0].type == MAESTRO_VAL_INT ? (double)argv[0].v.i : argv[0].v.f;

        for (i = 1; i < argc; i++) {
                double b = argv[i].type == MAESTRO_VAL_INT ? (double)argv[i].v.i : argv[i].v.f;

                if (!strcmp(op, "+"))
                        acc += b;
                else if (!strcmp(op, "-"))
                        acc -= b;
                else if (!strcmp(op, "*"))
                        acc *= b;
                else
                        return -1;
        }

        if (all_int)
                *out = v_int((maestro_int_t)acc);
        else
                *out = v_float((maestro_float_t)acc);

        return 0;
}

#define BUILTIN_FAIL(MSG)            \
	do {                         \
		vm_log_builtin_error(rctx, op, MSG); \
		out = v_invalid();       \
		goto out;               \
	} while (0)

static maestro_value eval_call(struct run_ctx *rctx, struct img_mod *mod,
                               struct maestro_scope *scope, struct img_node *node,
                               struct eval_res *eres) {
        struct img_node *head = img_node(rctx->ctx, node->first);
        const char *op = head->type == IMG_NODE_IDENT ? node_ident_name(rctx->ctx,
                         head) : NULL;
        maestro_value *argv = NULL;
        maestro_value callee = v_invalid();
        size_t argc = node->nr > 0 ? node->nr - 1 : 0;
        size_t i;
        maestro_value out = v_invalid();

	if (!op)
		return runtime_error_value(eres);

        if (!strcmp(op, "steps")) {
                for (i = 1; i < node->nr; i++) {
                        out = eval_node(rctx, mod, scope, node->first + i, eres);

                        if (eval_failed(eres, out) || eres->ctrl != CTRL_OK)
                                return out;
                }

                return out;
        }

        if (!strcmp(op, "let")) {
                struct img_node *name = img_node(rctx->ctx, node->first + 1);

                if (node->nr == 3 && name->type == IMG_NODE_IDENT) {
                        out = eval_node(rctx, mod, scope, node->first + 2, eres);

                        if (eval_failed(eres, out) || eres->ctrl != CTRL_OK)
                                return out;

                        scope_set(rctx->ctx, scope, name->str_off, out);
                        return out;
                }

                if (node->nr >= 4 && name->type == IMG_NODE_IDENT) {
                        struct maestro_binding *b = scope_find(scope, name->str_off);
                        maestro_value root;
                        maestro_value *slot;
                        char **path;
                        size_t pn = node->nr - 3;

                        if (!b) {
                                root = v_object(rctx->ctx);

                                if (scope_set(rctx->ctx, scope, name->str_off, root))
                                        return v_invalid();

                                b = scope_find(scope, name->str_off);
                        }

                        root = b->value.type ? b->value : v_object(rctx->ctx);
                        switch (eval_symbol_path(rctx, mod, scope, op,
                                                 node->first + 2, pn, eres, &path)) {
                        case PATH_EVAL_OK:
                                break;
                        case PATH_EVAL_ABORT:
                                return eres->v;
                        case PATH_EVAL_INVALID_SEG:
                                return runtime_error_value(eres);
                        case PATH_EVAL_OOM:
                        default:
                                return v_invalid();
                        }

                        if (ensure_object_path(rctx->ctx, &root, path, pn - 1, &slot)) {
                                free_owned_path(rctx->ctx, path, pn);
                                return v_invalid();
                        }

                        out = eval_node(rctx, mod, scope, node->first + node->nr - 1, eres);

                        if (eval_failed(eres, out) || eres->ctrl != CTRL_OK) {
                                free_owned_path(rctx->ctx, path, pn);
                                return out;
                        }

                        if (slot->type != MAESTRO_VAL_OBJECT) {
                                free_owned_path(rctx->ctx, path, pn);
                                return runtime_error_value(eres);
                        }

                        obj_set(rctx->ctx, slot->v.obj, path[pn - 1], out);
                        b->value = root;
                        free_owned_path(rctx->ctx, path, pn);
                        return out;
                }
        }

        if (!strcmp(op, "set")) {
                struct img_node *name = img_node(rctx->ctx, node->first + 1);
                struct maestro_binding *b;

                if (name->type != IMG_NODE_IDENT)
                        return runtime_error_value(eres);

                b = scope_find(scope, name->str_off);

                if (!b)
                        return runtime_error_value(eres);

                if (node->nr == 3) {
                        out = eval_node(rctx, mod, scope, node->first + 2, eres);

                        if (eval_failed(eres, out) || eres->ctrl != CTRL_OK)
                                return out;

                        if (b->value.type == MAESTRO_VAL_REF) {
                                if (ref_write(rctx->ctx, b->value.v.ref, out))
                                        return runtime_error_value(eres);
                        } else {
                                b->value = out;
                        }

                        return out;
                }

                if (node->nr >= 4) {
                        maestro_value root = b->value;
                        maestro_value *slot;
                        char **path;
                        size_t pn = node->nr - 3;

                        switch (eval_symbol_path(rctx, mod, scope, op,
                                                 node->first + 2, pn, eres, &path)) {
                        case PATH_EVAL_OK:
                                break;
                        case PATH_EVAL_ABORT:
                                return eres->v;
                        case PATH_EVAL_INVALID_SEG:
                                return runtime_error_value(eres);
                        case PATH_EVAL_OOM:
                        default:
                                return runtime_error_value(eres);
                        }

                        if (ensure_object_path(rctx->ctx, &root, path, pn - 1, &slot)) {
                                free_owned_path(rctx->ctx, path, pn);
                                return runtime_error_value(eres);
                        }

                        if (slot->type != MAESTRO_VAL_OBJECT) {
                                free_owned_path(rctx->ctx, path, pn);
                                return runtime_error_value(eres);
                        }

                        out = eval_node(rctx, mod, scope, node->first + node->nr - 1, eres);

                        if (eval_failed(eres, out) || eres->ctrl != CTRL_OK) {
                                free_owned_path(rctx->ctx, path, pn);
                                return out;
                        }

                        obj_set(rctx->ctx, slot->v.obj, path[pn - 1], out);
                        b->value = root;
                        free_owned_path(rctx->ctx, path, pn);
                        return out;
                }
        }

	if (!strcmp(op, "get")) {
                maestro_value cur;
                char **path = NULL;
                size_t pn = node->nr - 2;
                bool bad_mid = false;

		if (node->nr < 3)
			return vm_log_builtin_error(rctx, op, "expected object and at least one path segment"), v_invalid();

                cur = eval_node(rctx, mod, scope, node->first + 1, eres);

		if (!cur.type)
			return vm_log_builtin_error(rctx, op, "object root is invalid"), v_invalid();

                if (eval_failed(eres, cur) || eres->ctrl != CTRL_OK)
                        return cur;

                cur = deref_value(cur);

                switch (eval_symbol_path(rctx, mod, scope, op, node->first + 2,
                                         pn, eres, &path)) {
                case PATH_EVAL_OK:
                        break;
                case PATH_EVAL_ABORT:
                        return eres->v;
                case PATH_EVAL_INVALID_SEG:
                        return runtime_error_value(eres);
                case PATH_EVAL_OOM:
                default:
                        return runtime_error_value(eres);
                }

		for (i = 0; i < pn; i++) {
			if (cur.type != MAESTRO_VAL_OBJECT) {
                                bad_mid = true;
                                break;
                        }

                        cur = obj_get(cur.v.obj, path[i]);

			if (!cur.type)
                                break;
		}

                free_owned_path(rctx->ctx, path, pn);

                if (!cur.type)
                        return v_object(rctx->ctx);

                if (bad_mid)
                        return vm_log_builtin_error(rctx, op,
                                                    "path traversal requires objects at every segment"), v_invalid();

                return cur;
        }

        if (!strcmp(op, "ref")) {
                struct maestro_ref *ref;
                struct maestro_binding *b;
                char **path;
                size_t pn;

                b = scope_find(scope, img_node(rctx->ctx, node->first + 1)->str_off);

                if (!b)
			return vm_log_builtin_error(rctx, op, "reference root binding not found"), v_invalid();

                ref = rctx->ctx->alloc(sizeof(*ref));

                if (!ref)
                        return v_invalid();

                memset(ref, 0, sizeof(*ref));

                if (node->nr == 2) {
                        ref->kind = REF_BINDING;
                        ref->binding = b;
                        return v_ref(ref);
                }

                pn = node->nr - 2;
                switch (eval_symbol_path(rctx, mod, scope, op, node->first + 2,
                                         pn, eres, &path)) {
                case PATH_EVAL_OK:
                        break;
                case PATH_EVAL_ABORT:
                        return eres->v;
                case PATH_EVAL_INVALID_SEG:
                        return runtime_error_value(eres);
                case PATH_EVAL_OOM:
                default:
                        return v_invalid();
                }

                {
                        maestro_value *slot = &b->value;

                        if (lookup_object_path(slot, path, pn, &slot)) {
                                free_owned_path(rctx->ctx, path, pn);
				return vm_log_builtin_error(rctx, op, "object path segment not found"), v_invalid();
                        }
                }
                ref->kind = REF_PATH;
                ref->binding = b;
                ref->path = path;
                ref->path_nr = pn;
                return v_ref(ref);
        }

        if (!strcmp(op, "transition")) {
                if (node->nr >= 2) {
                        if (img_node(rctx->ctx, node->first + 1)->type == IMG_NODE_IDENT &&
                            !strcmp(node_ident_name(rctx->ctx,
                                                    img_node(rctx->ctx, node->first + 1)), "end")) {
                                struct eval_res inner = {0};

                                eres->ctrl = CTRL_END;
                                eres->v = eval_node(rctx, mod, scope, node->first + 2, &inner);

                                if (eval_failed(&inner, eres->v) || inner.ctrl != CTRL_OK)
                                        return runtime_error_value(eres);

                                return eres->v;
                        }

                        {
                                maestro_value target = eval_node(rctx, mod, scope,
                                                                 node->first + 1, eres);

                                if (eval_failed(eres, target))
                                        return target;

                                if (target.type == MAESTRO_VAL_STATE) {
                                        struct eval_res inner = {0};

                                        eres->ctrl = CTRL_TRANSITION;
                                        eres->next_state = target.v.handle;

                                        if (node->nr >= 3)
                                                eres->v = eval_node(rctx, mod, scope,
                                                                    node->first + 2, &inner);
                                        else
                                                eres->v = v_invalid();

                                        if (node->nr >= 3 &&
                                            (eval_failed(&inner, eres->v) ||
                                             inner.ctrl != CTRL_OK))
                                                return runtime_error_value(eres);

                                        return eres->v;
                                }
                        }
                }

                return runtime_error_value(eres);
        }

        if (!strcmp(op, "case")) {
                for (i = 1; i < node->nr; i++) {
                        struct img_node *cl = img_node(rctx->ctx, node->first + i);
                        struct img_node *pred;

                        if (cl->type != IMG_NODE_FORM || cl->nr != 2)
                                continue;

                        pred = img_node(rctx->ctx, cl->first);

                        if (pred->type == IMG_NODE_IDENT &&
                            !strcmp(node_ident_name(rctx->ctx, pred), "default"))
                                return eval_node(rctx, mod, scope, cl->first + 1, eres);

                        out = eval_node(rctx, mod, scope, cl->first, eres);

                        if (eval_failed(eres, out) || eres->ctrl != CTRL_OK)
                                return out;

                        if (value_truthy(out))
                                return eval_node(rctx, mod, scope, cl->first + 1, eres);
                }

                return runtime_error_value(eres);
        }

        if (!strcmp(op, "run")) {
                struct eval_res inner = {0};
                maestro_value prog = eval_node(rctx, mod, scope, node->first + 1, &inner);
                struct img_mod *run_mod;
                struct maestro_handle *start;
                maestro_value run_arg = argc >= 2 ?
                                        eval_node(rctx, mod, scope, node->first + 2, eres) :
                                        v_list(rctx->ctx);
                maestro_value *run_argv = NULL;
                size_t run_argc;

                if (eval_failed(&inner, prog) || inner.ctrl != CTRL_OK)
                        return runtime_error_value(eres);

                if (eval_failed(eres, run_arg) || eres->ctrl != CTRL_OK)
                        return run_arg;

                if (prog.type != MAESTRO_VAL_PROGRAM)
                        return runtime_error_value(eres);

                run_mod = img_mod_by_idx(rctx->ctx, prog.v.handle->module_idx);

                if (!run_mod)
                        return runtime_error_value(eres);

                start = eval_ident(rctx, run_mod, NULL, "start").v.handle;

                if (!start)
                        return runtime_error_value(eres);

                run_argc = run_arg.type == MAESTRO_VAL_LIST ? run_arg.v.list->nr : 1;

                if (run_arg.type == MAESTRO_VAL_LIST && !run_argc)
                        return run_program(rctx, run_mod, NULL, 0);

                run_argv = rctx->ctx->alloc(run_argc * sizeof(*run_argv));

                if (!run_argv)
                        return runtime_error_value(eres);

                for (i = 0; i < run_argc; i++) {
                        maestro_value src = run_arg.type == MAESTRO_VAL_LIST ?
                                            run_arg.v.list->item[i] : run_arg;

                        run_argv[i] = clone_value(rctx->ctx, src);

                        if (run_argv[i].type == MAESTRO_VAL_INVALID &&
                            src.type != MAESTRO_VAL_INVALID) {
                                while (i > 0) {
                                        i--;
                                        maestro_value_reset(rctx->ctx, &run_argv[i]);
                                }

                                rctx->ctx->dealloc(run_argv);
                                return runtime_error_value(eres);
                        }
                }

                out = run_program(rctx, run_mod, run_argv, run_argc);

                while (run_argc > 0) {
                        run_argc--;
                        maestro_value_reset(rctx->ctx, &run_argv[run_argc]);
                }

                rctx->ctx->dealloc(run_argv);
                return out;
        }

        if (!strcmp(op, "import") && node->nr >= 3) {
                struct img_mod *m = find_mod_from_form(rctx, node, 1, node->nr - 1);
                const char *name = node_text(rctx->ctx,
                                             img_node(rctx->ctx, node->first + node->nr - 1));

                if (!m)
                        return runtime_error_value(eres);

                out = eval_ident(rctx, m, NULL, name);
                return out;
        }

        if (!strcmp(op, "import-program") && node->nr >= 2) {
                struct maestro_handle *h = rctx->ctx->alloc(sizeof(*h));
                struct img_mod *prog_mod = find_mod_from_form(rctx, node, 1, node->nr);
                uint32_t start_id = find_ident_id_by_name(rctx->ctx, "start");

                if (!h || !prog_mod || start_id == UINT32_MAX)
                        return runtime_error_value(eres);

                memset(h, 0, sizeof(*h));
                h->kind = HANDLE_PROGRAM;
                h->module_idx = find_mod_idx(rctx->ctx, prog_mod);
                h->name_id = start_id;
                return v_handle(h, MAESTRO_VAL_PROGRAM);
        }

        if (!strcmp(op, "list")) {
                out = v_list(rctx->ctx);

                for (i = 1; i < node->nr; i++) {
                        maestro_value item = eval_node(rctx, mod, scope, node->first + i, eres);

                        if (eval_failed(eres, item) || eres->ctrl != CTRL_OK)
                                return item;

                        list_push(rctx->ctx, out.v.list, item);
                }

                return out;
        }

        if (!strcmp(op, "cons")) {
                maestro_value headv = eval_node(rctx, mod, scope, node->first + 1, eres);
                maestro_value tailv = eval_node(rctx, mod, scope, node->first + 2, eres);

                if (eval_failed(eres, headv) || eval_failed(eres, tailv) ||
                    eres->ctrl != CTRL_OK)
                        return v_invalid();

		if (tailv.type != MAESTRO_VAL_LIST)
			return vm_log_builtin_error(rctx, op, "second argument must be a list"), v_invalid();

                out = v_list(rctx->ctx);
                list_push(rctx->ctx, out.v.list, headv);

                for (i = 0; i < tailv.v.list->nr; i++)
                        list_push(rctx->ctx, out.v.list, tailv.v.list->item[i]);

                return out;
        }

		if (!strcmp(op, "json") && node->nr == 2) {
			maestro_value obj = deref_value(eval_node(rctx, mod, scope,
						node->first + 1, eres));

                        if (eval_failed(eres, obj) || eres->ctrl != CTRL_OK)
                                return v_invalid();
			char *json = json_serialize_object(obj);

			if (!json)
				return vm_log_builtin_error(rctx, op,
					       "argument must be a valid JSON-compatible object"),
				       v_invalid();

                out = v_string(rctx->ctx, json);
                free(json);
                return out;
        }

		if (!strcmp(op, "json-parse") && node->nr == 2) {
			maestro_value s = deref_value(eval_node(rctx, mod, scope,
						node->first + 1, eres));

                        if (eval_failed(eres, s) || eres->ctrl != CTRL_OK)
                                return v_invalid();

			if (s.type != MAESTRO_VAL_STRING)
				return vm_log_builtin_error(rctx, op,
					       "argument must be a JSON object string"),
				       v_invalid();

			if (json_parse_object_text(rctx->ctx, s.v.s, &out))
				return vm_log_builtin_error(rctx, op,
					       "argument must contain a valid JSON object"),
				       v_invalid();

                return out;
        }

        if (!strcmp(op, "and") || !strcmp(op, "or")) {
                bool accum = !strcmp(op, "and");

                for (i = 1; i < node->nr; i++) {
                        out = eval_node(rctx, mod, scope, node->first + i, eres);

                        if (eval_failed(eres, out) || eres->ctrl != CTRL_OK)
                                return out;

                        if (!strcmp(op, "and")) {
                                accum = accum && value_truthy(out);

                                if (!accum)
                                        break;
                        } else {
                                accum = accum || value_truthy(out);

                                if (accum)
                                        break;
                        }
                }

                return v_bool(accum);
        }

        if (!strcmp(op, "not")) {
                out = eval_node(rctx, mod, scope, node->first + 1, eres);
                if (eval_failed(eres, out) || eres->ctrl != CTRL_OK)
                        return out;
                return v_bool(!value_truthy(out));
        }

        if (argc) {
                argv = calloc(argc, sizeof(*argv));

                if (!argv)
                        return v_invalid();

                for (i = 0; i < argc; i++)
                        argv[i] = eval_node(rctx, mod, scope, node->first + 1 + i, eres);

                for (i = 0; i < argc; i++) {
                        if (eval_failed(eres, argv[i]) || eres->ctrl != CTRL_OK)
                                goto out;
                }
        }

        callee = eval_ident(rctx, mod, scope, op);

        if (callee.type == MAESTRO_VAL_BUILTIN)
                op = img_ident_name(rctx->ctx, callee.v.handle->name_id);

	if (!strcmp(op, "+") || !strcmp(op, "-") || !strcmp(op, "*") ||
	    !strcmp(op, "/") || !strcmp(op, "%")) {
		if (!builtin_numeric(rctx->ctx, op, argv, argc, &out))
			goto out;

		BUILTIN_FAIL("invalid numeric arguments");
	}

	if (!strcmp(op, "=") || !strcmp(op, "!=") || !strcmp(op, "<") ||
	    !strcmp(op, "<=") || !strcmp(op, ">") || !strcmp(op, ">=")) {
		if (!builtin_cmp(rctx->ctx, op, argv, argc, &out))
			goto out;

		BUILTIN_FAIL("invalid comparison arguments");
	}

	if (!strcmp(op, "concat")) {
		if (argc < 2)
			BUILTIN_FAIL("expected at least two arguments");

                if (argv[0].type == MAESTRO_VAL_STRING) {
                        char *acc = xstrdup(argv[0].v.s);

                        for (i = 1; i < argc; i++) {
                                char *next;
				argv[i] = deref_value(argv[i]);

				if (argv[i].type != MAESTRO_VAL_STRING) {
					free(acc);
					BUILTIN_FAIL("string concatenation requires string arguments");
				}

                                next = fmt_string("%s%s", acc, argv[i].v.s);
                                free(acc);
                                acc = next;
                        }

                        out = v_string(rctx->ctx, acc);
                        free(acc);
                        goto out;
                }

		if (argv[0].type == MAESTRO_VAL_LIST) {
			out = v_list(rctx->ctx);

                        for (i = 0; i < argc; i++) {
                                size_t j;
                                argv[i] = deref_value(argv[i]);

                                if (argv[i].type != MAESTRO_VAL_LIST)
                                        BUILTIN_FAIL("list concatenation requires list arguments");

                                for (j = 0; j < argv[i].v.list->nr; j++)
                                        list_push(rctx->ctx, out.v.list, argv[i].v.list->item[j]);
                        }

			goto out;
		}

		BUILTIN_FAIL("expected either strings or a leading list");
	}

	if (!strcmp(op, "append")) {
		if (argc < 1)
			BUILTIN_FAIL("expected a leading list");

		argv[0] = deref_value(argv[0]);

		if (argv[0].type != MAESTRO_VAL_LIST)
			BUILTIN_FAIL("expected a leading list");

		out = v_list(rctx->ctx);

		for (i = 0; i < argv[0].v.list->nr; i++)
			list_push(rctx->ctx, out.v.list, argv[0].v.list->item[i]);

		for (i = 1; i < argc; i++)
			list_push(rctx->ctx, out.v.list, deref_value(argv[i]));

		goto out;
	}

	if (!strcmp(op, "probe")) {
		if (argc != 1)
			BUILTIN_FAIL("expected exactly one object argument");

		argv[0] = deref_value(argv[0]);

		if (argv[0].type != MAESTRO_VAL_OBJECT) {
			out = v_list(rctx->ctx);
			goto out;
		}

		out = v_list(rctx->ctx);

		for (i = 0; i < argv[0].v.obj->nr; i++)
			list_push(rctx->ctx, out.v.list,
				  v_symbol(rctx->ctx, argv[0].v.obj->key[i]));

		goto out;
	}

	if (!strcmp(op, "map") || !strcmp(op, "filter") ||
	    !strcmp(op, "any?") || !strcmp(op, "all?")) {
		maestro_value fnv;
		maestro_value listv;
		bool accum = !strcmp(op, "all?");

		if (argc != 2)
			BUILTIN_FAIL("expected exactly two arguments");

		fnv = argv[0];
		listv = deref_value(argv[1]);

		if (fnv.type != MAESTRO_VAL_MACRO)
			BUILTIN_FAIL("callback must be bound to a source macro or external binding");

		if (listv.type != MAESTRO_VAL_LIST)
			BUILTIN_FAIL("expected a list argument");

		if (!strcmp(op, "map") || !strcmp(op, "filter"))
			out = v_list(rctx->ctx);

		for (i = 0; i < listv.v.list->nr; i++) {
			maestro_value cb_argv[1];
			maestro_value item = listv.v.list->item[i];
			maestro_value cb_out;

			cb_argv[0] = item;
			cb_out = invoke_callback_binding(rctx, fnv, cb_argv, 1);

			if (cb_out.type == MAESTRO_VAL_INVALID)
				BUILTIN_FAIL("callback failed");

			if (!strcmp(op, "map")) {
				list_push(rctx->ctx, out.v.list, cb_out);
				continue;
			}

			if (!strcmp(op, "filter")) {
				if (value_truthy(cb_out))
					list_push(rctx->ctx, out.v.list, item);
				continue;
			}

			if (!strcmp(op, "any?")) {
				if (value_truthy(cb_out)) {
					out = v_bool(true);
					goto out;
				}
				continue;
			}

			accum = accum && value_truthy(cb_out);

			if (!accum)
				break;
		}

		if (!strcmp(op, "any?")) {
			out = v_bool(false);
			goto out;
		}

		if (!strcmp(op, "all?")) {
			out = v_bool(accum);
			goto out;
		}

		goto out;
	}

	if (!strcmp(op, "foldl") || !strcmp(op, "foldr")) {
		maestro_value fnv;
		maestro_value initv;
		maestro_value listv;
		maestro_value acc;

		if (argc != 3)
			BUILTIN_FAIL("expected exactly three arguments");

		fnv = argv[0];
		initv = deref_value(argv[1]);
		listv = deref_value(argv[2]);

		if (fnv.type != MAESTRO_VAL_MACRO)
			BUILTIN_FAIL("callback must be bound to a source macro or external binding");

		if (listv.type != MAESTRO_VAL_LIST)
			BUILTIN_FAIL("expected a list argument");

		acc = initv;

		if (!strcmp(op, "foldl")) {
			for (i = 0; i < listv.v.list->nr; i++) {
				maestro_value cb_argv[2];

				cb_argv[0] = acc;
				cb_argv[1] = listv.v.list->item[i];
				acc = invoke_callback_binding(rctx, fnv, cb_argv, 2);

				if (acc.type == MAESTRO_VAL_INVALID)
					BUILTIN_FAIL("callback failed");
			}
		} else {
			for (i = listv.v.list->nr; i > 0; i--) {
				maestro_value cb_argv[2];

				cb_argv[0] = listv.v.list->item[i - 1];
				cb_argv[1] = acc;
				acc = invoke_callback_binding(rctx, fnv, cb_argv, 2);

				if (acc.type == MAESTRO_VAL_INVALID)
					BUILTIN_FAIL("callback failed");
			}
		}

		out = acc;
		goto out;
	}

	if (!strcmp(op, "substr")) {
		if (argc != 3)
			BUILTIN_FAIL("expected exactly three arguments");

		argv[0] = deref_value(argv[0]);
		argv[1] = deref_value(argv[1]);
		argv[2] = deref_value(argv[2]);

                if (argc == 3 && argv[0].type == MAESTRO_VAL_INT &&
                    argv[1].type == MAESTRO_VAL_INT && argv[2].type == MAESTRO_VAL_STRING) {
                        size_t l = (size_t)argv[0].v.i;
                        size_t r = (size_t)argv[1].v.i;
                        size_t n = strlen(argv[2].v.s);
                        char *s;

			if (l > r || r > n)
				BUILTIN_FAIL("substring range is out of bounds");

			s = malloc(r - l + 1);

			if (!s)
				BUILTIN_FAIL("unable to allocate substring");

                        memcpy(s, argv[2].v.s + l, r - l);
                        s[r - l] = 0;
                        out = v_string(rctx->ctx, s);
			free(s);
			goto out;
		}

		BUILTIN_FAIL("expected (substr int int string)");
	}

	if (!strcmp(op, "to-string")) {
		char *s;

		if (argc != 1)
			BUILTIN_FAIL("expected exactly one argument");

		argv[0] = deref_value(argv[0]);

                if (argc == 1 &&
                    (argv[0].type == MAESTRO_VAL_INT || argv[0].type == MAESTRO_VAL_FLOAT ||
                     argv[0].type == MAESTRO_VAL_SYMBOL)) {
                        s = maestro_value_stringify(rctx->ctx, argv[0]);
			out = v_string(rctx->ctx, s);
			free(s);
			goto out;
		}

		BUILTIN_FAIL("expected an int, float, or symbol");
	}

	if (!strcmp(op, "floor") || !strcmp(op, "ceil")) {
		if (argc != 1)
			BUILTIN_FAIL("expected exactly one argument");

		argv[0] = deref_value(argv[0]);

                if (argc == 1) {
                        if (argv[0].type == MAESTRO_VAL_INT) {
                                out = argv[0];
                                goto out;
                        }

			if (argv[0].type == MAESTRO_VAL_FLOAT) {
				out = v_int(!strcmp(op, "floor") ?
					    (maestro_int_t)floorf(argv[0].v.f) :
					    (maestro_int_t)ceilf(argv[0].v.f));
				goto out;
			}
		}

		BUILTIN_FAIL("expected an int or float");
	}

	if (!strcmp(op, "log") || !strcmp(op, "print")) {
                char *s;
                int rc;
                argv[0] = deref_value(argv[0]);

		if (argc != 1 || argv[0].type != MAESTRO_VAL_STRING)
			BUILTIN_FAIL("expected exactly one string argument");

		s = xstrdup(argv[0].v.s);

		if (!s)
			BUILTIN_FAIL("unable to allocate output buffer");

                rc = !strcmp(op, "log") ? rctx->ctx->log(rctx->ctx, s) :
                     rctx->ctx->print(rctx->ctx, s);
                free(s);
		out = v_int(rc);
		goto out;
	}

        if (!strcmp(op, "empty?") && argc == 1) {
                bool e = false;

                if (argv[0].type == MAESTRO_VAL_STRING)
                        e = argv[0].v.s[0] == 0;
                else if (argv[0].type == MAESTRO_VAL_LIST)
                        e = argv[0].v.list->nr == 0;
                else if (argv[0].type == MAESTRO_VAL_OBJECT)
                        e = argv[0].v.obj->nr == 0;

                out = v_bool(e);
                goto out;
        }

        if (!strcmp(op, "true?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_BOOL && argv[0].v.b);
                goto out;
        }

        if (!strcmp(op, "false?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_BOOL && !argv[0].v.b);
                goto out;
        }

        if (!strcmp(op, "number?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_INT ||
                             argv[0].type == MAESTRO_VAL_FLOAT);
                goto out;
        }

        if (!strcmp(op, "integer?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_INT);
                goto out;
        }

        if (!strcmp(op, "float?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_FLOAT);
                goto out;
        }

        if (!strcmp(op, "string?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_STRING);
                goto out;
        }

        if (!strcmp(op, "list?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_LIST);
                goto out;
        }

        if (!strcmp(op, "object?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_OBJECT);
                goto out;
        }

        if (!strcmp(op, "symbol?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_SYMBOL);
                goto out;
        }

        if (!strcmp(op, "boolean?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_BOOL);
                goto out;
        }

        if (!strcmp(op, "ref?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_REF ||
                             argv[0].type == MAESTRO_VAL_STATE ||
                             argv[0].type == MAESTRO_VAL_MACRO);
                goto out;
        }

        if (!strcmp(op, "state?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_STATE);
                goto out;
        }

        if (!strcmp(op, "macro?") && argc == 1) {
                out = v_bool(argv[0].type == MAESTRO_VAL_MACRO);
                goto out;
        }

        if (!strcmp(op, "ref=?") && argc == 2) {
                bool same = false;

                if ((argv[0].type == MAESTRO_VAL_REF || argv[0].type == MAESTRO_VAL_STATE ||
                     argv[0].type == MAESTRO_VAL_MACRO) &&
                    argv[0].type == argv[1].type) {
                        if (argv[0].type == MAESTRO_VAL_REF)
                                same = argv[0].v.ref == argv[1].v.ref;
                        else
                                same = argv[0].v.handle->module_idx ==
                                       argv[1].v.handle->module_idx &&
                                       argv[0].v.handle->name_id ==
                                       argv[1].v.handle->name_id;
                }

                out = v_bool(same);
                goto out;
        }

        if (callee.type == MAESTRO_VAL_BUILTIN)
                BUILTIN_FAIL("invalid arguments or arity");

        {
                maestro_value fn = callee.type ? callee : eval_ident(rctx, mod, scope, op);

                if (fn.type == MAESTRO_VAL_MACRO) {
                        struct maestro_handle *h = fn.v.handle;
                        struct img_mod *call_mod = img_mod_by_idx(rctx->ctx, h->module_idx);
                        struct module_scope *ms = call_mod ? module_scope_get(rctx, call_mod) : NULL;
                        struct maestro_scope *call = ms ? scope_new(rctx->ctx, ms->globals) : NULL;
                        struct img_node *body = img_node(rctx->ctx, h->body_idx);

                        if (!call)
                                goto out;

                        for (i = 0; i < h->argc && i < argc; i++) {
                                if (h->arg_ref[i]) {
                                        struct maestro_ref *ref = NULL;
                                        struct img_node *argn = img_node(rctx->ctx, node->first + 1 + i);

                                        if (argv[i].type == MAESTRO_VAL_REF ||
                                            argv[i].type == MAESTRO_VAL_STATE ||
                                            argv[i].type == MAESTRO_VAL_MACRO) {
                                                scope_set(rctx->ctx, call, h->argv_id[i], argv[i]);
                                                continue;
                                        }

                                        if (argn->type != IMG_NODE_IDENT)
                                                goto out;

                                        {
                                                struct maestro_binding *rb = scope_find(scope, argn->str_off);

                                                if (!rb) {
                                                        struct module_scope *cur_ms = module_scope_get(rctx, mod);
                                                        rb = cur_ms ? scope_find_here(cur_ms->globals,
                                                                                      argn->str_off) : NULL;
                                                }

                                                if (!rb)
                                                        goto out;

                                                ref = rctx->ctx->alloc(sizeof(*ref));

                                                if (!ref)
                                                        goto out;

                                                memset(ref, 0, sizeof(*ref));
                                                ref->kind = REF_BINDING;
                                                ref->binding = rb;
                                                scope_set(rctx->ctx, call, h->argv_id[i], v_ref(ref));
                                        }
                                } else {
                                        scope_set(rctx->ctx, call, h->argv_id[i], deref_value(argv[i]));
                                }
                        }

                        if (body->type == IMG_NODE_IDENT &&
                            !strcmp(node_ident_name(rctx->ctx, body), "external")) {
                                size_t t;
                                const char *fn_name = img_ident_name(rctx->ctx, h->name_id);
                                maestro_value **argp = NULL;
                                maestro_value *ext_result = NULL;
                                int rc = MAESTRO_ERR_RUNTIME;

                                if (argc) {
                                        argp = rctx->ctx->alloc(argc * sizeof(*argp));

                                        if (!argp)
                                                goto out;
                                }

                                for (t = 0; t < argc; t++)
                                        argp[t] = &argv[t];

                                for (t = 0; t < rctx->ctx->fn_nr; t++) {
                                        if (!strcmp(rctx->ctx->fns[t].name, fn_name)) {
                                                rc = rctx->ctx->fns[t].fn(rctx->ctx, argp, argc,
                                                                          &ext_result);

                                                if (!rc && ext_result)
                                                        out = clone_value(rctx->ctx, *ext_result);

                                                if (ext_result)
                                                        maestro_value_free(rctx->ctx, ext_result);

                                                rctx->ctx->dealloc(argp);
                                                goto out;
                                        }
                                }

                                rctx->ctx->dealloc(argp);
                                goto out;
                        }

                        out = eval_node(rctx, call_mod, call, h->body_idx, eres);
                        goto out;
                }
        }

out:
	free(argv);
	return out;
}

#undef BUILTIN_FAIL

static maestro_value eval_node(struct run_ctx *rctx, struct img_mod *mod,
                               struct maestro_scope *scope, uint32_t idx,
                               struct eval_res *eres) {
        struct img_node *node = img_node(rctx->ctx, idx);
        maestro_value out;

        switch (node->type) {
        case IMG_NODE_INT:
                out = v_int(node->i);
                break;

        case IMG_NODE_FLOAT:
                out = v_float(node->f);
                break;

        case IMG_NODE_STRING:
                out = v_string_borrow(rctx->ctx, img_str(rctx->ctx, node->str_off));
                break;

        case IMG_NODE_IDENT:
                out = eval_ident(rctx, mod, scope, node_ident_name(rctx->ctx, node));
                break;

        case IMG_NODE_SYMBOL:
                out = v_symbol_borrow(rctx->ctx, node_ident_name(rctx->ctx, node));
                break;

        case IMG_NODE_JSON:
                out = eval_json(rctx, mod, scope, node, eres);
                break;

        case IMG_NODE_FORM:
                out = eval_call(rctx, mod, scope, node, eres);
                break;

        default:
                out = v_invalid();
                break;
        }

        if (out.type == MAESTRO_VAL_INVALID && eres && eres->ctrl == CTRL_OK)
                eres->ctrl = CTRL_ERROR;

        return out;
}

static maestro_value run_program(struct run_ctx *parent, struct img_mod *mod,
                                 maestro_value *args, size_t argc) {
        struct run_ctx sub = {0};
        maestro_value startv;

        sub.ctx = parent->ctx;
        sub.mods_cache = parent->mods_cache;
        sub.last_state = v_object(parent->ctx);
        startv = eval_ident(&sub, mod, NULL, "start");

        if (startv.type != MAESTRO_VAL_STATE)
                return v_invalid();

        obj_set(parent->ctx, sub.last_state.v.obj, "state", startv);
        return run_state(&sub, mod, startv.v.handle, args, argc);
}

static struct maestro_scope *bind_callable_args(struct run_ctx *rctx,
                struct img_mod *mod,
                struct maestro_handle *callable,
                maestro_value *args, size_t argc) {
        struct module_scope *ms = module_scope_get(rctx, mod);
        struct maestro_scope *scope;
        size_t i;

        if (!ms)
                return NULL;

        scope = scope_new(rctx->ctx, ms->globals);

        if (!scope)
                return NULL;

        for (i = 0; i < callable->argc; i++) {
                maestro_value v = i < argc ? args[i] : v_invalid();

                if (scope_set(rctx->ctx, scope, callable->argv_id[i], v))
                        return NULL;
        }

        return scope;
}

static maestro_value run_state(struct run_ctx *rctx, struct img_mod *mod,
                               struct maestro_handle *state, maestro_value *args,
                               size_t argc) {
        struct maestro_scope *scope = bind_callable_args(rctx, mod, state, args, argc);
        struct eval_res eres = {0};
        maestro_value v;

        if (!scope)
                return v_invalid();

        for (;;) {
                eres.ctrl = CTRL_OK;
                v = eval_node(rctx, mod, scope, state->body_idx, &eres);

                if (eres.ctrl == CTRL_ERROR ||
                    (v.type == MAESTRO_VAL_INVALID &&
                     eres.ctrl != CTRL_TRANSITION))
                        return v_invalid();

                {
                        maestro_value ls = v_object(rctx->ctx);
                        obj_set(rctx->ctx, ls.v.obj, "state",
                                v_handle(state, MAESTRO_VAL_STATE));
                        obj_set(rctx->ctx, ls.v.obj, "val", v);
                        rctx->last_state = ls;
                }

                if (eres.ctrl == CTRL_END)
                        return eres.v;

                if (eres.ctrl == CTRL_TRANSITION) {
                        mod = img_mod_by_idx(rctx->ctx, eres.next_state->module_idx);
                        state = eres.next_state;
                        scope = bind_callable_args(rctx, mod, state, NULL, 0);
                        continue;
                }
        }
}

int maestro_run(maestro_ctx *ctx, const char *module_path, maestro_value **args,
                size_t argc,
                maestro_value **result) {
        struct img_mod *mod;
        struct run_ctx rctx;
        maestro_value start;
        maestro_value raw = v_invalid();
        maestro_value *argv = NULL;
        size_t i;
        int ret = 0;

	if (!ctx || !result)
		return MAESTRO_ERR_RUNTIME;

	*result = NULL;
	mod = find_mod(ctx, module_path);

	if (!mod) {
		vm_log_error(ctx, "module \"%s\" not found",
			     module_path ? module_path : "");
		return MAESTRO_ERR_RUNTIME;
	}

	if (argc) {
		argv = ctx->alloc(argc * sizeof(*argv));

		if (!argv) {
			vm_log_error(ctx, "unable to allocate startup arguments for module \"%s\"",
				     module_path);
			return MAESTRO_ERR_NOMEM;
		}

		for (i = 0; i < argc; i++) {
			if (!args || !args[i]) {
				vm_log_error(ctx,
					     "invalid startup argument list for module \"%s\"",
					     module_path);
				ret = MAESTRO_ERR_RUNTIME;
				goto out;
			}

                        argv[i] = clone_value(ctx, *args[i]);
                }
        }

        memset(&rctx, 0, sizeof(rctx));
        rctx.ctx = ctx;
        rctx.last_state = v_object(ctx);
        obj_set(ctx, rctx.last_state.v.obj, "state",
                eval_ident(&rctx, mod, NULL, "start"));
	start = eval_ident(&rctx, mod, NULL, "start");

	if (start.type != MAESTRO_VAL_STATE) {
		vm_log_error(ctx, "module \"%s\" has no runnable start state",
			     module_path);
		ret = MAESTRO_ERR_RUNTIME;
		goto out;
	}

        *result = ctx->alloc(sizeof(**result));

        if (!*result) {
                ret = MAESTRO_ERR_NOMEM;
                goto out;
        }

	raw = run_state(&rctx, mod, start.v.handle, argv, argc);

	if (raw.type == MAESTRO_VAL_INVALID) {
		vm_log_error(ctx, "module \"%s\" ended with an invalid runtime value",
			     module_path);
		ret = MAESTRO_ERR_RUNTIME;
		goto out;
	}

        **result = clone_value(ctx, raw);

        if ((*result)->type == MAESTRO_VAL_INVALID && raw.type != MAESTRO_VAL_INVALID) {
                ctx->dealloc(*result);
                *result = NULL;
                ret = MAESTRO_ERR_NOMEM;
                goto out;
        }

out:

        if (argv) {
                for (i = 0; i < argc; i++)
                        maestro_value_reset(ctx, &argv[i]);

                ctx->dealloc(argv);
        }

        maestro_value_reset(ctx, &rctx.last_state);
        return ret;
}

maestro_value *maestro_value_new_invalid(maestro_ctx *ctx) {
        maestro_value *v;

        if (!ctx)
                return NULL;

        v = ctx->alloc(sizeof(*v));

        if (!v)
                return NULL;

        *v = v_invalid();
        return v;
}

maestro_value *maestro_value_new_int(maestro_ctx *ctx, maestro_int_t val) {
        maestro_value *v = maestro_value_new_invalid(ctx);

        if (v)
                *v = v_int(val);

        return v;
}

maestro_value *maestro_value_new_float(maestro_ctx *ctx, maestro_float_t val) {
        maestro_value *v = maestro_value_new_invalid(ctx);

        if (v)
                *v = v_float(val);

        return v;
}

maestro_value *maestro_value_new_bool(maestro_ctx *ctx, bool val) {
        maestro_value *v = maestro_value_new_invalid(ctx);

        if (v)
                *v = v_bool(val);

        return v;
}

maestro_value *maestro_value_new_string(maestro_ctx *ctx, const char *s) {
        maestro_value *v = maestro_value_new_invalid(ctx);

        if (v)
                *v = v_string(ctx, s);

        return v;
}

maestro_value *maestro_value_new_symbol(maestro_ctx *ctx, const char *s) {
        maestro_value *v = maestro_value_new_invalid(ctx);

        if (!ctx || !v)
                return v;

        v->type = MAESTRO_VAL_SYMBOL;
        v->v.s = ctx->alloc(strlen(s ? s : "") + 1);

        if (v->v.s)
                strcpy(v->v.s, s ? s : "");

        return v;
}

maestro_value *maestro_value_new_list(maestro_ctx *ctx) {
        maestro_value *v = maestro_value_new_invalid(ctx);

        if (v)
                *v = v_list(ctx);

        return v;
}

maestro_value *maestro_value_new_json(maestro_ctx *ctx,
                                      const char *json_snippet) {
        maestro_value *v = maestro_value_new_invalid(ctx);

        if (!v)
                return NULL;

        if (json_parse_object_text(ctx, json_snippet ? json_snippet : "", v)) {
                maestro_value_reset(ctx, v);
                return NULL;
        }

        return v;
}

void maestro_value_free(maestro_ctx *ctx, maestro_value *v) {
        if (!ctx || !v)
                return;

        maestro_value_reset(ctx, v);
        ctx->dealloc(v);
}

int maestro_list_push(maestro_ctx *ctx, maestro_value *list, maestro_value *v) {
        if (!ctx || !list || !v || list->type != MAESTRO_VAL_LIST)
                return MAESTRO_ERR_RUNTIME;

        return list_push(ctx, list->v.list, clone_value(ctx,
                         *v)) ? MAESTRO_ERR_NOMEM : 0;
}

int maestro_value_type(const maestro_value *v) {
        return v ? (int)v->type : MAESTRO_VAL_INVALID;
}

int maestro_value_as_int(const maestro_value *v, maestro_int_t *out) {
        if (!v || !out || v->type != MAESTRO_VAL_INT)
                return MAESTRO_ERR_RUNTIME;

        *out = v->v.i;
        return 0;
}

int maestro_value_as_float(const maestro_value *v, maestro_float_t *out) {
        if (!v || !out || v->type != MAESTRO_VAL_FLOAT)
                return MAESTRO_ERR_RUNTIME;

        *out = v->v.f;
        return 0;
}

int maestro_value_as_bool(const maestro_value *v, bool *out) {
        if (!v || !out || v->type != MAESTRO_VAL_BOOL)
                return MAESTRO_ERR_RUNTIME;

        *out = v->v.b;
        return 0;
}

const char *maestro_value_as_string(const maestro_value *v) {
        if (!v || v->type != MAESTRO_VAL_STRING)
                return NULL;

        return v->v.s;
}

const char *maestro_value_as_symbol(const maestro_value *v) {
        if (!v || v->type != MAESTRO_VAL_SYMBOL)
                return NULL;

        return v->v.s;
}

size_t maestro_value_list_len(const maestro_value *v) {
        if (!v || v->type != MAESTRO_VAL_LIST || !v->v.list)
                return 0;

        return v->v.list->nr;
}

maestro_value *maestro_value_list_get(const maestro_value *v, size_t idx) {
        if (!v || v->type != MAESTRO_VAL_LIST || !v->v.list || idx >= v->v.list->nr)
                return NULL;

        return &v->v.list->item[idx];
}
