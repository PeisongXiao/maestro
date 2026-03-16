#include "maestro_int.h"

static const char *node_ident(maestro_ast_node *n) {
        if (!n || n->type != MAESTRO_AST_IDENT)
                return NULL;

        return n->text;
}

static char *join_node_path(maestro_ast_node *form, uint32_t start,
                            uint32_t end) {
        char *buf;
        size_t len = 0;
        uint32_t i;

        if (end <= start || end > form->child_nr)
                return NULL;

        for (i = start; i < end; i++) {
                if (!form->child[i]->text)
                        return NULL;

                len += strlen(form->child[i]->text) + (i + 1 < end ? 1U : 0U);
        }

        buf = malloc(len + 1);

        if (!buf)
                return NULL;

        buf[0] = 0;

        for (i = start; i < end; i++) {
                strcat(buf, form->child[i]->text);

                if (i + 1 < end)
                        strcat(buf, ".");
        }

        return buf;
}

static maestro_ast *find_ast(maestro_asts *asts, const char *path) {
        maestro_ast *ast;

        for (ast = asts->head; ast; ast = ast->next) {
                if (!strcmp(ast->module_path, path))
                        return ast;
        }

        return NULL;
}

static int is_exported(maestro_ast *ast, const char *name) {
        bool wildcard = false;
        uint32_t i;

        for (i = 0; i < ast->root->child_nr; i++) {
                maestro_ast_node *f = ast->root->child[i];

                if (f->type != MAESTRO_AST_FORM || f->child_nr < 2)
                        continue;

                if (!node_ident(f->child[0]) || strcmp(node_ident(f->child[0]), "export"))
                        continue;

                if (f->child_nr == 2 && node_ident(f->child[1]) &&
                    !strcmp(node_ident(f->child[1]), "*"))
                        wildcard = true;

                if (f->child_nr == 2 && node_ident(f->child[1]) &&
                    !strcmp(node_ident(f->child[1]), name))
                        return 1;
        }

        if (wildcard)
                return 1;

        return 0;
}

static int ast_has_state(maestro_ast *ast, const char *name) {
        uint32_t i;

        for (i = 0; i < ast->root->child_nr; i++) {
                maestro_ast_node *f = ast->root->child[i];

                if (f->type != MAESTRO_AST_FORM || f->child_nr < 2)
                        continue;

                if (!node_ident(f->child[0]) || strcmp(node_ident(f->child[0]), "state"))
                        continue;

                if (f->child[1]->type != MAESTRO_AST_FORM || f->child[1]->child_nr < 1)
                        continue;

                if (node_ident(f->child[1]->child[0]) &&
                    !strcmp(node_ident(f->child[1]->child[0]), name))
                        return 1;
        }

        return 0;
}

static int validate_imports(FILE *err, maestro_asts *asts,
                            maestro_ast_node *node) {
        uint32_t i;

        if (!node)
                return 0;

        if (node->type == MAESTRO_AST_FORM && node->child_nr >= 1 &&
            node_ident(node->child[0])) {
                const char *op = node_ident(node->child[0]);

                if (!strcmp(op, "import") && node->child_nr >= 3) {
                        char *mod = join_node_path(node, 1, node->child_nr - 1);
                        const char *name = node->child[node->child_nr - 1]->text;
                        maestro_ast *ast = find_ast(asts, mod);

                        if (!ast) {
                                diagf(err, "missing import module %s\n", mod);
                                free(mod);
                                return -1;
                        }

                        if (strcmp(name, "*") && !is_exported(ast, name)) {
                                diagf(err, "import %s from %s is not exported\n", name, mod);
                                free(mod);
                                return -1;
                        }

                        free(mod);
                }

                if (!strcmp(op, "import-program") && node->child_nr >= 2) {
                        char *mod = join_node_path(node, 1, node->child_nr);
                        maestro_ast *ast = find_ast(asts, mod);

                        if (!ast || !ast_has_state(ast, "start")) {
                                diagf(err, "import-program target %s has no start state\n",
                                      mod);
                                free(mod);
                                return -1;
                        }

                        free(mod);
                }
        }

        for (i = 0; i < node->child_nr; i++) {
                if (validate_imports(err, asts, node->child[i]))
                        return -1;
        }

        for (i = 0; i < node->kv_nr; i++) {
                if (validate_imports(err, asts, node->kv[i].value))
                        return -1;
        }

        return 0;
}

static int serialize_node(maestro_ast_node *node, struct strtab *tab,
                          struct ident_vec *idents, struct node_vec *nodes,
                          struct kv_vec *kvs, uint32_t *idx);

static int serialize_node_into(maestro_ast_node *node, struct strtab *tab,
                               struct ident_vec *idents, struct node_vec *nodes,
                               struct kv_vec *kvs, uint32_t idx) {
        struct img_node *out = &nodes->v[idx];
        uint32_t i;

        memset(out, 0, sizeof(*out));

        switch (node->type) {
        case MAESTRO_AST_INT:
                out->type = IMG_NODE_INT;
                out->i = node->i;
                return 0;

        case MAESTRO_AST_FLOAT:
                out->type = IMG_NODE_FLOAT;
                out->f = node->f;
                return 0;

        case MAESTRO_AST_STRING:
                out->type = IMG_NODE_STRING;

                if (strtab_add(tab, node->text, &out->str_off))
                        return -1;

                return 0;

        case MAESTRO_AST_IDENT:
                out->type = IMG_NODE_IDENT;

                if (ident_intern(tab, idents, node->text, &out->str_off))
                        return -1;

                return 0;

        case MAESTRO_AST_SYMBOL:
                out->type = IMG_NODE_SYMBOL;

                if (ident_intern(tab, idents, node->text, &out->str_off))
                        return -1;

                return 0;

        case MAESTRO_AST_FORM: {
                uint32_t base;

                out->type = IMG_NODE_FORM;
                out->nr = node->child_nr;

                if (node_vec_reserve(nodes, node->child_nr, &base))
                        return -1;

                out->first = base;

                for (i = 0; i < node->child_nr; i++) {
                        if (serialize_node_into(node->child[i], tab, idents, nodes, kvs, base + i))
                                return -1;
                }

                return 0;
        }

        case MAESTRO_AST_JSON: {
                uint32_t base, kv_idx;

                out->type = IMG_NODE_JSON;
                out->nr = node->kv_nr;
                base = (uint32_t)kvs->nr;
                out->first = base;

                for (i = 0; i < node->kv_nr; i++) {
                        struct img_kv kv = {0};
                        uint32_t val_idx;

                        if (strtab_add(tab, node->kv[i].key, &kv.key_off))
                                return -1;

                        if (serialize_node(node->kv[i].value, tab, idents, nodes, kvs, &val_idx))
                                return -1;

                        kv.val_idx = val_idx;

                        if (kv_vec_push(kvs, kv, &kv_idx))
                                return -1;
                }

                return 0;
        }

        default:
                return -1;
        }
}

static int serialize_node(maestro_ast_node *node, struct strtab *tab,
                          struct ident_vec *idents, struct node_vec *nodes,
                          struct kv_vec *kvs, uint32_t *idx) {
        if (node_vec_reserve(nodes, 1, idx))
                return -1;

        return serialize_node_into(node, tab, idents, nodes, kvs, *idx);
}

static int collect_externals(maestro_ast *ast, uint32_t mod_idx,
                             struct strtab *tab,
                             struct ident_vec *idents, struct ext_vec *exts) {
        uint32_t i;

        for (i = 0; i < ast->root->child_nr; i++) {
                maestro_ast_node *f = ast->root->child[i];
                maestro_ast_node *sig;
                maestro_ast_node *body;
                struct img_ext ext = {0};
                uint32_t idx;

                if (f->type != MAESTRO_AST_FORM || f->child_nr < 3)
                        continue;

                if (!node_ident(f->child[0]) || strcmp(node_ident(f->child[0]), "define"))
                        continue;

                sig = f->child[1];
                body = f->child[2];

                if (sig->type != MAESTRO_AST_FORM || sig->child_nr < 1)
                        continue;

                if (body->type != MAESTRO_AST_IDENT || strcmp(body->text, "external"))
                        continue;

                if (!node_ident(sig->child[0]))
                        continue;

                if (ident_intern(tab, idents, node_ident(sig->child[0]), &ext.ident_id))
                        return -1;

                if (strtab_add(tab, node_ident(sig->child[0]), &ext.name_off))
                        return -1;

                ext.mod_idx = mod_idx;
                ext.def_idx = i;

                if (ext_vec_push(exts, ext, &idx))
                        return -1;
        }

        return 0;
}

int maestro_link_ex(FILE *dest, maestro_asts *src, const uint8_t *magic,
                    uint64_t capability) {
        struct img_hdr hdr;
        struct strtab tab = {0};
        struct ident_vec idents = {0};
        struct ext_vec exts = {0};
        struct node_vec nodes = {0};
        struct kv_vec kvs = {0};
        struct mod_vec mods = {0};
        maestro_ast *ast;
        uint32_t off;
        size_t total;

        for (ast = src->head; ast; ast = ast->next) {
                struct img_mod mod = {0};

                if (validate_imports(stderr, src, ast->root))
                        return MAESTRO_ERR_LINK;

                if (strtab_add(&tab, ast->module_path, &mod.path_str))
                        return MAESTRO_ERR_NOMEM;

                if (strtab_add(&tab, ast->src ? ast->src : ast->module_path, &mod.src_str))
                        return MAESTRO_ERR_NOMEM;

                if (serialize_node(ast->root, &tab, &idents, &nodes, &kvs, &mod.root_idx))
                        return MAESTRO_ERR_NOMEM;

                if (mod_vec_push(&mods, mod))
                        return MAESTRO_ERR_NOMEM;
        }

        for (ast = src->head, off = 0; ast; ast = ast->next, off++) {
                if (collect_externals(ast, off, &tab, &idents, &exts))
                        return MAESTRO_ERR_NOMEM;
        }

        memset(&hdr, 0, sizeof(hdr));
        memcpy(hdr.magic, magic ? magic : MAESTRO_DEFAULT_MAGIC,
               sizeof(hdr.magic));
        hdr.version = MAESTRO_VERSION;
        hdr.capability = capability;
        hdr.mod_off = sizeof(hdr);
        hdr.mod_nr = (uint32_t)mods.nr;
        hdr.ext_off = hdr.mod_off + (uint32_t)(mods.nr * sizeof(mods.v[0]));
        hdr.ext_nr = (uint32_t)exts.nr;
        hdr.ident_off = hdr.ext_off + (uint32_t)(exts.nr * sizeof(exts.v[0]));
        hdr.ident_nr = (uint32_t)idents.nr;
        hdr.node_off = hdr.ident_off + (uint32_t)(idents.nr * sizeof(idents.v[0]));
        hdr.node_nr = (uint32_t)nodes.nr;
        hdr.kv_off = hdr.node_off + (uint32_t)(nodes.nr * sizeof(nodes.v[0]));
        hdr.kv_nr = (uint32_t)kvs.nr;
        hdr.str_off = hdr.kv_off + (uint32_t)(kvs.nr * sizeof(kvs.v[0]));
        hdr.str_sz = (uint32_t)tab.len;
        total = hdr.str_off + tab.len;
        hdr.size = (uint32_t)total;

        if (fwrite(&hdr, 1, sizeof(hdr), dest) != sizeof(hdr))
                return MAESTRO_ERR_LINK;

        if (mods.nr && fwrite(mods.v, sizeof(mods.v[0]), mods.nr, dest) != mods.nr)
                return MAESTRO_ERR_LINK;

        if (exts.nr && fwrite(exts.v, sizeof(exts.v[0]), exts.nr, dest) != exts.nr)
                return MAESTRO_ERR_LINK;

        if (idents.nr &&
            fwrite(idents.v, sizeof(idents.v[0]), idents.nr, dest) != idents.nr)
                return MAESTRO_ERR_LINK;

        if (nodes.nr && fwrite(nodes.v, sizeof(nodes.v[0]), nodes.nr, dest) != nodes.nr)
                return MAESTRO_ERR_LINK;

        if (kvs.nr && fwrite(kvs.v, sizeof(kvs.v[0]), kvs.nr, dest) != kvs.nr)
                return MAESTRO_ERR_LINK;

        if (tab.len && fwrite(tab.buf, 1, tab.len, dest) != tab.len)
                return MAESTRO_ERR_LINK;

        fflush(dest);
        free(tab.buf);
        free(idents.v);
        free(exts.v);
        free(nodes.v);
        free(kvs.v);
        free(mods.v);
        off = 0;
        (void)off;
        return 0;
}

int maestro_link(FILE *dest, maestro_asts *src) {
        return maestro_link_ex(dest, src, NULL, 0);
}
