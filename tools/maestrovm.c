#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "maestro/maestro.h"

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

static char *value_text(const maestro_value *v) {
        maestro_int_t i;
        maestro_float_t f;
        bool b;
        const char *s;
        size_t n;

        if (!v)
                return NULL;

        switch (maestro_value_type(v)) {
        case MAESTRO_VAL_INT:
                if (!maestro_value_as_int(v, &i)) {
                        char *out = malloc(32);

                        if (out)
                                snprintf(out, 32, "%lld", (long long)i);

                        return out;
                }

                break;

        case MAESTRO_VAL_FLOAT:
                if (!maestro_value_as_float(v, &f)) {
                        char *out = malloc(32);

                        if (out)
                                snprintf(out, 32, "%g", (double)f);

                        return out;
                }

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
                n = maestro_value_list_len(v);

                if (n == 0)
                        return dup_text("[]");

                return dup_text("<list>");

        default:
                return dup_text("<value>");
        }

        return NULL;
}

int main(int argc, char **argv) {
        maestro_ctx *ctx;
        maestro_value *result = NULL;
        size_t len;
        void *buf;
        int ret;
        char *text;

        if (argc != 3) {
                fprintf(stderr, "usage: %s image.mstro module-path\n", argv[0]);
                return 1;
        }

        buf = slurp(argv[1], &len);

        if (!buf) {
                fprintf(stderr, "load %s: %s\n", argv[1], strerror(errno));
                return 1;
        }

        ctx = maestro_ctx_new();

        if (!ctx) {
                free(buf);
                return 1;
        }

        ret = maestro_load(ctx, buf);

        if (ret) {
                maestro_ctx_free(ctx);
                free(buf);
                return ret;
        }

        maestro_ctx_set_image_len(ctx, len);

        if (maestro_validate(ctx, stderr)) {
                maestro_ctx_free(ctx);
                free(buf);
                return 1;
        }

        ret = maestro_run(ctx, argv[2], NULL, 0, &result);
        text = value_text(result);

        if (text) {
                fprintf(stdout, "%s\n", text);
                free(text);
        }

        maestro_value_free(ctx, result);
        maestro_ctx_free(ctx);
        free(buf);
        return ret;
}
