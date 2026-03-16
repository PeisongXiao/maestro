#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "maestro/maestro.h"

struct file_case {
        const char *path;
        const char *buf;
};

struct run_case {
        const char *module_path;
        const char *expect;
};

static int ext_len(struct maestro_ctx *ctx, const char *msg) {
        (void)ctx;
        return (int)strlen(msg);
}

static int write_file(const char *path, const char *buf) {
        FILE *fp = fopen(path, "wb");
        size_t len = strlen(buf);

        if (!fp)
                return -errno;

        if (fwrite(buf, 1, len, fp) != len) {
                fclose(fp);
                return -EIO;
        }

        fclose(fp);
        return 0;
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

static char *value_text(const maestro_value *v) {
        maestro_int_t i;
        const char *s;

        switch (maestro_value_type(v)) {
        case MAESTRO_VAL_INT:
                if (!maestro_value_as_int(v, &i)) {
                        char *out = malloc(32);

                        if (out)
                                snprintf(out, 32, "%lld", (long long)i);

                        return out;
                }

                break;

        case MAESTRO_VAL_STRING:
                s = maestro_value_as_string(v);
                return s ? dup_text(s) : NULL;

        case MAESTRO_VAL_SYMBOL:
                s = maestro_value_as_symbol(v);
                return s ? dup_text(s) : NULL;

        default:
                return dup_text("<value>");
        }

        return NULL;
}

static int run_case_check(maestro_ctx *ctx, const char *module_path,
                          const char *expect) {
        maestro_value *result = NULL;
        char *text;
        int ret;

        ret = maestro_run(ctx, module_path, NULL, 0, &result);

        if (ret) {
                fprintf(stderr, "maestro_run(%s) failed: %d\n", module_path, ret);
                return 1;
        }

        text = value_text(result);

        if (!text) {
                fprintf(stderr, "stringify failed for %s\n", module_path);
                maestro_value_free(ctx, result);
                return 1;
        }

        if (strcmp(text, expect)) {
                fprintf(stderr, "%s => got %s want %s\n", module_path, text, expect);
                free(text);
                maestro_value_free(ctx, result);
                return 1;
        }

        free(text);
        maestro_value_free(ctx, result);
        return 0;
}

static int run_case_check_args(maestro_ctx *ctx, const char *module_path,
                               const char *expect) {
        maestro_value *arg = NULL;
        maestro_value *argv[1];
        maestro_value *result = NULL;
        char *text;
        int ret;

        arg = maestro_value_new_string(ctx, "Ada");

        if (!arg)
                return 1;

        argv[0] = arg;
        ret = maestro_run(ctx, module_path, argv, 1, &result);

        if (ret) {
                fprintf(stderr, "maestro_run(%s, args) failed: %d\n", module_path, ret);
                maestro_value_free(ctx, arg);
                return 1;
        }

        text = value_text(result);

        if (!text) {
                maestro_value_free(ctx, result);
                maestro_value_free(ctx, arg);
                return 1;
        }

        if (strcmp(text, expect)) {
                fprintf(stderr, "%s(args) => got %s want %s\n", module_path, text, expect);
                free(text);
                maestro_value_free(ctx, result);
                maestro_value_free(ctx, arg);
                return 1;
        }

        free(text);
        maestro_value_free(ctx, result);
        maestro_value_free(ctx, arg);
        return 0;
}

int main(void) {
        static const struct file_case files[] = {
                {
                        .path = "examples/sample.mstr",
                        .buf =
                                "(module sample)\n"
                                "(state (start)\n"
                                "  (steps\n"
                                "    (let user empty-object)\n"
                                "    (set user name \"Ada\")\n"
                                "    (transition end (concat \"hello \" (get user name)))))\n",
                },
                {
                        .path = "examples/lib-strings.mstr",
                        .buf =
                        "(module lib strings)\n"
                        "(export *)\n"
                        "(define copy concat)\n"
                        "(define answer \"wild\")\n",
                },
                {
                        .path = "examples/app-imports.mstr",
                        .buf =
                        "(module app imports)\n"
                        "(import lib strings copy)\n"
                        "(import lib strings *)\n"
                        "(state (start)\n"
                        "  (steps\n"
                        "    (transition end (concat (copy \"im\" \"port\") \":\" answer))))\n",
                },
                {
                        .path = "examples/app-refs.mstr",
                        .buf =
                        "(module app refs)\n"
                        "(define (inc (ref x)) (set x (+ x 1)))\n"
                        "(state (start)\n"
                        "  (steps\n"
                        "    (let user empty-object)\n"
                        "    (set user age 41)\n"
                        "    (let age (ref user age))\n"
                        "    (inc age)\n"
                        "    (transition end (get user age))))\n",
                },
                {
                        .path = "examples/app-last.mstr",
                        .buf =
                        "(module app last)\n"
                        "(state (start)\n"
                        "  (steps\n"
                        "    (transition next \"alpha\")))\n"
                        "(state (next)\n"
                        "  (steps\n"
                        "    (transition end (get last-state val))))\n",
                },
                {
                        .path = "examples/app-worker.mstr",
                        .buf =
                        "(module app worker)\n"
                        "(state (start name)\n"
                        "  (steps\n"
                        "    (transition end (concat \"worker:\" name))))\n",
                },
                {
                        .path = "examples/app-caller.mstr",
                        .buf =
                        "(module app caller)\n"
                        "(define worker (import-program app worker))\n"
                        "(state (start)\n"
                        "  (steps\n"
                        "    (let msg (run worker (list \"Ada\")))\n"
                        "    (transition end msg)))\n",
                },
                {
                        .path = "examples/app-json.mstr",
                        .buf =
                        "(module app json)\n"
                        "(state (start)\n"
                        "  (steps\n"
                        "    (let age 37)\n"
                        "    (let user {\"name\":\"Ada\",\"age\":(+ age 1),\"tags\":(list 'x 'y)})\n"
                        "    (transition end (get user age))))\n",
                },
                {
                        .path = "examples/app-handoff-src.mstr",
                        .buf =
                        "(module app handoff src)\n"
                        "(state (start)\n"
                        "  (steps\n"
                        "    (transition (import app handoff dst done))))\n",
                },
                {
                        .path = "examples/app-handoff-dst.mstr",
                        .buf =
                        "(module app handoff dst)\n"
                        "(export done)\n"
                        "(state (done)\n"
                        "  (steps\n"
                        "    (transition end \"handoff\")))\n",
                },
                {
                        .path = "examples/app-external.mstr",
                        .buf =
                        "(module app external)\n"
                        "(define (echo msg) external)\n"
                        "(state (start)\n"
                        "  (steps\n"
                        "    (transition end (echo \"hello\"))))\n",
                },
        };
        static const struct run_case runs[] = {
                { "sample", "hello Ada" },
                { "app.imports", "import:wild" },
                { "app.refs", "42" },
                { "app.last", "alpha" },
                { "app.caller", "worker:Ada" },
                { "app.json", "38" },
                { "app.handoff.src", "handoff" },
                { "app.external", "5" },
        };
        const char *srcs[sizeof(files) / sizeof(files[0])];
        maestro_asts *asts;
        maestro_ctx *ctx;
        const char **exts = NULL;
        FILE *fp;
        void *img;
        size_t len;
        size_t i;
        size_t ext_nr;
        int ret;

        for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
                ret = write_file(files[i].path, files[i].buf);

                if (ret) {
                        fprintf(stderr, "write %s failed: %d\n", files[i].path, ret);
                        return 1;
                }

                srcs[i] = files[i].path;
        }

        asts = maestro_asts_new();

        if (!asts)
                return 1;

        ret = maestro_parse_list(asts, stderr, srcs,
                                 (int)(sizeof(srcs) / sizeof(srcs[0])));

        if (ret)
                return 1;

        fp = fopen("examples/all.mstro", "wb");

        if (!fp)
                return 1;

        ret = maestro_link(fp, asts);
        fclose(fp);

        if (ret)
                return 1;

        img = slurp("examples/all.mstro", &len);

        if (!img)
                return 1;

        ctx = maestro_ctx_new();

        if (!ctx)
                return 1;

        ret = maestro_ctx_add_tool(ctx, "echo", ext_len);

        if (ret)
                return 1;

        ret = maestro_load(ctx, img);

        if (ret)
                return 1;

        maestro_ctx_set_image_len(ctx, len);

        if (maestro_validate(ctx, stderr))
                return 1;

        ext_nr = maestro_list_externals(ctx, &exts);

        if (ext_nr != 1 || !exts || strcmp(exts[0], "echo")) {
                fprintf(stderr, "external list mismatch\n");
                return 1;
        }

        for (i = 0; i < sizeof(runs) / sizeof(runs[0]); i++) {
                if (run_case_check(ctx, runs[i].module_path, runs[i].expect))
                        return 1;
        }

        if (run_case_check_args(ctx, "app.worker", "worker:Ada"))
                return 1;

        maestro_ctx_free(ctx);
        maestro_asts_free(asts);
        free(img);
        return 0;
}
