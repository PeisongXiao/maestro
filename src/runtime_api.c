#include "maestro_int.h"

static int stderr_output(maestro_ctx *ctx, const char *msg) {
        (void)ctx;
        return fprintf(stderr, "%s", msg);
}

static int stdout_output(maestro_ctx *ctx, const char *msg) {
        (void)ctx;
        return fprintf(stdout, "%s", msg);
}

maestro_ctx *maestro_ctx_new(void) {
        maestro_ctx *ctx = calloc(1, sizeof(*ctx));

        if (!ctx)
                return NULL;

        ctx->alloc = malloc;
        ctx->dealloc = free;
        ctx->log = stderr_output;
        ctx->vm_logger = stderr_output;
        ctx->print = stdout_output;
        return ctx;
}

void maestro_ctx_free(maestro_ctx *ctx) {
        size_t i;

        if (!ctx)
                return;

        for (i = 0; i < ctx->tool_nr; i++)
                ctx->dealloc(ctx->tools[i].name);

        ctx->dealloc(ctx->tools);
        ctx->dealloc((void *)ctx->ext_names_cache);
        free(ctx);
}

int maestro_ctx_set_output(maestro_ctx *ctx, maestro_output print_fn,
                           maestro_output log_fn) {
        if (!ctx || !print_fn || !log_fn)
                return MAESTRO_ERR_RUNTIME;

        ctx->print = print_fn;
        ctx->log = log_fn;
        return 0;
}

int maestro_ctx_set_vm_logger(maestro_ctx *ctx, maestro_output fn) {
        if (!ctx || !fn)
                return MAESTRO_ERR_RUNTIME;

        ctx->vm_logger = fn;
        return 0;
}

int maestro_ctx_set_allocator(maestro_ctx *ctx, maestro_alloc_fn alloc,
                              maestro_free_fn dealloc) {
        if (!ctx || !alloc || !dealloc)
                return MAESTRO_ERR_RUNTIME;

        ctx->alloc = alloc;
        ctx->dealloc = dealloc;
        return 0;
}

void maestro_ctx_set_capability(maestro_ctx *ctx, uint64_t vm_cap) {
        if (ctx)
                ctx->vm_cap = vm_cap;
}

void maestro_ctx_set_log_flags(maestro_ctx *ctx, uint64_t flags) {
        if (ctx)
                ctx->log_flags = flags;
}

int maestro_ctx_add_tool(maestro_ctx *ctx, const char *name,
                         maestro_output fn) {
        struct maestro_tool_binding *nv;
        char *dup;

        if (!ctx || !name || !fn)
                return MAESTRO_ERR_RUNTIME;

        if (ctx->tool_nr == ctx->tool_cap) {
                size_t ncap = ctx->tool_cap ? ctx->tool_cap * 2 : 8;

                nv = ctx->alloc(ncap * sizeof(*nv));

                if (!nv)
                        return MAESTRO_ERR_NOMEM;

                if (ctx->tools) {
                        memcpy(nv, ctx->tools, ctx->tool_nr * sizeof(*nv));
                        ctx->dealloc(ctx->tools);
                }

                ctx->tools = nv;
                ctx->tool_cap = ncap;
        }

        dup = ctx->alloc(strlen(name) + 1);

        if (!dup)
                return MAESTRO_ERR_NOMEM;

        strcpy(dup, name);
        ctx->tools[ctx->tool_nr].name = dup;
        ctx->tools[ctx->tool_nr].fn = fn;
        ctx->tool_nr++;
        return 0;
}

int maestro_load(maestro_ctx *dest, const void *src) {
        const struct img_hdr *hdr = src;

        if (!dest || !src)
                return MAESTRO_ERR_LOAD;

        if (memcmp(hdr->magic, MAESTRO_DEFAULT_MAGIC, sizeof(hdr->magic)))
                return MAESTRO_ERR_LOAD;

        if (hdr->version != MAESTRO_VERSION)
                return MAESTRO_ERR_LOAD;

        dest->image = src;
        dest->image_len = hdr->size;
        dest->img_hdr = hdr;
        dest->img_mods = (const uint8_t *)src + hdr->mod_off;
        dest->img_exts = (const uint8_t *)src + hdr->ext_off;
        dest->img_idents = (const uint8_t *)src + hdr->ident_off;
        dest->img_paths = (const uint8_t *)src + hdr->path_off;
        dest->img_nodes = (const uint8_t *)src + hdr->node_off;
        dest->img_kv = (const uint8_t *)src + hdr->kv_off;
        dest->img_strs = (const char *)src + hdr->str_off;
        dest->img_mod_nr = hdr->mod_nr;
        dest->img_ext_nr = hdr->ext_nr;
        dest->img_ident_nr = hdr->ident_nr;
        dest->img_path_nr = hdr->path_nr;
        dest->img_node_nr = hdr->node_nr;
        dest->img_kv_nr = hdr->kv_nr;
        return 0;
}

void maestro_ctx_set_image_len(maestro_ctx *ctx, size_t len) {
        if (ctx)
                ctx->image_len = len;
}

uint64_t maestro_validate(maestro_ctx *dest, FILE *err) {
        struct img_hdr *hdr;
        const struct img_ext *exts;
        uint64_t flags = 0;
        size_t i;

        if (!dest || !dest->img_hdr)
                flags |= MAESTRO_VERR_IMAGE;

        if (!dest->alloc || !dest->dealloc)
                flags |= MAESTRO_VERR_ALLOC;

        if (!dest->log || !dest->print || !dest->vm_logger)
                flags |= MAESTRO_VERR_OUTPUT;

        if (flags) {
                diagf(err, "maestro_validate failed: 0x%llx\n",
                      (unsigned long long)flags);
                return flags;
        }

        hdr = (struct img_hdr *)dest->img_hdr;

        if (dest->image_len && hdr->size > dest->image_len)
                flags |= MAESTRO_VERR_IMAGE;

        if (hdr->mod_off < sizeof(*hdr) || hdr->node_off < hdr->mod_off ||
            hdr->ext_off < hdr->mod_off || hdr->ident_off < hdr->ext_off ||
            hdr->path_off < hdr->ident_off || hdr->node_off < hdr->path_off ||
            hdr->kv_off < hdr->node_off ||
            hdr->str_off < hdr->kv_off ||
            hdr->str_off + hdr->str_sz > hdr->size)
                flags |= MAESTRO_VERR_IMAGE;

        if (hdr->mod_off + hdr->mod_nr * sizeof(struct img_mod) > hdr->size ||
            hdr->ext_off + hdr->ext_nr * sizeof(struct img_ext) > hdr->size ||
            hdr->ident_off + hdr->ident_nr * sizeof(struct img_ident) > hdr->size ||
            hdr->path_off + hdr->path_nr * sizeof(struct img_path) > hdr->size ||
            hdr->node_off + hdr->node_nr * sizeof(struct img_node) > hdr->size ||
            hdr->kv_off + hdr->kv_nr * sizeof(struct img_kv) > hdr->size)
                flags |= MAESTRO_VERR_IMAGE;

        if ((hdr->capability & ~dest->vm_cap) != 0)
                flags |= MAESTRO_VERR_CAP;

        exts = (const struct img_ext *)dest->img_exts;

        for (i = 0; i < dest->img_ext_nr; i++) {
                size_t t;
                bool found = false;

                for (t = 0; t < dest->tool_nr; t++) {
                        if (!strcmp(dest->tools[t].name, dest->img_strs + exts[i].name_off)) {
                                found = true;
                                break;
                        }
                }

                if (!found) {
                        flags |= MAESTRO_VERR_TOOL;
                        break;
                }
        }

        if (flags)
                diagf(err, "maestro_validate failed: 0x%llx\n",
                      (unsigned long long)flags);

        return flags;
}

size_t maestro_list_externals(maestro_ctx *ctx, const char ***names) {
        static const char **empty;
        const struct img_ext *exts;
        size_t i;

        if (!ctx || !ctx->img_exts || !names) {
                if (names)
                        *names = empty;

                return 0;
        }

        exts = (const struct img_ext *)ctx->img_exts;

        if (ctx->img_ext_nr && !ctx->ext_names_cache) {
                ctx->ext_names_cache = ctx->alloc(ctx->img_ext_nr *
                                                  sizeof(*ctx->ext_names_cache));

                if (!ctx->ext_names_cache) {
                        *names = empty;
                        return 0;
                }

                for (i = 0; i < ctx->img_ext_nr; i++)
                        ctx->ext_names_cache[i] = ctx->img_strs + exts[i].name_off;
        }

        *names = ctx->ext_names_cache ? ctx->ext_names_cache : empty;
        return ctx->img_ext_nr;
}
