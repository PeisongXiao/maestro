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

static int check_scalar_int(maestro_ctx *ctx, const char *module_path,
                            maestro_int_t want) {
        maestro_value *result = NULL;
        maestro_int_t got;
        int ret = maestro_run(ctx, module_path, NULL, 0, &result);

        if (ret)
                return ret;

        ret = maestro_value_as_int(result, &got) || got != want;
        maestro_value_free(ctx, result);
        return ret ? MAESTRO_ERR_RUNTIME : 0;
}

static int check_scalar_float(maestro_ctx *ctx, const char *module_path,
                              maestro_float_t want) {
        maestro_value *result = NULL;
        maestro_float_t got;
        int ret = maestro_run(ctx, module_path, NULL, 0, &result);

        if (ret)
                return ret;

        ret = maestro_value_as_float(result, &got) || got != want;
        maestro_value_free(ctx, result);
        return ret ? MAESTRO_ERR_RUNTIME : 0;
}

static int check_scalar_bool(maestro_ctx *ctx, const char *module_path,
                             bool want) {
        maestro_value *result = NULL;
        bool got;
        int ret = maestro_run(ctx, module_path, NULL, 0, &result);

        if (ret)
                return ret;

        ret = maestro_value_as_bool(result, &got) || got != want;
        maestro_value_free(ctx, result);
        return ret ? MAESTRO_ERR_RUNTIME : 0;
}

static int check_string(maestro_ctx *ctx, const char *module_path,
                        const char *want, bool symbol) {
        maestro_value *result = NULL;
        const char *got;
        int ret = maestro_run(ctx, module_path, NULL, 0, &result);

        if (ret)
                return ret;

        got = symbol ? maestro_value_as_symbol(result) : maestro_value_as_string(
                      result);
        ret = !got || strcmp(got, want);
        maestro_value_free(ctx, result);
        return ret ? MAESTRO_ERR_RUNTIME : 0;
}

static int check_list(maestro_ctx *ctx) {
        maestro_value *result = NULL;
        maestro_value *nested;
        maestro_int_t i;
        maestro_float_t f;
        bool b;
        const char *s;
        const char *sym;
        int ret = maestro_run(ctx, "tests dll list", NULL, 0, &result);

        if (ret)
                return ret;

        if (maestro_value_list_len(result) != 6) {
                maestro_value_free(ctx, result);
                return MAESTRO_ERR_RUNTIME;
        }

        ret = maestro_value_as_int(maestro_value_list_get(result, 0), &i) || i != 7;
        ret |= maestro_value_as_float(maestro_value_list_get(result, 1), &f) ||
               f != 2.5f;
        ret |= maestro_value_as_bool(maestro_value_list_get(result, 2), &b) || !b;
        s = maestro_value_as_string(maestro_value_list_get(result, 3));
        sym = maestro_value_as_symbol(maestro_value_list_get(result, 4));
        ret |= !s || strcmp(s, "Ada");
        ret |= !sym || strcmp(sym, "token");
        nested = maestro_value_list_get(result, 5);
        ret |= !nested || maestro_value_list_len(nested) != 2;
        ret |= maestro_value_as_int(maestro_value_list_get(nested, 0), &i) || i != 1;
        s = maestro_value_as_string(maestro_value_list_get(nested, 1));
        ret |= !s || strcmp(s, "nest");
        maestro_value_free(ctx, result);
        return ret ? MAESTRO_ERR_RUNTIME : 0;
}

static int check_describe(maestro_ctx *ctx) {
        maestro_value *args[5];
        maestro_value *result = NULL;
        const char *got;
        int ret;

        args[0] = maestro_value_new_int(ctx, 42);
        args[1] = maestro_value_new_float(ctx, 2.5f);
        args[2] = maestro_value_new_string(ctx, "hi");
        args[3] = maestro_value_new_symbol(ctx, "sym");
        args[4] = maestro_value_new_json(ctx, "{\"user\":{\"name\":\"Ada\"}}");

        if (!args[0] || !args[1] || !args[2] || !args[3] || !args[4]) {
                size_t i;

                for (i = 0; i < 5; i++)
                        if (args[i])
                                maestro_value_free(ctx, args[i]);

                return MAESTRO_ERR_NOMEM;
        }

        ret = maestro_run(ctx, "tests dll describe", args, 5, &result);

        for (size_t i = 0; i < 5; i++)
                maestro_value_free(ctx, args[i]);

        if (ret)
                return ret;

        got = maestro_value_as_string(result);
        ret = !got || strcmp(got, "42|2.5|hi|sym|{\"user\":{\"name\":\"Ada\"}}");
        maestro_value_free(ctx, result);
        return ret ? MAESTRO_ERR_RUNTIME : 0;
}

static int load_artifact(maestro_ctx *ctx, const char *path, void **img_out) {
        void *img;
        size_t len;
        int ret;

        img = slurp(path, &len);

        if (!img)
                return MAESTRO_ERR_RUNTIME;

        ret = maestro_load(ctx, img);

        if (!ret)
                maestro_ctx_set_image_len(ctx, len);

        if (!ret && maestro_validate(ctx, stderr))
                ret = MAESTRO_ERR_VALIDATE;

        if (ret) {
                free(img);
                return ret;
        }

        *img_out = img;
        return 0;
}

static maestro_ctx *load_ctx_for_check(const char *plugin, const char *artifact,
                                       void **img_out) {
        maestro_ctx *ctx = maestro_ctx_new();

        if (!ctx)
                return NULL;

        if (maestro_ctx_load_dll(ctx, plugin) ||
            load_artifact(ctx, artifact, img_out)) {
                maestro_ctx_free(ctx);
                free(*img_out);
                *img_out = NULL;
                return NULL;
        }

        return ctx;
}

int main(int argc, char **argv) {
        const char *artifact;
        maestro_ctx *ctx;
        void *img = NULL;
        int ret = 1;

        if (argc != 6) {
                fprintf(stderr,
                        "usage: %s artifact.mstro ok.so duplicate.so fail.so missing.so\n",
                        argv[0]);
                return 2;
        }

        artifact = argv[1];

        ctx = maestro_ctx_new();

        if (!ctx)
                return 1;

        if (maestro_ctx_load_dll(ctx, argv[2])) {
                fprintf(stderr, "expected %s to load\n", argv[2]);
                goto out_ctx;
        }

        if (maestro_ctx_load_dll(ctx, argv[3]) != MAESTRO_ERR_RUNTIME) {
                fprintf(stderr, "expected duplicate registration failure\n");
                goto out_ctx;
        }

        maestro_ctx_free(ctx);

        ctx = maestro_ctx_new();

        if (!ctx)
                return 1;

        if (maestro_ctx_load_dll(ctx, argv[4]) == 0) {
                fprintf(stderr, "expected init failure for %s\n", argv[4]);
                goto out_ctx;
        }

        maestro_ctx_free(ctx);

        ctx = maestro_ctx_new();

        if (!ctx)
                return 1;

        if (maestro_ctx_load_dll(ctx, argv[5]) == 0) {
                fprintf(stderr, "expected missing symbol failure for %s\n", argv[5]);
                goto out_ctx;
        }

        maestro_ctx_free(ctx);

        ctx = load_ctx_for_check(argv[2], artifact, &img);

        if (!ctx) {
                fprintf(stderr, "prepare int ctx failed\n");
                return 1;
        }

        if (check_scalar_int(ctx, "tests dll int", 7)) {
                fprintf(stderr, "int check failed\n");
                goto out_ctx;
        }

        maestro_ctx_free(ctx);
        free(img);

        ctx = load_ctx_for_check(argv[2], artifact, &img);

        if (!ctx) {
                fprintf(stderr, "prepare float ctx failed\n");
                return 1;
        }

        if (check_scalar_float(ctx, "tests dll float", 2.5f)) {
                fprintf(stderr, "float check failed\n");
                goto out_ctx;
        }

        maestro_ctx_free(ctx);
        free(img);

        ctx = load_ctx_for_check(argv[2], artifact, &img);

        if (!ctx) {
                fprintf(stderr, "prepare bool ctx failed\n");
                return 1;
        }

        if (check_scalar_bool(ctx, "tests dll bool", true)) {
                fprintf(stderr, "bool check failed\n");
                goto out_ctx;
        }

        maestro_ctx_free(ctx);
        free(img);

        ctx = load_ctx_for_check(argv[2], artifact, &img);

        if (!ctx) {
                fprintf(stderr, "prepare string ctx failed\n");
                return 1;
        }

        if (check_string(ctx, "tests dll string", "host", false)) {
                fprintf(stderr, "string check failed\n");
                goto out_ctx;
        }

        maestro_ctx_free(ctx);
        free(img);

        ctx = load_ctx_for_check(argv[2], artifact, &img);

        if (!ctx) {
                fprintf(stderr, "prepare symbol ctx failed\n");
                return 1;
        }

        if (check_string(ctx, "tests dll symbol", "token", true)) {
                fprintf(stderr, "symbol check failed\n");
                goto out_ctx;
        }

        maestro_ctx_free(ctx);
        free(img);

        ctx = load_ctx_for_check(argv[2], artifact, &img);

        if (!ctx) {
                fprintf(stderr, "prepare list ctx failed\n");
                return 1;
        }

        if (check_list(ctx)) {
                fprintf(stderr, "list check failed\n");
                goto out_ctx;
        }

        maestro_ctx_free(ctx);
        free(img);

        ctx = load_ctx_for_check(argv[2], artifact, &img);

        if (!ctx) {
                fprintf(stderr, "prepare describe ctx failed\n");
                return 1;
        }

        if (check_describe(ctx)) {
                fprintf(stderr, "describe check failed\n");
                goto out_ctx;
        }

        ret = 0;
out_ctx:
        maestro_ctx_free(ctx);
        free(img);
        return ret;
}
