#include "maestro_int.h"

const uint8_t MAESTRO_DEFAULT_MAGIC[32] = {
        0x23, 0x3e, 0x1c, 0xde, 0xd6, 0x1a, 0xce, 0x81,
        0x1d, 0x7a, 0x93, 0x09, 0x47, 0xb0, 0x6a, 0xcb,
        0x18, 0xcd, 0xa1, 0xdc, 0x7b, 0x67, 0xe2, 0x15,
        0x94, 0x1e, 0x18, 0x30, 0xfd, 0xec, 0x3b, 0x7c,
};

void diagf(FILE *err, const char *fmt, ...) {
        va_list ap;

        if (!err)
                return;

        va_start(ap, fmt);
        vfprintf(err, fmt, ap);
        va_end(ap);
}

void *xrealloc(maestro_ctx *ctx, void *ptr, size_t size) {
        (void)ctx;
        return realloc(ptr, size);
}

char *xstrdup(const char *s) {
        size_t len = strlen(s) + 1;
        char *p = malloc(len);

        if (!p)
                return NULL;

        memcpy(p, s, len);
        return p;
}

int strtab_add(struct strtab *tab, const char *s, uint32_t *off) {
        size_t need;
        size_t pos = 0;

        while (pos < tab->len) {
                if (!strcmp(tab->buf + pos, s)) {
                        *off = (uint32_t)pos;
                        return 0;
                }

                pos += strlen(tab->buf + pos) + 1;
        }

        need = strlen(s) + 1;

        if (tab->len + need > tab->cap) {
                size_t ncap = tab->cap ? tab->cap * 2 : 256;

                while (ncap < tab->len + need)
                        ncap *= 2;

                tab->buf = realloc(tab->buf, ncap);

                if (!tab->buf)
                        return -1;

                tab->cap = ncap;
        }

        memcpy(tab->buf + tab->len, s, need);
        *off = (uint32_t)tab->len;
        tab->len += need;
        return 0;
}

int node_vec_reserve(struct node_vec *vec, uint32_t nr, uint32_t *idx) {
        if (vec->nr + nr > vec->cap) {
                size_t ncap = vec->cap ? vec->cap * 2 : 128;
                struct img_node *nv;

                while (ncap < vec->nr + nr)
                        ncap *= 2;

                nv = realloc(vec->v, ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                vec->v = nv;
                vec->cap = ncap;
        }

        *idx = (uint32_t)vec->nr;
        memset(vec->v + vec->nr, 0, nr * sizeof(*vec->v));
        vec->nr += nr;
        return 0;
}

int kv_vec_push(struct kv_vec *vec, struct img_kv kv, uint32_t *idx) {
        if (vec->nr == vec->cap) {
                size_t ncap = vec->cap ? vec->cap * 2 : 128;
                struct img_kv *nv = realloc(vec->v, ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                vec->v = nv;
                vec->cap = ncap;
        }

        *idx = (uint32_t)vec->nr;
        vec->v[vec->nr++] = kv;
        return 0;
}

int mod_vec_push(struct mod_vec *vec, struct img_mod mod) {
        if (vec->nr == vec->cap) {
                size_t ncap = vec->cap ? vec->cap * 2 : 32;
                struct img_mod *nv = realloc(vec->v, ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                vec->v = nv;
                vec->cap = ncap;
        }

        vec->v[vec->nr++] = mod;
        return 0;
}

int ext_vec_push(struct ext_vec *vec, struct img_ext ext, uint32_t *idx) {
        if (vec->nr == vec->cap) {
                size_t ncap = vec->cap ? vec->cap * 2 : 16;
                struct img_ext *nv = realloc(vec->v, ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                vec->v = nv;
                vec->cap = ncap;
        }

        *idx = (uint32_t)vec->nr;
        vec->v[vec->nr++] = ext;
        return 0;
}

int ident_intern(struct strtab *tab, struct ident_vec *idents, const char *s,
                 uint32_t *id) {
        size_t i;
        uint32_t off;

        for (i = 0; i < idents->nr; i++) {
                if (!strcmp(tab->buf + idents->v[i].name_off, s)) {
                        *id = (uint32_t)i;
                        return 0;
                }
        }

        if (strtab_add(tab, s, &off))
                return -1;

        if (idents->nr == idents->cap) {
                size_t ncap = idents->cap ? idents->cap * 2 : 64;
                struct img_ident *nv = realloc(idents->v, ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                idents->v = nv;
                idents->cap = ncap;
        }

        idents->v[idents->nr].name_off = off;
        *id = (uint32_t)idents->nr;
        idents->nr++;
        return 0;
}

int path_vec_push(struct path_vec *vec, struct img_path path, uint32_t *idx) {
        if (vec->nr == vec->cap) {
                size_t ncap = vec->cap ? vec->cap * 2 : 64;
                struct img_path *nv = realloc(vec->v, ncap * sizeof(*nv));

                if (!nv)
                        return -1;

                vec->v = nv;
                vec->cap = ncap;
        }

        *idx = (uint32_t)vec->nr;
        vec->v[vec->nr++] = path;
        return 0;
}
