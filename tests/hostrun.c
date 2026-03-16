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

static int ext_len(struct maestro_ctx *ctx, const char *msg) {
        (void)ctx;
        return (int)strlen(msg);
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
                fprintf(stderr, "usage: %s image.mstro module.path [arg...]\n", argv[0]);
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
        maestro_ctx_add_tool(ctx, "echo", ext_len);

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
