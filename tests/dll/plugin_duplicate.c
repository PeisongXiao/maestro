#include "maestro/maestro.h"

static int fn_dup(maestro_ctx *ctx, maestro_value **args, size_t argc,
                  maestro_value **result) {
        (void)ctx;
        (void)args;
        (void)argc;
        (void)result;
        return MAESTRO_ERR_RUNTIME;
}

int maestro_dll_init(maestro_ctx *ctx) {
        return maestro_register_fn(ctx, "dll-int", fn_dup);
}
