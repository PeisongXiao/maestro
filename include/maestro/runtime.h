#ifndef MAESTRO_RUNTIME_H
#define MAESTRO_RUNTIME_H

#include "maestro/common.h"

#ifdef __cplusplus
extern "C" {
#endif

maestro_ctx *maestro_ctx_new(void);
void maestro_ctx_free(maestro_ctx *ctx);
int maestro_ctx_set_output(maestro_ctx *ctx, maestro_output print_fn,
                           maestro_output log_fn);
int maestro_ctx_set_vm_logger(maestro_ctx *ctx, maestro_output fn);
int maestro_ctx_set_allocator(maestro_ctx *ctx, maestro_alloc_fn alloc,
                              maestro_free_fn dealloc);
void maestro_ctx_set_capability(maestro_ctx *ctx, uint64_t vm_cap);
void maestro_ctx_set_log_flags(maestro_ctx *ctx, uint64_t flags);
int maestro_ctx_add_tool(maestro_ctx *ctx, const char *name, maestro_output fn);

int maestro_load(maestro_ctx *dest, const void *src);
void maestro_ctx_set_image_len(maestro_ctx *ctx, size_t len);
uint64_t maestro_validate(maestro_ctx *dest, FILE *err);
int maestro_run(maestro_ctx *ctx, const char *module_path, maestro_value **args,
                size_t argc,
                maestro_value **result);
size_t maestro_list_externals(maestro_ctx *ctx, const char ***names);

#ifdef __cplusplus
}
#endif

#endif
