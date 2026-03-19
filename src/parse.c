#include "maestro_int.h"

static maestro_ast_node *ast_new(uint32_t type, uint32_t line, uint32_t col) {
        maestro_ast_node *node = calloc(1, sizeof(*node));

        if (!node)
                return NULL;

        node->type = type;
        node->line = line;
        node->col = col;
        return node;
}

static int ast_add_child(maestro_ast_node *node, maestro_ast_node *child) {
        if (node->child_nr == node->child_cap) {
                uint32_t ncap = node->child_cap ? node->child_cap * 2 : 8;
                maestro_ast_node **nv = realloc(node->child,
                                                ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                node->child = nv;
                node->child_cap = ncap;
        }

        node->child[node->child_nr++] = child;
        return 0;
}

static int ast_add_kv(maestro_ast_node *node, const char *key,
                      maestro_ast_node *val) {
        if (node->kv_nr == node->kv_cap) {
                uint32_t ncap = node->kv_cap ? node->kv_cap * 2 : 8;
                maestro_ast_kv *nv = realloc(node->kv, ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                node->kv = nv;
                node->kv_cap = ncap;
        }

        node->kv[node->kv_nr].key = xstrdup(key);

        if (!node->kv[node->kv_nr].key)
                return -1;

        node->kv[node->kv_nr].value = val;
        node->kv_nr++;
        return 0;
}

static bool is_delim(int c) {
        return c == EOF || isspace(c) || c == '(' || c == ')' || c == '{' ||
               c == '}' || c == ':' || c == ',' || c == ';';
}

static int txt_peek(struct text *t) {
        if (t->pos >= t->len)
                return EOF;

        return (unsigned char)t->buf[t->pos];
}

static int txt_get(struct text *t) {
        int c;

        if (t->pos >= t->len)
                return EOF;

        c = (unsigned char)t->buf[t->pos++];

        if (c == '\n') {
                t->line++;
                t->col = 1;
        } else {
                t->col++;
        }

        return c;
}

static void skip_ws(struct text *t) {
        int c;

        for (;;) {
                c = txt_peek(t);

                if (c == ';') {
                        while ((c = txt_get(t)) != EOF && c != '\n')
                                ;

                        continue;
                }

                if (!isspace(c))
                        break;

                txt_get(t);
        }
}

static char *parse_token(struct text *t) {
        size_t start = t->pos;
        size_t len;
        char *s;

        while (!is_delim(txt_peek(t)))
                txt_get(t);

        len = t->pos - start;
        s = malloc(len + 1);

        if (!s)
                return NULL;

        memcpy(s, t->buf + start, len);
        s[len] = 0;
        return s;
}

static char *parse_string_raw(struct text *t, FILE *err) {
        char *buf = NULL;
        size_t len = 0, cap = 0;
        int c;

        if (txt_get(t) != '"')
                return NULL;

        while ((c = txt_get(t)) != EOF) {
                char out;

                if (c == '"')
                        break;

                if (c == '\\') {
                        c = txt_get(t);

                        if (c == EOF)
                                break;

                        switch (c) {
                        case 'n':
                                out = '\n';
                                break;

                        case 't':
                                out = '\t';
                                break;

                        case '"':
                                out = '"';
                                break;

                        case '\\':
                                out = '\\';
                                break;

                        default:
                                out = (char)c;
                                break;
                        }
                } else {
                        out = (char)c;
                }

                if (len + 2 > cap) {
                        size_t ncap = cap ? cap * 2 : 32;
                        char *nb;

                        while (ncap < len + 2)
                                ncap *= 2;

                        nb = realloc(buf, ncap);

                        if (!nb) {
                                free(buf);
                                return NULL;
                        }

                        buf = nb;
                        cap = ncap;
                }

                buf[len++] = out;
        }

        if (c != '"') {
                diagf(err, "unterminated string literal\n");
                free(buf);
                return NULL;
        }

        if (!buf) {
                buf = malloc(1);

                if (!buf)
                        return NULL;
        }

        buf[len] = 0;
        return buf;
}

static maestro_ast_node *parse_expr(struct text *t, FILE *err);

static maestro_ast_node *parse_json(struct text *t, FILE *err) {
        maestro_ast_node *node;
        uint32_t line = t->line;
        uint32_t col = t->col;

        if (txt_get(t) != '{')
                return NULL;

        node = ast_new(MAESTRO_AST_JSON, line, col);

        if (!node)
                return NULL;

        skip_ws(t);

        if (txt_peek(t) == '}') {
                txt_get(t);
                return node;
        }

        for (;;) {
                char *key;
                maestro_ast_node *val;

                skip_ws(t);

                if (txt_peek(t) != '"') {
                        diagf(err, "json object keys must be strings\n");
                        return NULL;
                }

                key = parse_string_raw(t, err);

                if (!key)
                        return NULL;

                skip_ws(t);

                if (txt_get(t) != ':') {
                        diagf(err, "expected ':' after json key\n");
                        free(key);
                        return NULL;
                }

                skip_ws(t);
                val = parse_expr(t, err);

                if (!val) {
                        free(key);
                        return NULL;
                }

                if (ast_add_kv(node, key, val)) {
                        free(key);
                        return NULL;
                }

                free(key);
                skip_ws(t);

                if (txt_peek(t) == '}') {
                        txt_get(t);
                        break;
                }

                if (txt_get(t) != ',') {
                        diagf(err, "expected ',' in json object\n");
                        return NULL;
                }
        }

        return node;
}

static maestro_ast_node *parse_list_form(struct text *t, FILE *err) {
        maestro_ast_node *node;
        uint32_t line = t->line;
        uint32_t col = t->col;

        if (txt_get(t) != '(')
                return NULL;

        node = ast_new(MAESTRO_AST_FORM, line, col);

        if (!node)
                return NULL;

        for (;;) {
                maestro_ast_node *child;

                skip_ws(t);

                if (txt_peek(t) == ')') {
                        txt_get(t);
                        break;
                }

                if (txt_peek(t) == EOF) {
                        diagf(err, "unterminated form\n");
                        return NULL;
                }

                child = parse_expr(t, err);

                if (!child)
                        return NULL;

                if (ast_add_child(node, child))
                        return NULL;
        }

        return node;
}

static bool token_is_int(const char *s) {
        char *end;

        if (!*s)
                return false;

        if (!strcmp(s, "-") || !strcmp(s, "+"))
                return false;

        strtoll(s, &end, 10);
        return *end == 0;
}

static bool token_is_float(const char *s) {
        char *end;

        if (!strchr(s, '.'))
                return false;

        strtof(s, &end);
        return *end == 0;
}

static maestro_ast_node *parse_expr(struct text *t, FILE *err) {
        int c;
        uint32_t line, col;
        char *tok;
        maestro_ast_node *node;

        skip_ws(t);
        line = t->line;
        col = t->col;
        c = txt_peek(t);

        if (c == '(')
                return parse_list_form(t, err);

        if (c == '{')
                return parse_json(t, err);

        if (c == EOF) {
                diagf(err, "unexpected end of input\n");
                return NULL;
        }

        if (c == ')' || c == '}' || c == ':' || c == ',') {
                diagf(err, "unexpected '%c'\n", c);
                return NULL;
        }

        if (c == '"') {
                char *s = parse_string_raw(t, err);

                if (!s)
                        return NULL;

                node = ast_new(MAESTRO_AST_STRING, line, col);

                if (!node) {
                        free(s);
                        return NULL;
                }

                node->text = s;
                return node;
        }

        if (c == '\'') {
                txt_get(t);

                if (is_delim(txt_peek(t))) {
                        diagf(err, "expected symbol after quote\n");
                        return NULL;
                }

                tok = parse_token(t);

                if (!tok)
                        return NULL;

                node = ast_new(MAESTRO_AST_SYMBOL, line, col);

                if (!node) {
                        free(tok);
                        return NULL;
                }

                node->text = tok;
                return node;
        }

        tok = parse_token(t);

        if (!tok)
                return NULL;

        if (token_is_int(tok)) {
                node = ast_new(MAESTRO_AST_INT, line, col);

                if (!node) {
                        free(tok);
                        return NULL;
                }

                node->i = strtoll(tok, NULL, 10);
                free(tok);
                return node;
        }

        if (token_is_float(tok)) {
                node = ast_new(MAESTRO_AST_FLOAT, line, col);

                if (!node) {
                        free(tok);
                        return NULL;
                }

                node->f = strtof(tok, NULL);
                free(tok);
                return node;
        }

        node = ast_new(MAESTRO_AST_IDENT, line, col);

        if (!node) {
                free(tok);
                return NULL;
        }

        node->text = tok;
        return node;
}

static char *slurp_path(const char *path, FILE *err) {
        FILE *fp;
        long end;
        char *buf;

        fp = fopen(path, "rb");

        if (!fp) {
                diagf(err, "open %s: %s\n", path, strerror(errno));
                return NULL;
        }

        if (fseek(fp, 0, SEEK_END)) {
                fclose(fp);
                return NULL;
        }

        end = ftell(fp);

        if (end < 0) {
                fclose(fp);
                return NULL;
        }

        if (fseek(fp, 0, SEEK_SET)) {
                fclose(fp);
                return NULL;
        }

        buf = malloc((size_t)end + 1);

        if (!buf) {
                fclose(fp);
                return NULL;
        }

        if (fread(buf, 1, (size_t)end, fp) != (size_t)end) {
                fclose(fp);
                free(buf);
                return NULL;
        }

        fclose(fp);
        buf[end] = 0;
        return buf;
}

static maestro_ast_node *parse_root(const char *buf, FILE *err) {
        struct text t = {
                .buf = buf,
                .len = strlen(buf),
                .pos = 0,
                .line = 1,
                .col = 1,
        };
        maestro_ast_node *root = ast_new(MAESTRO_AST_FORM, 1, 1);

        if (!root)
                return NULL;

        while (1) {
                maestro_ast_node *child;

                skip_ws(&t);

                if (txt_peek(&t) == EOF)
                        break;

                child = parse_expr(&t, err);

                if (!child)
                        return NULL;

                if (ast_add_child(root, child))
                        return NULL;
        }

        return root;
}

static const char *node_ident(maestro_ast_node *n) {
        if (!n || n->type != MAESTRO_AST_IDENT)
                return NULL;

        return n->text;
}

static bool is_ident_start(int c) {
        return isalpha(c) || c == '_';
}

static bool is_ident_rest(int c) {
        return isalnum(c) || c == '_' || c == '.' || c == '-' || c == '?' ||
               c == '*' || c == '+' || c == '/' || c == '=' || c == '!' ||
               c == '<' || c == '>';
}

static bool is_valid_identifier(const char *s) {
        size_t i;

        if (!s || !s[0])
                return false;

        if (!is_ident_start((unsigned char)s[0]))
                return false;

        for (i = 1; s[i]; i++) {
                if (!is_ident_rest((unsigned char)s[i]))
                        return false;
        }

        return true;
}

static int extract_module_path(maestro_ast_node *root, FILE *err,
                               char ***seg_out,
                               uint32_t *nr_out) {
        uint32_t found = 0;
        uint32_t i;

        *seg_out = NULL;
        *nr_out = 0;

        for (i = 0; i < root->child_nr; i++) {
                maestro_ast_node *f = root->child[i];

                if (f->type != MAESTRO_AST_FORM || f->child_nr < 2)
                        continue;

                if (node_ident(f->child[0]) && !strcmp(node_ident(f->child[0]), "module")) {
                        char **segv;
                        uint32_t j;

                        found++;

                        if (found > 1) {
                                diagf(err, "multiple module statements are not allowed\n");
                                return -1;
                        }

                        for (j = 1; j < f->child_nr; j++) {
                                if ((f->child[j]->type != MAESTRO_AST_IDENT &&
                                     f->child[j]->type != MAESTRO_AST_SYMBOL) ||
                                    !is_valid_identifier(f->child[j]->text)) {
                                        diagf(err, "invalid module path segment\n");
                                        return -1;
                                }
                        }

                        segv = calloc(f->child_nr - 1, sizeof(*segv));

                        if (!segv)
                                return -1;

                        for (j = 1; j < f->child_nr; j++) {
                                segv[j - 1] = xstrdup(f->child[j]->text);

                                if (!segv[j - 1]) {
                                        while (j > 1) {
                                                j--;
                                                free(segv[j - 1]);
                                        }

                                        free(segv);
                                        return -1;
                                }
                        }

                        *seg_out = segv;
                        *nr_out = f->child_nr - 1;
                        return 0;
                }
        }

        return -1;
}

static void maestro_add_ast(maestro_asts *dest, maestro_ast *src) {
        src->next = NULL;

        if (!dest->head)
                dest->head = src;
        else
                dest->tail->next = src;

        dest->tail = src;
        dest->nr++;
}

static void ast_node_free(maestro_ast_node *node) {
        uint32_t i;

        if (!node)
                return;

        for (i = 0; i < node->child_nr; i++)
                ast_node_free(node->child[i]);

        for (i = 0; i < node->kv_nr; i++) {
                free(node->kv[i].key);
                ast_node_free(node->kv[i].value);
        }

        free(node->child);
        free(node->kv);
        free(node->text);
        free(node);
}

maestro_asts *maestro_asts_new(void) {
        return calloc(1, sizeof(maestro_asts));
}

void maestro_asts_free(maestro_asts *asts) {
        maestro_ast *ast;
        maestro_ast *next;

        if (!asts)
                return;

        for (ast = asts->head; ast; ast = next) {
                uint32_t i;

                next = ast->next;
                free(ast->src);

                for (i = 0; i < ast->module_nr; i++)
                        free(ast->module_seg[i]);

                free(ast->module_seg);
                ast_node_free(ast->root);
                free(ast);
        }

        free(asts);
}

int maestro_parse_file(maestro_asts *dest, FILE *err, const char *src) {
        char *buf;
        maestro_ast_node *root;
        maestro_ast *ast;

        if (!dest)
                return MAESTRO_ERR_PARSE;

        ast = calloc(1, sizeof(*ast));

        if (!ast)
                return MAESTRO_ERR_NOMEM;

        buf = slurp_path(src, err);

        if (!buf)
                return free(ast), MAESTRO_ERR_PARSE;

        root = parse_root(buf, err);

        if (!root) {
                free(buf);
                free(ast);
                return MAESTRO_ERR_PARSE;
        }

        ast->src = xstrdup(src);
        ast->root = root;

        if (extract_module_path(root, err, &ast->module_seg, &ast->module_nr)) {
                diagf(err, "%s: missing module declaration\n", src);
                free(buf);
                ast_node_free(root);
                free(ast->src);
                free(ast);
                return MAESTRO_ERR_PARSE;
        }


        free(buf);
        maestro_add_ast(dest, ast);
        return 0;
}

int maestro_parse_list(maestro_asts *dest, FILE *err, const char **srcs,
                       int src_cnt) {
        int i;

        if (!dest)
                return MAESTRO_ERR_PARSE;

        for (i = 0; i < src_cnt; i++) {
                int ret;

                ret = maestro_parse_file(dest, err, srcs[i]);

                if (ret)
                        return ret;
        }

        return 0;
}
