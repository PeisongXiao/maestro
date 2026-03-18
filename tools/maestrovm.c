#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "maestro/maestro.h"

struct str_vec {
        char **v;
        size_t nr;
        size_t cap;
};

struct sha256_ctx {
        uint32_t h[8];
        uint64_t len;
        uint8_t buf[64];
        size_t nr;
};

static const uint32_t sha256_k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

static void usage(FILE *out, const char *argv0) {
        fprintf(out,
                "usage: %s [options]\n"
                "\n"
                "options:\n"
                "  -h, --help             show this help and exit\n"
                "  -v, --version          show the embedded Maestro runtime version and exit\n"
                "  -l, --libs PATH...     add shared libraries until the next option\n"
                "  -d, --directory DIR    recursively add .so and .mstr files from DIR\n"
                "  -f, --files PATH...    add .mstr source files until the next option\n"
                "  -o, --output PATH      write compiled artifact to PATH\n"
                "  -a, --artifact PATH    run an existing artifact bundle\n"
                "  -m, --magic STRING     override artifact magic string in source mode\n"
                "  -r, --run MOD ARGS     run module path MOD with static literal args ARGS\n",
                argv0);
}

static int has_suffix(const char *name, const char *suf) {
        size_t nl = strlen(name);
        size_t sl = strlen(suf);

        if (nl < sl)
                return 0;

        return !strcmp(name + nl - sl, suf);
}

static int is_dir(const char *path) {
        struct stat st;

        if (stat(path, &st))
                return 0;

        return S_ISDIR(st.st_mode);
}

static char *join_path(const char *a, const char *b) {
        size_t al = strlen(a);
        size_t bl = strlen(b);
        char *s = malloc(al + bl + 2);

        if (!s)
                return NULL;

        memcpy(s, a, al);
        s[al] = '/';
        memcpy(s + al + 1, b, bl + 1);
        return s;
}

static int str_vec_push(struct str_vec *vec, char *s) {
        if (vec->nr == vec->cap) {
                size_t ncap = vec->cap ? vec->cap * 2 : 16;
                char **nv = realloc(vec->v, ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                vec->v = nv;
                vec->cap = ncap;
        }

        vec->v[vec->nr++] = s;
        return 0;
}

static char *normalize_path(const char *path) {
        char *cwd;
        char *out;
        size_t len;

        if (path[0] == '/')
                return strdup(path);

        cwd = getcwd(NULL, 0);

        if (!cwd)
                return NULL;

        len = strlen(cwd) + strlen(path) + 2;
        out = malloc(len);

        if (!out) {
                free(cwd);
                return NULL;
        }

        snprintf(out, len, "%s/%s", cwd, path);
        free(cwd);
        return out;
}

static int str_vec_add_unique(struct str_vec *vec, const char *path) {
        char *norm;
        size_t i;

        norm = normalize_path(path);

        if (!norm)
                return -1;

        for (i = 0; i < vec->nr; i++) {
                if (!strcmp(vec->v[i], norm)) {
                        free(norm);
                        return 0;
                }
        }

        return str_vec_push(vec, norm);
}

static void str_vec_free(struct str_vec *vec) {
        size_t i;

        for (i = 0; i < vec->nr; i++)
                free(vec->v[i]);

        free(vec->v);
        memset(vec, 0, sizeof(*vec));
}

static int collect_dir(struct str_vec *libs, struct str_vec *srcs,
                       const char *dir, FILE *err) {
        DIR *dp;
        struct dirent *de;

        dp = opendir(dir);

        if (!dp) {
                fprintf(err, "open %s: %s\n", dir, strerror(errno));
                return MAESTRO_ERR_PARSE;
        }

        while ((de = readdir(dp))) {
                char *path;
                struct stat st;

                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                        continue;

                path = join_path(dir, de->d_name);

                if (!path) {
                        closedir(dp);
                        return MAESTRO_ERR_NOMEM;
                }

                if (stat(path, &st)) {
                        fprintf(err, "stat %s: %s\n", path, strerror(errno));
                        free(path);
                        closedir(dp);
                        return MAESTRO_ERR_PARSE;
                }

                if (S_ISDIR(st.st_mode)) {
                        int ret = collect_dir(libs, srcs, path, err);

                        free(path);

                        if (ret) {
                                closedir(dp);
                                return ret;
                        }

                        continue;
                }

                if (S_ISREG(st.st_mode) && has_suffix(path, ".mstr")) {
                        if (str_vec_add_unique(srcs, path)) {
                                free(path);
                                closedir(dp);
                                return MAESTRO_ERR_NOMEM;
                        }
                } else if (S_ISREG(st.st_mode) && has_suffix(path, ".so")) {
                        if (str_vec_add_unique(libs, path)) {
                                free(path);
                                closedir(dp);
                                return MAESTRO_ERR_NOMEM;
                        }
                }

                free(path);
        }

        closedir(dp);
        return 0;
}

static int cmp_str(const void *a, const void *b) {
        const char *const *sa = a;
        const char *const *sb = b;

        return strcmp(*sa, *sb);
}

static uint32_t rotr32(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32U - n));
}

static void sha256_init(struct sha256_ctx *ctx) {
        static const uint32_t init[8] = {
                0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
        };

        memcpy(ctx->h, init, sizeof(init));
        ctx->len = 0;
        ctx->nr = 0;
}

static void sha256_block(struct sha256_ctx *ctx, const uint8_t *buf) {
        uint32_t w[64];
        uint32_t a, b, c, d, e, f, g, h;
        size_t i;

        for (i = 0; i < 16; i++) {
                w[i] = ((uint32_t)buf[i * 4] << 24) |
                       ((uint32_t)buf[i * 4 + 1] << 16) |
                       ((uint32_t)buf[i * 4 + 2] << 8) |
                       (uint32_t)buf[i * 4 + 3];
        }

        for (i = 16; i < 64; i++) {
                uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^
                              (w[i - 15] >> 3);
                uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^
                              (w[i - 2] >> 10);

                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        a = ctx->h[0];
        b = ctx->h[1];
        c = ctx->h[2];
        d = ctx->h[3];
        e = ctx->h[4];
        f = ctx->h[5];
        g = ctx->h[6];
        h = ctx->h[7];

        for (i = 0; i < 64; i++) {
                uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
                uint32_t ch = (e & f) ^ ((~e) & g);
                uint32_t tmp1 = h + s1 + ch + sha256_k[i] + w[i];
                uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
                uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                uint32_t tmp2 = s0 + maj;

                h = g;
                g = f;
                f = e;
                e = d + tmp1;
                d = c;
                c = b;
                b = a;
                a = tmp1 + tmp2;
        }

        ctx->h[0] += a;
        ctx->h[1] += b;
        ctx->h[2] += c;
        ctx->h[3] += d;
        ctx->h[4] += e;
        ctx->h[5] += f;
        ctx->h[6] += g;
        ctx->h[7] += h;
}

static void sha256_update(struct sha256_ctx *ctx, const uint8_t *buf,
                          size_t len) {
        size_t i = 0;

        ctx->len += (uint64_t)len * 8U;

        if (ctx->nr) {
                while (i < len && ctx->nr < sizeof(ctx->buf))
                        ctx->buf[ctx->nr++] = buf[i++];

                if (ctx->nr == sizeof(ctx->buf)) {
                        sha256_block(ctx, ctx->buf);
                        ctx->nr = 0;
                }
        }

        while (i + 64 <= len) {
                sha256_block(ctx, buf + i);
                i += 64;
        }

        while (i < len)
                ctx->buf[ctx->nr++] = buf[i++];
}

static void sha256_final(struct sha256_ctx *ctx, uint8_t out[32]) {
        size_t i;

        ctx->buf[ctx->nr++] = 0x80;

        if (ctx->nr > 56) {
                while (ctx->nr < 64)
                        ctx->buf[ctx->nr++] = 0;

                sha256_block(ctx, ctx->buf);
                ctx->nr = 0;
        }

        while (ctx->nr < 56)
                ctx->buf[ctx->nr++] = 0;

        for (i = 0; i < 8; i++)
                ctx->buf[56 + i] = (uint8_t)(ctx->len >> (56U - i * 8U));

        sha256_block(ctx, ctx->buf);

        for (i = 0; i < 8; i++) {
                out[i * 4] = (uint8_t)(ctx->h[i] >> 24);
                out[i * 4 + 1] = (uint8_t)(ctx->h[i] >> 16);
                out[i * 4 + 2] = (uint8_t)(ctx->h[i] >> 8);
                out[i * 4 + 3] = (uint8_t)ctx->h[i];
        }
}

static void hash_magic(const char *s, uint8_t out[32]) {
        struct sha256_ctx ctx;

        sha256_init(&ctx);
        sha256_update(&ctx, (const uint8_t *)s, strlen(s));
        sha256_final(&ctx, out);
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

        if (!v)
                return NULL;

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

        case MAESTRO_VAL_OBJECT:
                return dup_text("<object>");

        default:
                return dup_text("<value>");
        }

	return NULL;
}

static int stderr_output(maestro_ctx *ctx, const char *msg) {
	(void)ctx;
	return fprintf(stderr, "%s", msg);
}

static int stdout_output(maestro_ctx *ctx, const char *msg) {
	(void)ctx;
	return fprintf(stdout, "%s", msg);
}

static int compile_sources(const struct str_vec *srcs, const char *output_path,
			   const uint8_t *magic) {
        FILE *out;
        maestro_asts *asts;
        int ret;

        asts = maestro_asts_new();

        if (!asts)
                return MAESTRO_ERR_NOMEM;

        ret = maestro_parse_list(asts, stderr, (const char **)srcs->v,
                                 (int)srcs->nr);

        if (ret) {
                maestro_asts_free(asts);
                return ret;
        }

        out = fopen(output_path, "wb");

        if (!out) {
                fprintf(stderr, "open %s: %s\n", output_path, strerror(errno));
                maestro_asts_free(asts);
                return MAESTRO_ERR_RUNTIME;
        }

        ret = maestro_link_ex(out, asts, magic, 0);
        fclose(out);
        maestro_asts_free(asts);
        return ret;
}

static int make_temp_artifact(char **out_path) {
        char tmpl[] = "/tmp/maestrovm-artifact-XXXXXX";
        int fd;

        fd = mkstemp(tmpl);

        if (fd < 0)
                return MAESTRO_ERR_RUNTIME;

        close(fd);
        *out_path = strdup(tmpl);
        return *out_path ? 0 : MAESTRO_ERR_NOMEM;
}

static void skip_ws(const char **sp) {
        while (**sp == ' ' || **sp == '\t' || **sp == '\n' || **sp == '\r')
                (*sp)++;
}

static char *dup_range(const char *start, size_t len) {
        char *out = malloc(len + 1);

        if (!out)
                return NULL;

        memcpy(out, start, len);
        out[len] = '\0';
        return out;
}

static int push_char(char **buf, size_t *len, size_t *cap, char c) {
        char *next;

        if (*len + 2 > *cap) {
                size_t ncap = *cap ? *cap * 2 : 32;

                while (*len + 2 > ncap)
                        ncap *= 2;

                next = realloc(*buf, ncap);

                if (!next)
                        return -1;

                *buf = next;
                *cap = ncap;
        }

        (*buf)[(*len)++] = c;
        (*buf)[*len] = '\0';
        return 0;
}

static int parse_quoted(const char **sp, char **out) {
        const char *s = *sp;
        char *buf = NULL;
        size_t len = 0;
        size_t cap = 0;

        if (*s != '"')
                return MAESTRO_ERR_RUNTIME;

        s++;

        while (*s && *s != '"') {
                char c = *s++;

                if (c == '\\') {
                        if (!*s) {
                                free(buf);
                                return MAESTRO_ERR_RUNTIME;
                        }

                        switch (*s++) {
                        case 'n':
                                c = '\n';
                                break;

                        case 't':
                                c = '\t';
                                break;

                        case 'r':
                                c = '\r';
                                break;

                        case '\\':
                                c = '\\';
                                break;

                        case '"':
                                c = '"';
                                break;

                        default:
                                c = s[-1];
                                break;
                        }
                }

                if (push_char(&buf, &len, &cap, c)) {
                        free(buf);
                        return MAESTRO_ERR_NOMEM;
                }
        }

        if (*s != '"') {
                free(buf);
                return MAESTRO_ERR_RUNTIME;
        }

        *sp = s + 1;
        *out = buf ? buf : dup_text("");
        return *out ? 0 : MAESTRO_ERR_NOMEM;
}

static int scan_json_end(const char *s, const char **endp) {
        int depth = 0;
        bool in_string = false;
        bool escape = false;

        if (*s != '{')
                return MAESTRO_ERR_RUNTIME;

        while (*s) {
                char c = *s++;

                if (in_string) {
                        if (escape) {
                                escape = false;
                                continue;
                        }

                        if (c == '\\') {
                                escape = true;
                        } else if (c == '"') {
                                in_string = false;
                        }

                        continue;
                }

                if (c == '"') {
                        in_string = true;
                        continue;
                }

                if (c == '{')
                        depth++;
                else if (c == '}') {
                        depth--;

                        if (depth == 0) {
                                *endp = s;
                                return 0;
                        }
                }
        }

        return MAESTRO_ERR_RUNTIME;
}

static int parse_arg_value(maestro_ctx *ctx, const char **sp,
                           maestro_value **out) {
        const char *s = *sp;
        const char *end;
        char *text;
        char *tmp;
        char *num_end;
        bool is_float = false;
        size_t len;
        maestro_value *v;

        skip_ws(&s);

        if (!*s) {
                *sp = s;
                return 1;
        }

        if (*s == '"') {
                int ret = parse_quoted(&s, &text);

                if (ret)
                        return ret;

                v = maestro_value_new_string(ctx, text);
                free(text);

                if (!v)
                        return MAESTRO_ERR_NOMEM;

                *out = v;
                *sp = s;
                return 0;
        }

        if (*s == '\'') {
                const char *start = ++s;

                while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r')
                        s++;

                if (s == start)
                        return MAESTRO_ERR_RUNTIME;

                text = dup_range(start, (size_t)(s - start));

                if (!text)
                        return MAESTRO_ERR_NOMEM;

                v = maestro_value_new_symbol(ctx, text);
                free(text);

                if (!v)
                        return MAESTRO_ERR_NOMEM;

                *out = v;
                *sp = s;
                return 0;
        }

        if (*s == '{') {
                if (scan_json_end(s, &end))
                        return MAESTRO_ERR_RUNTIME;

                text = dup_range(s, (size_t)(end - s));

                if (!text)
                        return MAESTRO_ERR_NOMEM;

                v = maestro_value_new_json(ctx, text);
                free(text);

                if (!v)
                        return MAESTRO_ERR_RUNTIME;

                *out = v;
                *sp = end;
                return 0;
        }

        end = s;

        while (*end && *end != ' ' && *end != '\t' && *end != '\n' &&
               *end != '\r')
                end++;

        len = (size_t)(end - s);

        if (!len)
                return MAESTRO_ERR_RUNTIME;

        text = dup_range(s, len);

        if (!text)
                return MAESTRO_ERR_NOMEM;

        for (tmp = text; *tmp; tmp++) {
                if (*tmp == '.') {
                        is_float = true;
                        break;
                }
        }

        errno = 0;

        if (is_float) {
                maestro_float_t f = strtof(text, &num_end);

                if (errno || *num_end) {
                        free(text);
                        return MAESTRO_ERR_RUNTIME;
                }

                v = maestro_value_new_float(ctx, f);
        } else {
                maestro_int_t i = (maestro_int_t)strtoll(text, &num_end, 10);

                if (errno || *num_end) {
                        free(text);
                        return MAESTRO_ERR_RUNTIME;
                }

                v = maestro_value_new_int(ctx, i);
        }

        free(text);

        if (!v)
                return MAESTRO_ERR_NOMEM;

        *out = v;
        *sp = end;
        return 0;
}

static int parse_run_args(maestro_ctx *ctx, const char *spec,
                          maestro_value ***out_args, size_t *out_argc) {
        const char *s = spec;
        struct str_vec dummy = {0};
        maestro_value **args = NULL;
        size_t argc = 0;
        size_t cap = 0;
        int ret;

        (void)dummy;
        skip_ws(&s);

        if (!*s) {
                *out_args = NULL;
                *out_argc = 0;
                return 0;
        }

        while (*s) {
                maestro_value *v = NULL;
                maestro_value **next;

                ret = parse_arg_value(ctx, &s, &v);

                if (ret == 1)
                        break;

                if (ret) {
                        size_t i;

                        for (i = 0; i < argc; i++)
                                maestro_value_free(ctx, args[i]);

                        free(args);
                        return ret;
                }

                if (argc == cap) {
                        size_t ncap = cap ? cap * 2 : 8;

                        next = realloc(args, ncap * sizeof(*next));

                        if (!next) {
                                maestro_value_free(ctx, v);

                                for (size_t i = 0; i < argc; i++)
                                        maestro_value_free(ctx, args[i]);

                                free(args);
                                return MAESTRO_ERR_NOMEM;
                        }

                        args = next;
                        cap = ncap;
                }

                args[argc++] = v;
                skip_ws(&s);
        }

        *out_args = args;
        *out_argc = argc;
        return 0;
}

static void free_run_args(maestro_ctx *ctx, maestro_value **args, size_t argc) {
        size_t i;

        for (i = 0; i < argc; i++)
                maestro_value_free(ctx, args[i]);

        free(args);
}

int main(int argc, char **argv) {
        struct str_vec libs = {0};
        struct str_vec srcs = {0};
        uint8_t magic_buf[32];
        const char *artifact_path = NULL;
        const char *run_module = NULL;
        const char *run_args_spec = NULL;
        char *tmp_artifact = NULL;
        char *text = NULL;
        void *img = NULL;
        maestro_ctx *ctx = NULL;
        maestro_value **run_args = NULL;
        maestro_value *result = NULL;
        size_t run_argc = 0;
        size_t img_len = 0;
        const char *output_path = NULL;
        int have_output = 0;
        int have_magic = 0;
        int i;
        int ret;

        for (i = 1; i < argc; i++) {
                if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                        usage(stdout, argv[0]);
                        ret = 0;
                        goto out;
                }

                if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
                        fprintf(stdout, "%u\n", MAESTRO_VERSION);
                        ret = 0;
                        goto out;
                }

                if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--libs")) {
                        int added = 0;

                        while (i + 1 < argc && argv[i + 1][0] != '-') {
                                if (str_vec_add_unique(&libs, argv[++i])) {
                                        fprintf(stderr, "unable to add library %s\n",
                                                argv[i]);
                                        ret = 1;
                                        goto out;
                                }

                                added = 1;
                        }

                        if (!added) {
                                fprintf(stderr, "-l/--libs requires at least one file\n");
                                ret = 1;
                                goto out;
                        }

                        continue;
                }

                if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--directory")) {
                        if (i + 1 >= argc) {
                                usage(stderr, argv[0]);
                                ret = 1;
                                goto out;
                        }

                        i++;

                        if (!is_dir(argv[i])) {
                                fprintf(stderr, "%s is not a directory\n", argv[i]);
                                ret = 1;
                                goto out;
                        }

                        ret = collect_dir(&libs, &srcs, argv[i], stderr);

                        if (ret)
                                goto out;

                        continue;
                }

                if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--files")) {
                        int added = 0;

                        while (i + 1 < argc && argv[i + 1][0] != '-') {
                                if (str_vec_add_unique(&srcs, argv[++i])) {
                                        fprintf(stderr, "unable to add source %s\n",
                                                argv[i]);
                                        ret = 1;
                                        goto out;
                                }

                                added = 1;
                        }

                        if (!added) {
                                fprintf(stderr, "-f/--files requires at least one file\n");
                                ret = 1;
                                goto out;
                        }

                        continue;
                }

                if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
                        if (have_output || i + 1 >= argc) {
                                usage(stderr, argv[0]);
                                ret = 1;
                                goto out;
                        }

                        output_path = argv[++i];
                        have_output = 1;
                        continue;
                }

                if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--artifact")) {
                        if (artifact_path || i + 1 >= argc) {
                                usage(stderr, argv[0]);
                                ret = 1;
                                goto out;
                        }

                        artifact_path = argv[++i];
                        continue;
                }

                if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--magic")) {
                        if (have_magic || i + 1 >= argc) {
                                usage(stderr, argv[0]);
                                ret = 1;
                                goto out;
                        }

                        hash_magic(argv[++i], magic_buf);
                        have_magic = 1;
                        continue;
                }

                if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--run")) {
                        if (run_module || i + 2 >= argc) {
                                usage(stderr, argv[0]);
                                ret = 1;
                                goto out;
                        }

                        run_module = argv[++i];
                        run_args_spec = argv[++i];
                        continue;
                }

                fprintf(stderr, "unknown option or misplaced argument: %s\n", argv[i]);
                usage(stderr, argv[0]);
                ret = 1;
                goto out;
        }

        if (libs.nr)
                qsort(libs.v, libs.nr, sizeof(*libs.v), cmp_str);

        if (srcs.nr)
                qsort(srcs.v, srcs.nr, sizeof(*srcs.v), cmp_str);

        if (!run_module && artifact_path) {
                fprintf(stderr, "-a/--artifact requires -r/--run\n");
                ret = 1;
                goto out;
        }

        if (artifact_path && srcs.nr) {
                fprintf(stderr, "-a/--artifact cannot be used with source files\n");
                ret = 1;
                goto out;
        }

        if (artifact_path && have_output) {
                fprintf(stderr, "-a/--artifact cannot be used with -o/--output\n");
                ret = 1;
                goto out;
        }

        if (artifact_path && have_magic) {
                fprintf(stderr, "-a/--artifact cannot be used with -m/--magic\n");
                ret = 1;
                goto out;
        }

        if (!artifact_path && !srcs.nr) {
                fprintf(stderr, "no artifact or source files provided\n");
                ret = 1;
                goto out;
        }

        if (!artifact_path) {
                if (!output_path) {
                        ret = make_temp_artifact(&tmp_artifact);

                        if (ret) {
                                fprintf(stderr, "failed to create temporary artifact path\n");
                                goto out;
                        }

                        output_path = tmp_artifact;
                }

                ret = compile_sources(&srcs, output_path, have_magic ? magic_buf : NULL);

                if (ret)
                        goto out;

                artifact_path = output_path;
        }

        ctx = maestro_ctx_new();

	if (!ctx) {
		ret = MAESTRO_ERR_NOMEM;
		goto out;
	}

	maestro_ctx_set_output(ctx, stdout_output, stderr_output);
	maestro_ctx_set_vm_logger(ctx, stderr_output);

	for (i = 0; i < (int)libs.nr; i++) {
		ret = maestro_ctx_load_dll(ctx, libs.v[i]);

                if (ret) {
                        fprintf(stderr, "load dll %s failed\n", libs.v[i]);
                        goto out;
                }
        }

        img = slurp(artifact_path, &img_len);

        if (!img) {
                fprintf(stderr, "load %s: %s\n", artifact_path, strerror(errno));
                ret = 1;
                goto out;
        }

        ret = maestro_load(ctx, img);

        if (ret)
                goto out;

        maestro_ctx_set_image_len(ctx, img_len);

        if (maestro_validate(ctx, stderr)) {
                ret = 1;
                goto out;
        }

        if (!run_module) {
                fprintf(stderr, "validation succeeded: %s\n", artifact_path);
                ret = 0;
                goto out;
        }

        ret = parse_run_args(ctx, run_args_spec ? run_args_spec : "", &run_args,
                             &run_argc);

        if (ret) {
                fprintf(stderr, "invalid run arguments\n");
                goto out;
        }

	ret = maestro_run(ctx, run_module, run_args, run_argc, &result);

	if (ret) {
		fprintf(stderr, "runtime error: module \"%s\" failed with code %d\n",
			run_module, ret);
		goto out;
	}

	text = value_text(result);

        if (text)
                fprintf(stdout, "%s\n", text);

        ret = 0;
out:

        if (result)
                maestro_value_free(ctx, result);

        if (run_args)
                free_run_args(ctx, run_args, run_argc);

        free(text);
        maestro_ctx_free(ctx);
        free(img);
        free(tmp_artifact);
        str_vec_free(&libs);
        str_vec_free(&srcs);
        return ret;
}
