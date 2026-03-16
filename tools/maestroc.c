#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <limits.h>
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
                "  -h, --help           show this help and exit\n"
                "  -v, --version        show the embedded Maestro version and exit\n"
                "  -m, --magic STRING   override artifact magic string\n"
                "  -c, --capabilities N set artifact capability bitmap\n"
                "  -f, --files PATH...  add source files until the next option\n"
                "  -d, --directory DIR  recursively add .mstr files from a directory\n"
                "  -o, --output PATH    write output artifact to PATH\n",
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

static int collect_dir(struct str_vec *vec, const char *dir, FILE *err) {
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
                        int ret = collect_dir(vec, path, err);

                        free(path);

                        if (ret) {
                                closedir(dp);
                                return ret;
                        }

                        continue;
                }

                if (S_ISREG(st.st_mode) && has_suffix(path, ".mstr")) {
                        if (str_vec_add_unique(vec, path)) {
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
                uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
                uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);

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

int main(int argc, char **argv) {
        struct str_vec files = {0};
        maestro_asts *asts;
        uint8_t magic_buf[32];
        const char *output_path = "./artifact.mstro";
        FILE *out;
        uint64_t capability = 0;
        int have_magic = 0;
        int have_cap = 0;
        int have_output = 0;
        int i;
        int ret;

        for (i = 1; i < argc; i++) {
                if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                        usage(stdout, argv[0]);
                        return 0;
                }

                if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
                        fprintf(stdout, "%u\n", MAESTRO_VERSION);
                        return 0;
                }

                if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--magic")) {
                        if (have_magic || i + 1 >= argc) {
                                usage(stderr, argv[0]);
                                return 1;
                        }

                        hash_magic(argv[++i], magic_buf);
                        have_magic = 1;
                        continue;
                }

                if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--capabilities")) {
                        char *end;

                        if (have_cap || i + 1 >= argc) {
                                usage(stderr, argv[0]);
                                return 1;
                        }

                        errno = 0;
                        capability = strtoull(argv[++i], &end, 10);

                        if (errno || *end) {
                                fprintf(stderr, "invalid capability bitmap: %s\n", argv[i]);
                                return 1;
                        }

                        have_cap = 1;
                        continue;
                }

                if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
                        if (have_output || i + 1 >= argc) {
                                usage(stderr, argv[0]);
                                return 1;
                        }

                        output_path = argv[++i];
                        have_output = 1;
                        continue;
                }

                if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--files")) {
                        int added = 0;

                        while (i + 1 < argc && argv[i + 1][0] != '-') {
                                if (str_vec_add_unique(&files, argv[++i])) {
                                        fprintf(stderr, "unable to add file %s\n", argv[i]);
                                        return 1;
                                }

                                added = 1;
                        }

                        if (!added) {
                                fprintf(stderr, "-f/--files requires at least one file\n");
                                return 1;
                        }

                        continue;
                }

                if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--directory")) {
                        if (i + 1 >= argc) {
                                usage(stderr, argv[0]);
                                return 1;
                        }

                        i++;

                        if (!is_dir(argv[i])) {
                                fprintf(stderr, "%s is not a directory\n", argv[i]);
                                return 1;
                        }

                        ret = collect_dir(&files, argv[i], stderr);

                        if (ret)
                                return ret;

                        continue;
                }

                fprintf(stderr, "unknown option or misplaced argument: %s\n", argv[i]);
                usage(stderr, argv[0]);
                return 1;
        }

        if (!files.nr) {
                fprintf(stderr, "no input files provided\n");
                usage(stderr, argv[0]);
                return 1;
        }

        qsort(files.v, files.nr, sizeof(*files.v), cmp_str);
        asts = maestro_asts_new();

        if (!asts)
                return MAESTRO_ERR_NOMEM;

        ret = maestro_parse_list(asts, stderr, (const char **)files.v, (int)files.nr);

        if (ret)
                return ret;

        out = fopen(output_path, "wb");

        if (!out) {
                fprintf(stderr, "open %s: %s\n", output_path, strerror(errno));
                return 1;
        }

        ret = maestro_link_ex(out, asts, have_magic ? magic_buf : NULL, capability);
        fclose(out);
        maestro_asts_free(asts);
        return ret;
}
