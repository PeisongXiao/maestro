#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "maestro/maestro.h"

struct text_buf {
        char *buf;
        size_t len;
        size_t cap;
};

static struct text_buf g_stdout_buf;
static struct text_buf g_stderr_buf;

static int buf_append(struct text_buf *buf, const char *msg) {
        size_t add = strlen(msg);

        if (buf->len + add + 1 > buf->cap) {
                size_t ncap = buf->cap ? buf->cap * 2 : 64;
                char *nbuf;

                while (ncap < buf->len + add + 1)
                        ncap *= 2;

                nbuf = realloc(buf->buf, ncap);

                if (!nbuf)
                        return -1;

                buf->buf = nbuf;
                buf->cap = ncap;
        }

        memcpy(buf->buf + buf->len, msg, add + 1);
        buf->len += add;
        return 0;
}

static int capture_stdout(struct maestro_ctx *ctx, const char *msg) {
        (void)ctx;
        return buf_append(&g_stdout_buf, msg) ? -1 : (int)strlen(msg);
}

static int capture_stderr(struct maestro_ctx *ctx, const char *msg) {
        (void)ctx;
        return buf_append(&g_stderr_buf, msg) ? -1 : (int)strlen(msg);
}

static int value_list_push_take(maestro_ctx *ctx, maestro_value *list,
                                maestro_value *item) {
        int rc;

        if (!item)
                return MAESTRO_ERR_NOMEM;

        rc = maestro_list_push(ctx, list, item);
        maestro_value_free(ctx, item);
        return rc;
}

static int fn_echo(maestro_ctx *ctx, maestro_value **args, size_t argc,
                   maestro_value **result) {
        const char *msg;

        if (!ctx || !args || !result || argc != 1)
                return MAESTRO_ERR_RUNTIME;

        msg = maestro_value_as_string(args[0]);

        if (!msg)
                return MAESTRO_ERR_RUNTIME;

        *result = maestro_value_new_int(ctx, (maestro_int_t)strlen(msg));
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_host_int(maestro_ctx *ctx, maestro_value **args, size_t argc,
                       maestro_value **result) {
        (void)args;
        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;
        *result = maestro_value_new_int(ctx, 7);
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_host_float(maestro_ctx *ctx, maestro_value **args, size_t argc,
                         maestro_value **result) {
        (void)args;
        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;
        *result = maestro_value_new_float(ctx, 2.5f);
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_host_bool(maestro_ctx *ctx, maestro_value **args, size_t argc,
                        maestro_value **result) {
        (void)args;
        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;
        *result = maestro_value_new_bool(ctx, true);
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_host_string(maestro_ctx *ctx, maestro_value **args, size_t argc,
                          maestro_value **result) {
        (void)args;
        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;
        *result = maestro_value_new_string(ctx, "host");
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_host_symbol(maestro_ctx *ctx, maestro_value **args, size_t argc,
                          maestro_value **result) {
        (void)args;
        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;
        *result = maestro_value_new_symbol(ctx, "token");
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_host_list(maestro_ctx *ctx, maestro_value **args, size_t argc,
                        maestro_value **result) {
        maestro_value *list;
        maestro_value *nested;

        (void)args;
        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;

        list = maestro_value_new_list(ctx);
        nested = maestro_value_new_list(ctx);

        if (!list || !nested) {
            if (list)
                    maestro_value_free(ctx, list);
            if (nested)
                    maestro_value_free(ctx, nested);
            return MAESTRO_ERR_NOMEM;
        }

        if (value_list_push_take(ctx, nested, maestro_value_new_int(ctx, 1)) ||
            value_list_push_take(ctx, nested, maestro_value_new_string(ctx, "nest")) ||
            value_list_push_take(ctx, list, maestro_value_new_int(ctx, 7)) ||
            value_list_push_take(ctx, list, maestro_value_new_float(ctx, 2.5f)) ||
            value_list_push_take(ctx, list, maestro_value_new_bool(ctx, true)) ||
            value_list_push_take(ctx, list, maestro_value_new_string(ctx, "Ada")) ||
            value_list_push_take(ctx, list, maestro_value_new_symbol(ctx, "token")) ||
            maestro_list_push(ctx, list, nested)) {
                maestro_value_free(ctx, nested);
                maestro_value_free(ctx, list);
                return MAESTRO_ERR_NOMEM;
        }

        maestro_value_free(ctx, nested);
        *result = list;
        return 0;
}

static int fn_host_object(maestro_ctx *ctx, maestro_value **args, size_t argc,
                          maestro_value **result) {
        (void)args;
        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;
        *result = maestro_value_new_json(
                ctx,
                "{\"user\":{\"name\":\"Ada\",\"meta\":{\"active\":\"yes\"}},\"scores\":[1,2,3]}");
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_host_check(maestro_ctx *ctx, maestro_value **args, size_t argc,
                         maestro_value **result) {
        maestro_int_t i;
        maestro_float_t f;
        bool b;
        const char *s;
        const char *sym;
        maestro_value *nested;

        if (!ctx || !args || !result || argc != 6)
                return MAESTRO_ERR_RUNTIME;

        if (maestro_value_as_int(args[0], &i) || i != 7)
                return MAESTRO_ERR_RUNTIME;
        if (maestro_value_as_float(args[1], &f) || f != 2.5f)
                return MAESTRO_ERR_RUNTIME;
        if (maestro_value_as_bool(args[2], &b) || !b)
                return MAESTRO_ERR_RUNTIME;

        s = maestro_value_as_string(args[3]);
        sym = maestro_value_as_symbol(args[4]);
        if (!s || strcmp(s, "host") || !sym || strcmp(sym, "token"))
                return MAESTRO_ERR_RUNTIME;

        if (maestro_value_list_len(args[5]) != 6)
                return MAESTRO_ERR_RUNTIME;

        if (!maestro_value_list_get(args[5], 0) ||
            maestro_value_as_int(maestro_value_list_get(args[5], 0), &i) || i != 7)
                return MAESTRO_ERR_RUNTIME;
        if (!maestro_value_list_get(args[5], 1) ||
            maestro_value_as_float(maestro_value_list_get(args[5], 1), &f) || f != 2.5f)
                return MAESTRO_ERR_RUNTIME;
        if (!maestro_value_list_get(args[5], 2) ||
            maestro_value_as_bool(maestro_value_list_get(args[5], 2), &b) || !b)
                return MAESTRO_ERR_RUNTIME;
        s = maestro_value_as_string(maestro_value_list_get(args[5], 3));
        sym = maestro_value_as_symbol(maestro_value_list_get(args[5], 4));
        if (!s || strcmp(s, "Ada") || !sym || strcmp(sym, "token"))
                return MAESTRO_ERR_RUNTIME;

        nested = maestro_value_list_get(args[5], 5);
        if (!nested || maestro_value_list_len(nested) != 2)
                return MAESTRO_ERR_RUNTIME;
        if (maestro_value_as_int(maestro_value_list_get(nested, 0), &i) || i != 1)
                return MAESTRO_ERR_RUNTIME;
        s = maestro_value_as_string(maestro_value_list_get(nested, 1));
        if (!s || strcmp(s, "nest"))
                return MAESTRO_ERR_RUNTIME;

        *result = maestro_value_new_string(ctx, "checked");
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}
static void *slurp(const char *path, size_t *len) {
        FILE *fp;
        long end;
        void *buf;

        fp = fopen(path, "rb");

        if (!fp)
                return NULL;

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

        buf = malloc((size_t)end);

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
        *len = (size_t)end;
        return buf;
}

static char *dup_text(const char *s) {
        size_t n = strlen(s) + 1;
        char *out = malloc(n);

        if (out)
                memcpy(out, s, n);

        return out;
}

static char *fmt_double(double v) {
        char tmp[64];

        snprintf(tmp, sizeof(tmp), "%g", v);
        return dup_text(tmp);
}

static char *value_text(const maestro_value *v) {
        maestro_int_t i;
        maestro_float_t f;
        bool b;
        const char *s;
        size_t idx;
        char *buf;

        switch (maestro_value_type(v)) {
        case MAESTRO_VAL_INT:
                if (!maestro_value_as_int(v, &i)) {
                        char tmp[64];

                        snprintf(tmp, sizeof(tmp), "%lld", (long long)i);
                        return dup_text(tmp);
                }

                break;

        case MAESTRO_VAL_FLOAT:
                if (!maestro_value_as_float(v, &f))
                        return fmt_double((double)f);

                break;

        case MAESTRO_VAL_BOOL:
                if (!maestro_value_as_bool(v, &b))
                        return dup_text(b ? "true" : "false");

                break;

        case MAESTRO_VAL_STRING:
                s = maestro_value_as_string(v);
                return s ? dup_text(s) : NULL;

        case MAESTRO_VAL_SYMBOL:
                s = maestro_value_as_symbol(v);
                return s ? dup_text(s) : NULL;

        case MAESTRO_VAL_LIST:
                buf = dup_text("[");

                if (!buf)
                        return NULL;

                for (idx = 0; idx < maestro_value_list_len(v); idx++) {
                        maestro_value *item = maestro_value_list_get(v, idx);
                        char *item_text = value_text(item);
                        char *next;
                        size_t need;

                        if (!item_text)
                                return buf;

                        need = strlen(buf) + strlen(item_text) + 3;
                        next = malloc(need);

                        if (!next) {
                                free(item_text);
                                return buf;
                        }

                        snprintf(next, need, "%s%s%s", buf, idx ? "," : "", item_text);
                        free(item_text);
                        free(buf);
                        buf = next;
                }

                {
                        char *next = malloc(strlen(buf) + 2);

                        if (!next)
                                return buf;

                        sprintf(next, "%s]", buf);
                        free(buf);
                        return next;
                }

        default:
                return dup_text("<value>");
        }

        return NULL;
}

static maestro_value *make_arg(maestro_ctx *ctx, const char *spec) {
        const char *sep = strchr(spec, ':');

        if (!sep)
                return maestro_value_new_string(ctx, spec);

        if ((size_t)(sep - spec) == 3 && !strncmp(spec, "int", 3))
                return maestro_value_new_int(ctx, strtoll(sep + 1, NULL, 10));

        if ((size_t)(sep - spec) == 5 && !strncmp(spec, "float", 5))
                return maestro_value_new_float(ctx, strtof(sep + 1, NULL));

        if ((size_t)(sep - spec) == 4 && !strncmp(spec, "bool", 4))
                return maestro_value_new_bool(ctx, !strcmp(sep + 1, "true"));

        if ((size_t)(sep - spec) == 3 && !strncmp(spec, "str", 3))
                return maestro_value_new_string(ctx, sep + 1);

        if ((size_t)(sep - spec) == 3 && !strncmp(spec, "sym", 3))
                return maestro_value_new_symbol(ctx, sep + 1);

        if ((size_t)(sep - spec) == 4 && !strncmp(spec, "json", 4))
                return maestro_value_new_json(ctx, sep + 1);

        return maestro_value_new_string(ctx, spec);
}

int main(int argc, char **argv) {
        maestro_ctx *ctx;
        maestro_value **args = NULL;
        maestro_value *result = NULL;
        size_t len = 0;
        void *img;
        char *text = NULL;
        int ret = 1;
        int i;

        if (argc < 3) {
                fprintf(stderr, "usage: %s image.mstro \"module path\" [arg...]\n", argv[0]);
                return 2;
        }

        img = slurp(argv[1], &len);

        if (!img) {
                fprintf(stderr, "failed to read %s: %s\n", argv[1], strerror(errno));
                return 1;
        }

        ctx = maestro_ctx_new();

        if (!ctx) {
                free(img);
                return 1;
        }

        maestro_ctx_set_output(ctx, capture_stdout, capture_stderr);
        maestro_ctx_set_vm_logger(ctx, capture_stderr);
        if (maestro_register_fn(ctx, "echo", fn_echo) ||
            maestro_register_fn(ctx, "host-int", fn_host_int) ||
            maestro_register_fn(ctx, "host-float", fn_host_float) ||
            maestro_register_fn(ctx, "host-bool", fn_host_bool) ||
            maestro_register_fn(ctx, "host-string", fn_host_string) ||
            maestro_register_fn(ctx, "host-symbol", fn_host_symbol) ||
            maestro_register_fn(ctx, "host-list", fn_host_list) ||
            maestro_register_fn(ctx, "host-object", fn_host_object) ||
            maestro_register_fn(ctx, "host-check", fn_host_check)) {
                fprintf(stderr, "register functions failed\n");
                goto out;
        }

        if (argc > 3) {
                args = calloc((size_t)(argc - 3), sizeof(*args));

                if (!args)
                        goto out;

                for (i = 3; i < argc; i++) {
                        args[i - 3] = make_arg(ctx, argv[i]);

                        if (!args[i - 3])
                                goto out;
                }
        }

        if (maestro_load(ctx, img) || !len) {
                fprintf(stderr, "load failed\n");
                goto out;
        }

        maestro_ctx_set_image_len(ctx, len);

        if (maestro_validate(ctx, stderr)) {
                fprintf(stderr, "validate failed\n");
                goto out;
        }

        if (maestro_run(ctx, argv[2], args, argc > 3 ? (size_t)(argc - 3) : 0,
                        &result)) {
                fprintf(stderr, "run failed\n");
                fprintf(stderr, "[stdout]\n%s\n[/stdout]\n",
                        g_stdout_buf.buf ? g_stdout_buf.buf : "");
                fprintf(stderr, "[stderr]\n%s\n[/stderr]\n",
                        g_stderr_buf.buf ? g_stderr_buf.buf : "");
                goto out;
        }

        text = value_text(result);

        if (!text)
                goto out;

        printf("[stdout]\n%s\n[/stdout]\n", g_stdout_buf.buf ? g_stdout_buf.buf : "");
        printf("[stderr]\n%s\n[/stderr]\n", g_stderr_buf.buf ? g_stderr_buf.buf : "");
        printf("[result]\n%s\n[/result]\n", text);
        ret = 0;
out:

        if (result)
                maestro_value_free(ctx, result);

        if (args) {
                for (i = 3; i < argc; i++) {
                        if (args[i - 3])
                                maestro_value_free(ctx, args[i - 3]);
                }

                free(args);
        }

        free(text);
        free(g_stdout_buf.buf);
        free(g_stderr_buf.buf);
        maestro_ctx_free(ctx);
        free(img);
        return ret;
}
