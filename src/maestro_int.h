#ifndef MAESTRO_INT_H
#define MAESTRO_INT_H

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "maestro/maestro.h"

enum {
        CTRL_OK = 0,
        CTRL_TRANSITION = 1,
        CTRL_END = 2,
};

enum {
        HANDLE_MACRO = 1,
        HANDLE_STATE = 2,
        HANDLE_PROGRAM = 3,
        HANDLE_BUILTIN = 4,
};

enum {
        REF_BINDING = 1,
        REF_PATH = 2,
};

enum {
        IMG_NODE_INT = 1,
        IMG_NODE_FLOAT = 2,
        IMG_NODE_STRING = 3,
        IMG_NODE_IDENT = 4,
        IMG_NODE_SYMBOL = 5,
        IMG_NODE_FORM = 6,
        IMG_NODE_JSON = 7,
};

struct maestro_ast_kv {
        char *key;
        maestro_ast_node *value;
};

struct maestro_ast_node {
        uint32_t type;
        uint32_t line;
        uint32_t col;
        char *text;
        maestro_int_t i;
        maestro_float_t f;
        maestro_ast_node **child;
        uint32_t child_nr;
        uint32_t child_cap;
        struct maestro_ast_kv *kv;
        uint32_t kv_nr;
        uint32_t kv_cap;
};

struct maestro_ast {
        char *src;
        char **module_seg;
        uint32_t module_nr;
        maestro_ast_node *root;
        struct maestro_ast *next;
};

struct maestro_asts {
        maestro_ast *head;
        maestro_ast *tail;
        size_t nr;
};

struct maestro_value {
        uint32_t type;
        uint32_t flags;
        union {
                maestro_int_t i;
                maestro_float_t f;
                char *s;
                bool b;
                struct maestro_list *list;
                struct maestro_object *obj;
                struct maestro_ref *ref;
                struct maestro_handle *handle;
                void *ptr;
        } v;
};

struct maestro_fn_binding {
        char *name;
        maestro_fn fn;
};

struct maestro_ctx {
        const void *image;
        size_t image_len;
        const void *img_hdr;
        const void *img_mods;
        const void *img_exts;
        const void *img_idents;
        const void *img_paths;
        const void *img_nodes;
        const void *img_kv;
        const char *img_strs;
        uint32_t img_mod_nr;
        uint32_t img_ext_nr;
        uint32_t img_ident_nr;
        uint32_t img_path_nr;
        uint32_t img_node_nr;
        uint32_t img_kv_nr;
        uint64_t vm_cap;
        uint64_t log_flags;
        maestro_output log;
        maestro_output print;
        maestro_output vm_logger;
        maestro_alloc_fn alloc;
        maestro_free_fn dealloc;
        struct maestro_fn_binding *fns;
        size_t fn_nr;
        size_t fn_cap;
        const char **ext_names_cache;
        void *priv;
};

struct maestro_list {
        size_t nr;
        size_t cap;
        maestro_value *item;
};

struct maestro_object {
        size_t nr;
        size_t cap;
        char **key;
        maestro_value *val;
};

struct maestro_binding;

struct maestro_ref {
        int kind;
        struct maestro_binding *binding;
        maestro_value *root;
        char **path;
        size_t path_nr;
};

struct maestro_handle {
        int kind;
        uint32_t module_idx;
        uint32_t name_id;
        uint32_t node_idx;
        uint32_t body_idx;
        uint32_t argc;
        uint32_t *argv_id;
        uint8_t *arg_ref;
};

struct maestro_binding {
        uint32_t ident_id;
        maestro_value value;
};

struct maestro_scope {
        struct maestro_scope *up;
        size_t nr;
        size_t cap;
        struct maestro_binding *bind;
};

struct img_hdr {
        uint8_t magic[32];
        uint32_t version;
        uint32_t size;
        uint64_t capability;
        uint32_t mod_off;
        uint32_t mod_nr;
        uint32_t ext_off;
        uint32_t ext_nr;
        uint32_t ident_off;
        uint32_t ident_nr;
        uint32_t path_off;
        uint32_t path_nr;
        uint32_t node_off;
        uint32_t node_nr;
        uint32_t kv_off;
        uint32_t kv_nr;
        uint32_t str_off;
        uint32_t str_sz;
};

struct img_mod {
        uint32_t path_first;
        uint32_t path_nr;
        uint32_t src_str;
        uint32_t root_idx;
};

struct img_ext {
        uint32_t name_off;
        uint32_t ident_id;
        uint32_t mod_idx;
        uint32_t def_idx;
};

struct img_ident {
        uint32_t name_off;
};

struct img_path {
        uint32_t ident_id;
};

struct module_scope {
        struct img_mod *mod;
        struct maestro_scope *globals;
        struct module_scope *next;
};

struct img_node {
        uint32_t type;
        uint32_t str_off;
        uint32_t first;
        uint32_t nr;
        int64_t i;
        float f;
        uint32_t pad;
};

struct img_kv {
        uint32_t key_off;
        uint32_t val_idx;
};

struct node_vec {
        struct img_node *v;
        size_t nr;
        size_t cap;
};

struct kv_vec {
        struct img_kv *v;
        size_t nr;
        size_t cap;
};

struct mod_vec {
        struct img_mod *v;
        size_t nr;
        size_t cap;
};

struct ext_vec {
        struct img_ext *v;
        size_t nr;
        size_t cap;
};

struct ident_vec {
        struct img_ident *v;
        size_t nr;
        size_t cap;
};

struct path_vec {
        struct img_path *v;
        size_t nr;
        size_t cap;
};

struct strtab {
        char *buf;
        size_t len;
        size_t cap;
};

struct text {
        const char *buf;
        size_t len;
        size_t pos;
        uint32_t line;
        uint32_t col;
};

struct eval_res {
        int ctrl;
        maestro_value v;
        struct maestro_handle *next_state;
};

struct run_ctx {
        maestro_ctx *ctx;
        struct img_hdr *hdr;
        struct img_mod *mods;
        struct img_node *nodes;
        struct img_kv *kvs;
        const char *strs;
        maestro_value last_state;
        struct module_scope *mods_cache;
};

struct resolve_frame {
        uint32_t mod_idx;
        const char *name;
        struct resolve_frame *up;
};

void diagf(FILE *err, const char *fmt, ...);
void *xrealloc(maestro_ctx *ctx, void *ptr, size_t size);
char *xstrdup(const char *s);
int strtab_add(struct strtab *tab, const char *s, uint32_t *off);
int node_vec_reserve(struct node_vec *vec, uint32_t nr, uint32_t *idx);
int kv_vec_push(struct kv_vec *vec, struct img_kv kv, uint32_t *idx);
int mod_vec_push(struct mod_vec *vec, struct img_mod mod);
int ext_vec_push(struct ext_vec *vec, struct img_ext ext, uint32_t *idx);
int ident_intern(struct strtab *tab, struct ident_vec *idents, const char *s,
                 uint32_t *id);
int path_vec_push(struct path_vec *vec, struct img_path path, uint32_t *idx);

#endif
