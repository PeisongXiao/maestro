#ifndef MAESTRO_COMPILE_H
#define MAESTRO_COMPILE_H

#include "maestro/common.h"

#ifdef __cplusplus
extern "C" {
#endif

maestro_asts *maestro_asts_new(void);
void maestro_asts_free(maestro_asts *asts);

int maestro_parse_list(maestro_asts *dest, FILE *err, const char **srcs,
                       int src_cnt);
int maestro_parse_file(maestro_asts *dest, FILE *err, const char *src);
int maestro_link(FILE *dest, maestro_asts *src);
int maestro_link_ex(FILE *dest, maestro_asts *src, const uint8_t *magic,
                    uint64_t capability);

#ifdef __cplusplus
}
#endif

#endif
