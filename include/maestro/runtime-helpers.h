#ifndef MAESTRO_RUNTIME_HELPERS_H
#define MAESTRO_RUNTIME_HELPERS_H

#include "maestro/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

maestro_value *maestro_value_new_invalid(maestro_ctx *ctx);
maestro_value *maestro_value_new_int(maestro_ctx *ctx, maestro_int_t v);
maestro_value *maestro_value_new_float(maestro_ctx *ctx, maestro_float_t v);
maestro_value *maestro_value_new_bool(maestro_ctx *ctx, bool v);
maestro_value *maestro_value_new_string(maestro_ctx *ctx, const char *s);
maestro_value *maestro_value_new_symbol(maestro_ctx *ctx, const char *s);
maestro_value *maestro_value_new_list(maestro_ctx *ctx);
maestro_value *maestro_value_new_json(maestro_ctx *ctx,
                                      const char *json_snippet);
void maestro_value_free(maestro_ctx *ctx, maestro_value *v);

int maestro_list_push(maestro_ctx *ctx, maestro_value *list, maestro_value *v);

int maestro_value_type(const maestro_value *v);
int maestro_value_as_int(const maestro_value *v, maestro_int_t *out);
int maestro_value_as_float(const maestro_value *v, maestro_float_t *out);
int maestro_value_as_bool(const maestro_value *v, bool *out);
const char *maestro_value_as_string(const maestro_value *v);
const char *maestro_value_as_symbol(const maestro_value *v);
size_t maestro_value_list_len(const maestro_value *v);
maestro_value *maestro_value_list_get(const maestro_value *v, size_t idx);

#ifdef __cplusplus
}
#endif

#endif
