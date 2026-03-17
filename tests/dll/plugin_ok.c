#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "maestro/maestro.h"

static int value_list_push_take(maestro_ctx *ctx, maestro_value *list,
                                maestro_value *item) {
        int rc;

        if (!item)
                return MAESTRO_ERR_NOMEM;

        rc = maestro_list_push(ctx, list, item);
        maestro_value_free(ctx, item);
        return rc;
}

static int fn_echo(maestro_ctx *ctx, maestro_value **args, size_t argc,
                   maestro_value **result) {
        const char *msg;

        if (!ctx || !args || !result || argc != 1)
                return MAESTRO_ERR_RUNTIME;

        msg = maestro_value_as_string(args[0]);

        if (!msg)
                return MAESTRO_ERR_RUNTIME;

        *result = maestro_value_new_int(ctx, (maestro_int_t)strlen(msg));
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_dll_int(maestro_ctx *ctx, maestro_value **args, size_t argc,
                      maestro_value **result) {
        (void)args;

        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;

        *result = maestro_value_new_int(ctx, 7);
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_dll_float(maestro_ctx *ctx, maestro_value **args, size_t argc,
                        maestro_value **result) {
        (void)args;

        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;

        *result = maestro_value_new_float(ctx, 2.5f);
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_dll_bool(maestro_ctx *ctx, maestro_value **args, size_t argc,
                       maestro_value **result) {
        (void)args;

        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;

        *result = maestro_value_new_bool(ctx, true);
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_dll_string(maestro_ctx *ctx, maestro_value **args, size_t argc,
                         maestro_value **result) {
        (void)args;

        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;

        *result = maestro_value_new_string(ctx, "host");
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_dll_symbol(maestro_ctx *ctx, maestro_value **args, size_t argc,
                         maestro_value **result) {
        (void)args;

        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;

        *result = maestro_value_new_symbol(ctx, "token");
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_dll_list(maestro_ctx *ctx, maestro_value **args, size_t argc,
                       maestro_value **result) {
        maestro_value *list;
        maestro_value *nested;

        (void)args;

        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;

        list = maestro_value_new_list(ctx);
        nested = maestro_value_new_list(ctx);

        if (!list || !nested) {
                if (list)
                        maestro_value_free(ctx, list);

                if (nested)
                        maestro_value_free(ctx, nested);

                return MAESTRO_ERR_NOMEM;
        }

        if (value_list_push_take(ctx, nested, maestro_value_new_int(ctx, 1)) ||
            value_list_push_take(ctx, nested, maestro_value_new_string(ctx, "nest")) ||
            value_list_push_take(ctx, list, maestro_value_new_int(ctx, 7)) ||
            value_list_push_take(ctx, list, maestro_value_new_float(ctx, 2.5f)) ||
            value_list_push_take(ctx, list, maestro_value_new_bool(ctx, true)) ||
            value_list_push_take(ctx, list, maestro_value_new_string(ctx, "Ada")) ||
            value_list_push_take(ctx, list, maestro_value_new_symbol(ctx, "token")) ||
            maestro_list_push(ctx, list, nested)) {
                maestro_value_free(ctx, nested);
                maestro_value_free(ctx, list);
                return MAESTRO_ERR_NOMEM;
        }

        maestro_value_free(ctx, nested);
        *result = list;
        return 0;
}

static int fn_dll_object(maestro_ctx *ctx, maestro_value **args, size_t argc,
                         maestro_value **result) {
        (void)args;

        if (!ctx || !result || argc != 0)
                return MAESTRO_ERR_RUNTIME;

        *result = maestro_value_new_json(
                          ctx,
                          "{\"user\":{\"name\":\"Ada\",\"meta\":{\"active\":\"yes\"}},\"scores\":[1,2,3]}");
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

static int fn_dll_describe(maestro_ctx *ctx, maestro_value **args, size_t argc,
                           maestro_value **result) {
        maestro_int_t i;
        maestro_float_t f;
        const char *s;
        const char *sym;
        const char *obj;
        char buf[256];

        if (!ctx || !args || !result || argc != 5)
                return MAESTRO_ERR_RUNTIME;

        if (maestro_value_as_int(args[0], &i) || i != 42)
                return MAESTRO_ERR_RUNTIME;

        if (maestro_value_as_float(args[1], &f) || f != 2.5f)
                return MAESTRO_ERR_RUNTIME;

        s = maestro_value_as_string(args[2]);
        sym = maestro_value_as_symbol(args[3]);
        obj = maestro_value_as_string(args[4]);

        if (!s || strcmp(s, "hi") || !sym || strcmp(sym, "sym") || !obj ||
            strcmp(obj, "{\"user\":{\"name\":\"Ada\"}}"))
                return MAESTRO_ERR_RUNTIME;

        snprintf(buf, sizeof(buf), "%lld|%g|%s|%s|%s", (long long)i, (double)f, s,
                 sym, obj);
        *result = maestro_value_new_string(ctx, buf);
        return *result ? 0 : MAESTRO_ERR_NOMEM;
}

int maestro_dll_init(maestro_ctx *ctx) {
        if (!ctx)
                return MAESTRO_ERR_RUNTIME;

        if (maestro_register_fn(ctx, "echo", fn_echo) ||
            maestro_register_fn(ctx, "dll-int", fn_dll_int) ||
            maestro_register_fn(ctx, "dll-float", fn_dll_float) ||
            maestro_register_fn(ctx, "dll-bool", fn_dll_bool) ||
            maestro_register_fn(ctx, "dll-string", fn_dll_string) ||
            maestro_register_fn(ctx, "dll-symbol", fn_dll_symbol) ||
            maestro_register_fn(ctx, "dll-list", fn_dll_list) ||
            maestro_register_fn(ctx, "dll-object", fn_dll_object) ||
            maestro_register_fn(ctx, "dll-describe", fn_dll_describe))
                return MAESTRO_ERR_RUNTIME;

        return 0;
}
