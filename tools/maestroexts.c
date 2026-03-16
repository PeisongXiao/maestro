#include <errno.h>
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

int main(int argc, char **argv) {
        maestro_ctx *ctx;
        const char **names;
        void *buf;
        size_t len;
        size_t nr;
        size_t i;
        int ret;

        if (argc != 2) {
                fprintf(stderr, "usage: %s image.mstro\n", argv[0]);
                return 1;
        }

        buf = slurp(argv[1], &len);

        if (!buf) {
                fprintf(stderr, "load %s: %s\n", argv[1], strerror(errno));
                return 1;
        }

        ctx = maestro_ctx_new();

        if (!ctx) {
                free(buf);
                return 1;
        }

        ret = maestro_load(ctx, buf);

        if (ret) {
                maestro_ctx_free(ctx);
                free(buf);
                return ret;
        }

        maestro_ctx_set_image_len(ctx, len);

        if (maestro_validate(ctx, NULL) & ~MAESTRO_VERR_TOOL) {
                maestro_ctx_free(ctx);
                free(buf);
                return 1;
        }

        nr = maestro_list_externals(ctx, &names);

        for (i = 0; i < nr; i++)
                fprintf(stdout, "%s\n", names[i]);

        maestro_ctx_free(ctx);
        free(buf);
        return 0;
}
