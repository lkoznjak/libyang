/**
 * @file path.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief Path functions
 *
 * Copyright (c) 2020 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */
#define _ISOC99_SOURCE /* strtoull */

#include "path.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>

#include "common.h"
#include "log.h"
#include "plugins_types.h"
#include "tree_data_internal.h"
#include "tree_schema_internal.h"
#include "xpath.h"

/**
 * @brief Check predicate syntax.
 *
 * @param[in] ctx libyang context.
 * @param[in] exp Parsed predicate.
 * @param[in,out] tok_idx Index in @p exp, is adjusted.
 * @param[in] prefix Prefix option.
 * @param[in] pred Predicate option.
 * @return LY_ERR value.
 */
static LY_ERR
ly_path_check_predicate(const struct ly_ctx *ctx, const struct lyxp_expr *exp, uint16_t *tok_idx, uint8_t prefix,
                        uint8_t pred)
{
    struct ly_set *set = NULL;
    uint32_t i;
    const char *name;
    size_t name_len;

    /* '[' */
    if (!lyxp_next_token(NULL, exp, tok_idx, LYXP_TOKEN_BRACK1)) {
        if (((pred == LY_PATH_PRED_SIMPLE) || (pred == LY_PATH_PRED_KEYS))
                && !lyxp_check_token(NULL, exp, *tok_idx, LYXP_TOKEN_NAMETEST)) {
            set = ly_set_new();
            LY_CHECK_ERR_GOTO(!set, LOGMEM(ctx), error);

            do {
                /* NameTest is always expected here */
                LY_CHECK_GOTO(lyxp_check_token(ctx, exp, *tok_idx, LYXP_TOKEN_NAMETEST), error);

                /* check prefix based on the options */
                name = strnstr(exp->expr + exp->tok_pos[*tok_idx], ":", exp->tok_len[*tok_idx]);
                if ((prefix == LY_PATH_PREFIX_MANDATORY) && !name) {
                    LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Prefix missing for \"%.*s\" in path.", exp->tok_len[*tok_idx],
                           exp->expr + exp->tok_pos[*tok_idx]);
                    goto error;
                }
                if (!name) {
                    name = exp->expr + exp->tok_pos[*tok_idx];
                    name_len = exp->tok_len[*tok_idx];
                } else {
                    ++name;
                    name_len = exp->tok_len[*tok_idx] - (name - (exp->expr + exp->tok_pos[*tok_idx]));
                }

                /* check whether it was not already specified */
                for (i = 0; i < set->count; ++i) {
                    /* all the keys must be from the same module so this comparison should be fine */
                    if (!strncmp(set->objs[i], name, name_len) && !isalpha(((char *)set->objs[i])[name_len])) {
                        LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Duplicate predicate key \"%.*s\" in path.",
                               name_len, name);
                        goto error;
                    }
                }

                /* add it into the set */
                ly_set_add(set, (void *)name, LY_SET_OPT_USEASLIST);

                /* NameTest */
                ++(*tok_idx);

                /* '=' */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_OPER_EQUAL), error);

                /* Literal */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_LITERAL), error);

                /* ']' */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_BRACK2), error);

            /* '[' */
            } while (!lyxp_next_token(NULL, exp, tok_idx, LYXP_TOKEN_BRACK1));

        /* '.' */
        } else if ((pred == LY_PATH_PRED_SIMPLE) && !lyxp_next_token(NULL, exp, tok_idx, LYXP_TOKEN_DOT)) {
            /* '=' */
            LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_OPER_EQUAL), error);

            /* Literal */
            LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_LITERAL), error);

            /* ']' */
            LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_BRACK2), error);

        /* Number */
        } else if ((pred == LY_PATH_PRED_SIMPLE) && !lyxp_next_token(NULL, exp, tok_idx, LYXP_TOKEN_NUMBER)) {

            /* ']' */
            LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_BRACK2), error);

        } else if ((pred == LY_PATH_PRED_LEAFREF) && !lyxp_check_token(NULL, exp, *tok_idx, LYXP_TOKEN_NAMETEST)) {
            assert(prefix == LY_PATH_PREFIX_OPTIONAL);
            set = ly_set_new();
            LY_CHECK_ERR_GOTO(!set, LOGMEM(ctx), error);

            do {
                /* NameTest is always expected here */
                LY_CHECK_GOTO(lyxp_check_token(ctx, exp, *tok_idx, LYXP_TOKEN_NAMETEST), error);

                name = strnstr(exp->expr + exp->tok_pos[*tok_idx], ":", exp->tok_len[*tok_idx]);
                if (!name) {
                    name = exp->expr + exp->tok_pos[*tok_idx];
                    name_len = exp->tok_len[*tok_idx];
                } else {
                    ++name;
                    name_len = exp->tok_len[*tok_idx] - (name - (exp->expr + exp->tok_pos[*tok_idx]));
                }

                /* check whether it was not already specified */
                for (i = 0; i < set->count; ++i) {
                    /* all the keys must be from the same module so this comparison should be fine */
                    if (!strncmp(set->objs[i], name, name_len) && !isalpha(((char *)set->objs[i])[name_len])) {
                        LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Duplicate predicate key \"%.*s\" in path.",
                               name_len, name);
                        goto error;
                    }
                }

                /* add it into the set */
                ly_set_add(set, (void *)name, LY_SET_OPT_USEASLIST);

                /* NameTest */
                ++(*tok_idx);

                /* '=' */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_OPER_EQUAL), error);

                /* FuncName */
                LY_CHECK_GOTO(lyxp_check_token(ctx, exp, *tok_idx, LYXP_TOKEN_FUNCNAME), error);
                if ((exp->tok_len[*tok_idx] != 7) || strncmp(exp->expr + exp->tok_pos[*tok_idx], "current", 7)) {
                    LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Invalid function \"%.*s\" invocation in path.",
                        exp->tok_len[*tok_idx], exp->expr + exp->tok_pos[*tok_idx]);
                    goto error;
                }
                ++(*tok_idx);

                /* '(' */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_PAR1), error);

                /* ')' */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_PAR2), error);

                /* '/' */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_OPER_PATH), error);

                /* '..' */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_DDOT), error);
                do {
                    /* '/' */
                    LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_OPER_PATH), error);
                } while (!lyxp_next_token(NULL, exp, tok_idx, LYXP_TOKEN_DDOT));

                /* NameTest */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_NAMETEST), error);

                /* '/' */
                while (!lyxp_next_token(NULL, exp, tok_idx, LYXP_TOKEN_OPER_PATH)) {
                    /* NameTest */
                    LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_NAMETEST), error);
                }

                /* ']' */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, tok_idx, LYXP_TOKEN_BRACK2), error);

            /* '[' */
            } while (!lyxp_next_token(NULL, exp, tok_idx, LYXP_TOKEN_BRACK1));

        } else {
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LY_VCODE_XP_INTOK, lyxp_print_token(exp->tokens[*tok_idx]),
                   exp->expr + exp->tok_pos[*tok_idx]);
            goto error;
        }
    }

    ly_set_free(set, NULL);
    return LY_SUCCESS;

error:
    ly_set_free(set, NULL);
    return LY_EVALID;
}

LY_ERR
ly_path_parse(const struct ly_ctx *ctx, const char *str_path, size_t path_len, uint8_t begin, uint8_t lref,
              uint8_t prefix, uint8_t pred, struct lyxp_expr **expr)
{
    struct lyxp_expr *exp;
    uint16_t tok_idx;

    assert((begin == LY_PATH_BEGIN_ABSOLUTE) || (begin == LY_PATH_BEGIN_EITHER));
    assert((lref == LY_PATH_LREF_TRUE) || (lref == LY_PATH_LREF_FALSE));
    assert((prefix == LY_PATH_PREFIX_OPTIONAL) || (prefix == LY_PATH_PREFIX_MANDATORY));
    assert((pred == LY_PATH_PRED_KEYS) || (pred == LY_PATH_PRED_SIMPLE) || (pred == LY_PATH_PRED_LEAFREF));

    /* parse as a generic XPath expression */
    exp = lyxp_expr_parse(ctx, str_path, path_len, 1);
    LY_CHECK_GOTO(!exp, error);
    tok_idx = 0;

    if (begin == LY_PATH_BEGIN_EITHER) {
        /* is the path relative? */
        if (lyxp_next_token(NULL, exp, &tok_idx, LYXP_TOKEN_OPER_PATH)) {
            /* '..' */
            while ((lref == LY_PATH_LREF_TRUE) && !lyxp_next_token(NULL, exp, &tok_idx, LYXP_TOKEN_DDOT)) {
                /* '/' */
                LY_CHECK_GOTO(lyxp_next_token(ctx, exp, &tok_idx, LYXP_TOKEN_OPER_PATH), error);
            }
        }
    } else {
        /* '/' */
        LY_CHECK_GOTO(lyxp_next_token(ctx, exp, &tok_idx, LYXP_TOKEN_OPER_PATH), error);
    }

    do {
        /* NameTest */
        LY_CHECK_GOTO(lyxp_check_token(ctx, exp, tok_idx, LYXP_TOKEN_NAMETEST), error);

        /* check prefix based on the options */
        if ((prefix == LY_PATH_PREFIX_MANDATORY) && !strnstr(exp->expr + exp->tok_pos[tok_idx], ":", exp->tok_len[tok_idx])) {
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Prefix missing for \"%.*s\" in path.", exp->tok_len[tok_idx],
                   exp->expr + exp->tok_pos[tok_idx]);
            goto error;
        }

        ++tok_idx;

        /* Predicate* */
        LY_CHECK_GOTO(ly_path_check_predicate(ctx, exp, &tok_idx, prefix, pred), error);

    /* '/' */
    } while (!lyxp_next_token(NULL, exp, &tok_idx, LYXP_TOKEN_OPER_PATH));

    /* trailing token check */
    if (exp->used > tok_idx) {
        LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Unparsed characters \"%s\" left at the end of path.",
               exp->expr + exp->tok_pos[tok_idx]);
        goto error;
    }

    *expr = exp;
    return LY_SUCCESS;

error:
    lyxp_expr_free(ctx, exp);
    return LY_EINVAL;
}

LY_ERR
ly_path_parse_predicate(const struct ly_ctx *ctx, const char *str_path, size_t path_len, uint8_t prefix, uint8_t pred,
                        struct lyxp_expr **expr)
{
    struct lyxp_expr *exp;
    uint16_t tok_idx;

    assert((prefix == LY_PATH_PREFIX_OPTIONAL) || (prefix == LY_PATH_PREFIX_MANDATORY));
    assert((pred == LY_PATH_PRED_KEYS) || (pred == LY_PATH_PRED_SIMPLE) || (pred == LY_PATH_PRED_LEAFREF));

    /* parse as a generic XPath expression */
    exp = lyxp_expr_parse(ctx, str_path, path_len, 0);
    LY_CHECK_GOTO(!exp, error);
    tok_idx = 0;

    LY_CHECK_GOTO(ly_path_check_predicate(ctx, exp, &tok_idx, prefix, pred), error);

    /* trailing token check */
    if (exp->used > tok_idx) {
        LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Unparsed characters \"%s\" left at the end of predicate.",
               exp->expr + exp->tok_pos[tok_idx]);
        goto error;
    }

    *expr = exp;
    return LY_SUCCESS;

error:
    lyxp_expr_free(ctx, exp);
    return LY_EINVAL;
}

/**
 * @brief Parse prefix from a NameTest, if any, and node name, and return expected module of the node.
 *
 * @param[in] ctx libyang context.
 * @param[in] cur_mod Module of the current (original context) node. Needed for ::LYD_SCHEMA.
 * @param[in] prev_ctx_node Previous context node. Needed for ::LYD_JSON.
 * @param[in] expr Parsed path.
 * @param[in] tok_idx Index in @p expr.
 * @param[in] lref Lref option.
 * @param[in] resolve_prefix Callback for prefix resolution.
 * @param[in] prefix_data Data for @p resolve_prefix.
 * @param[in] format Format of the path.
 * @param[out] mod Resolved module.
 * @param[out] name Parsed node name.
 * @param[out] name_len Length of @p name.
 * @return LY_ERR value.
 */
static LY_ERR
ly_path_compile_prefix(const struct ly_ctx *ctx, const struct lys_module *cur_mod, const struct lysc_node *prev_ctx_node,
                       const struct lyxp_expr *expr, uint16_t tok_idx, uint8_t lref, ly_clb_resolve_prefix resolve_prefix,
                       void *prefix_data, LYD_FORMAT format, const struct lys_module **mod, const char **name, size_t *name_len)
{
    const char *ptr;
    size_t len;

    assert(expr->tokens[tok_idx] == LYXP_TOKEN_NAMETEST);

    /* get prefix */
    ptr = strnstr(expr->expr + expr->tok_pos[tok_idx], ":", expr->tok_len[tok_idx]);
    len = ptr ? ptr - (expr->expr + expr->tok_pos[tok_idx]) : 0;

    /* find next node module */
    if (ptr) {
        *mod = resolve_prefix(ctx, expr->expr + expr->tok_pos[tok_idx], len, prefix_data);
        if (!*mod) {
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Prefix \"%.*s\" not found of a module in path.",
                   len, expr->expr + expr->tok_pos[tok_idx]);
            return LY_EINVAL;
        } else if (!(*mod)->implemented) {
            if (lref == LY_PATH_LREF_FALSE) {
                LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Not implemented module \"%s\" in path.", (*mod)->name);
                return LY_EINVAL;
            }
            lys_set_implemented_internal((struct lys_module *)*mod, 2);
        }
    } else {
        switch (format) {
        case LYD_SCHEMA:
            *mod = cur_mod;
            break;
        case LYD_JSON:
            if (!prev_ctx_node) {
                LOGINT_RET(ctx);
            }
            *mod = prev_ctx_node->module;
            break;
        case LYD_XML:
            /* not really defined */
            LOGINT_RET(ctx);
        }
    }

    /* set name */
    if (ptr) {
        *name = ptr + 1;
        *name_len = expr->tok_len[tok_idx] - len - 1;
    } else {
        *name = expr->expr + expr->tok_pos[tok_idx];
        *name_len = expr->tok_len[tok_idx];
    }

    return LY_SUCCESS;
}

LY_ERR
ly_path_compile_predicate(const struct ly_ctx *ctx, const struct lys_module *cur_mod, const struct lysc_node *ctx_node,
                          const struct lyxp_expr *expr, uint16_t *tok_idx, ly_clb_resolve_prefix resolve_prefix,
                          void *prefix_data, LYD_FORMAT format, struct ly_path_predicate **predicates,
                          enum ly_path_pred_type *pred_type)
{
    struct ly_path_predicate *p;
    const struct lysc_node *key;
    const struct lys_module *mod;
    const char *name;
    size_t name_len, key_count;

    assert(ctx && ctx_node);

    *pred_type = 0;

    /* '[' */
    if (lyxp_next_token(NULL, expr, tok_idx, LYXP_TOKEN_BRACK1)) {
        /* no predicate */
        return LY_SUCCESS;
    }

    if (expr->tokens[*tok_idx] == LYXP_TOKEN_NAMETEST) {
        if (ctx_node->nodetype != LYS_LIST) {
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "List predicate defined for %s \"%s\" in path.",
                   lys_nodetype2str(ctx_node->nodetype), ctx_node->name);
            return LY_EINVAL;
        } else if (ctx_node->flags & LYS_KEYLESS) {
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "List predicate defined for keyless %s \"%s\" in path.",
                   lys_nodetype2str(ctx_node->nodetype), ctx_node->name);
            return LY_EINVAL;
        }

        do {
            /* NameTest, find the key */
            LY_CHECK_RET(ly_path_compile_prefix(ctx, cur_mod, ctx_node, expr, *tok_idx, LY_PATH_LREF_FALSE, resolve_prefix,
                                                prefix_data, format, &mod, &name, &name_len));
            key = lys_find_child(ctx_node, mod, name, name_len, 0, LYS_GETNEXT_NOSTATECHECK);
            if (!key) {
                LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Not found node \"%.*s\" in path.", name_len, name);
                return LY_EINVAL;
            } else if ((key->nodetype != LYS_LEAF) || !(key->flags & LYS_KEY)) {
                LOGVAL(ctx, LY_VLOG_LYSC, key, LYVE_XPATH, "Key expected instead of %s \"%s\" in path.",
                       lys_nodetype2str(key->nodetype), key->name);
                return LY_EINVAL;
            }
            ++(*tok_idx);

            /* new predicate */
            if (!*pred_type) {
                *pred_type = LY_PATH_PREDTYPE_LIST;
            }
            assert(*pred_type == LY_PATH_PREDTYPE_LIST);
            LY_ARRAY_NEW_RET(ctx, *predicates, p, LY_EMEM);
            p->key = key;

            /* '=' */
            assert(expr->tokens[*tok_idx] == LYXP_TOKEN_OPER_EQUAL);
            ++(*tok_idx);

            /* Literal */
            assert(expr->tokens[*tok_idx] == LYXP_TOKEN_LITERAL);
            LY_CHECK_RET(lyd_value_store(&p->value, key, expr->expr + expr->tok_pos[*tok_idx] + 1,
                                         expr->tok_len[*tok_idx] - 2, NULL, resolve_prefix, prefix_data, format));
            ++(*tok_idx);

            /* ']' */
            assert(expr->tokens[*tok_idx] == LYXP_TOKEN_BRACK2);
            ++(*tok_idx);

            /* another predicate follows? */
        } while (!lyxp_next_token(NULL, expr, tok_idx, LYXP_TOKEN_BRACK1));

        /* check that all keys were set */
        key_count = 0;
        for (key = lysc_node_children(ctx_node, 0); key && (key->flags & LYS_KEY); key = key->next) {
            ++key_count;
        }
        if (LY_ARRAY_SIZE(*predicates) != key_count) {
            /* names (keys) are unique - it was checked when parsing */
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Predicate missing for a key of %s \"%s\" in path.",
                   lys_nodetype2str(ctx_node->nodetype), ctx_node->name);
            ly_path_predicates_free(ctx, LY_PATH_PREDTYPE_LIST, NULL, *predicates);
            *predicates = NULL;
            return LY_EINVAL;
        }

    } else if (expr->tokens[*tok_idx] == LYXP_TOKEN_DOT) {
        if (ctx_node->nodetype != LYS_LEAFLIST) {
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Leaf-list predicate defined for %s \"%s\" in path.",
                   lys_nodetype2str(ctx_node->nodetype), ctx_node->name);
            return LY_EINVAL;
        }
        ++(*tok_idx);

        /* new predicate */
        *pred_type = LY_PATH_PREDTYPE_LEAFLIST;
        LY_ARRAY_NEW_RET(ctx, *predicates, p, LY_EMEM);

        /* '=' */
        assert(expr->tokens[*tok_idx] == LYXP_TOKEN_OPER_EQUAL);
        ++(*tok_idx);

        assert(expr->tokens[*tok_idx] == LYXP_TOKEN_LITERAL);
        /* store the value */
        LY_CHECK_RET(lyd_value_store(&p->value, ctx_node, expr->expr + expr->tok_pos[*tok_idx] + 1,
                                     expr->tok_len[*tok_idx] - 2, NULL, resolve_prefix, prefix_data, format));
        ++(*tok_idx);

        /* ']' */
        assert(expr->tokens[*tok_idx] == LYXP_TOKEN_BRACK2);
        ++(*tok_idx);
    } else {
        assert(expr->tokens[*tok_idx] == LYXP_TOKEN_NUMBER);
        if (!(ctx_node->nodetype & (LYS_LEAFLIST | LYS_LIST))) {
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Positional predicate defined for %s \"%s\" in path.",
                   lys_nodetype2str(ctx_node->nodetype), ctx_node->name);
            return LY_EINVAL;
        } else if (ctx_node->flags & LYS_CONFIG_W) {
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Positional predicate defined for configuration"
                   " %s \"%s\" in path.", lys_nodetype2str(ctx_node->nodetype), ctx_node->name);
            return LY_EINVAL;
        }

        /* new predicate */
        *pred_type = LY_PATH_PREDTYPE_POSITION;
        LY_ARRAY_NEW_RET(ctx, *predicates, p, LY_EMEM);

        /* syntax was already checked */
        p->position = strtoull(expr->expr + expr->tok_pos[*tok_idx], (char **)&name, 10);
        ++(*tok_idx);

        /* ']' */
        assert(expr->tokens[*tok_idx] == LYXP_TOKEN_BRACK2);
        ++(*tok_idx);
    }

    return LY_SUCCESS;
}

/**
 * @brief Compile leafref predicate. Actually, it is only checked.
 *
 * @param[in] ctx_node Context node, node for which the predicate is defined.
 * @param[in] cur_node Current (original context) node.
 * @param[in] expr Parsed path.
 * @param[in,out] tok_idx Index in @p expr, is adjusted for parsed tokens.
 * @param[in] resolve_prefix Callback for prefix resolution.
 * @param[in] prefix_data Data for @p resolve_prefix.
 * @param[in] format Format of the path.
 * @return LY_ERR value.
 */
static LY_ERR
ly_path_compile_predicate_leafref(const struct lysc_node *ctx_node, const struct lysc_node *cur_node,
                                  const struct lyxp_expr *expr, uint16_t *tok_idx, ly_clb_resolve_prefix resolve_prefix,
                                  void *prefix_data, LYD_FORMAT format)
{
    const struct lysc_node *key, *node, *node2;
    const struct lys_module *mod;
    const char *name;
    size_t name_len;

    /* '[' */
    if (lyxp_next_token(NULL, expr, tok_idx, LYXP_TOKEN_BRACK1)) {
        /* no predicate */
        return LY_SUCCESS;
    }

    if (ctx_node->nodetype != LYS_LIST) {
        LOGVAL(cur_node->module->ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "List predicate defined for %s \"%s\" in path.",
               lys_nodetype2str(ctx_node->nodetype), ctx_node->name);
        return LY_EINVAL;
    } else if (ctx_node->flags & LYS_KEYLESS) {
        LOGVAL(cur_node->module->ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "List predicate defined for keyless %s \"%s\" in path.",
               lys_nodetype2str(ctx_node->nodetype), ctx_node->name);
        return LY_EINVAL;
    }

    do {
        /* NameTest, find the key */
        LY_CHECK_RET(ly_path_compile_prefix(cur_node->module->ctx, cur_node->module, ctx_node, expr, *tok_idx,
                                            LY_PATH_LREF_TRUE, resolve_prefix, prefix_data, format, &mod, &name, &name_len));
        key = lys_find_child(ctx_node, mod, name, name_len, 0, LYS_GETNEXT_NOSTATECHECK);
        if (!key) {
            LOGVAL(cur_node->module->ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Not found node \"%.*s\" in path.", name_len, name);
            return LY_EINVAL;
        } else if ((key->nodetype != LYS_LEAF) || !(key->flags & LYS_KEY)) {
            LOGVAL(cur_node->module->ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Key expected instead of %s \"%s\" in path.",
                   lys_nodetype2str(key->nodetype), key->name);
            return LY_EINVAL;
        }
        ++(*tok_idx);

        /* we are not actually compiling, throw the key away */
        (void)key;

        /* '=' */
        assert(expr->tokens[*tok_idx] == LYXP_TOKEN_OPER_EQUAL);
        ++(*tok_idx);

        /* FuncName */
        assert(expr->tokens[*tok_idx] == LYXP_TOKEN_FUNCNAME);
        ++(*tok_idx);

        /* evaluating from the "current()" node */
        node = cur_node;

        /* '(' */
        assert(expr->tokens[*tok_idx] == LYXP_TOKEN_PAR1);
        ++(*tok_idx);

        /* ')' */
        assert(expr->tokens[*tok_idx] == LYXP_TOKEN_PAR2);
        ++(*tok_idx);

        do {
            /* '/' */
            assert(expr->tokens[*tok_idx] == LYXP_TOKEN_OPER_PATH);
            ++(*tok_idx);

            /* go to parent */
            if (!node) {
                LOGVAL(cur_node->module->ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Too many parent references in path.");
                return LY_EINVAL;
            }
            node = lysc_data_parent(node);

            /* '..' */
            assert(expr->tokens[*tok_idx] == LYXP_TOKEN_DDOT);
            ++(*tok_idx);
        } while (expr->tokens[*tok_idx + 1] == LYXP_TOKEN_DDOT);

        do {
            /* '/' */
            assert(expr->tokens[*tok_idx] == LYXP_TOKEN_OPER_PATH);
            ++(*tok_idx);

            /* NameTest */
            assert(expr->tokens[*tok_idx] == LYXP_TOKEN_NAMETEST);
            LY_CHECK_RET(ly_path_compile_prefix(cur_node->module->ctx, cur_node->module, node, expr, *tok_idx,
                                                LY_PATH_LREF_TRUE, resolve_prefix, prefix_data, format, &mod, &name, &name_len));
            node2 = lys_find_child(node, mod, name, name_len, 0, LYS_GETNEXT_NOSTATECHECK);
            if (!node2) {
                LOGVAL(cur_node->module->ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Not found node \"%.*s\" in path.",
                       name_len, name);
                return LY_EINVAL;
            }
            node = node2;
            ++(*tok_idx);
        } while ((*tok_idx + 1 < expr->used) && (expr->tokens[*tok_idx + 1] == LYXP_TOKEN_NAMETEST));

        /* check the last target node */
        if (node->nodetype != LYS_LEAF) {
            LOGVAL(cur_node->module->ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Leaf expected instead of %s \"%s\" in"
                   " leafref predicate in path.", lys_nodetype2str(node->nodetype), node->name);
            return LY_EINVAL;
        }

        /* we are not actually compiling, throw the rightside node away */
        (void)node;

        /* ']' */
        assert(expr->tokens[*tok_idx] == LYXP_TOKEN_BRACK2);
        ++(*tok_idx);

    /* another predicate follows? */
    } while (!lyxp_next_token(NULL, expr, tok_idx, LYXP_TOKEN_BRACK1));

    return LY_SUCCESS;
}

LY_ERR
ly_path_compile(const struct ly_ctx *ctx, const struct lys_module *cur_mod, const struct lysc_node *ctx_node,
                const struct lyxp_expr *expr, uint8_t lref, uint8_t oper, uint8_t target,
                ly_clb_resolve_prefix resolve_prefix, void *prefix_data, LYD_FORMAT format, struct ly_path **path)
{
    LY_ERR ret = LY_SUCCESS;
    uint16_t tok_idx = 0;
    const struct lys_module *mod;
    const struct lysc_node *node2, *cur_node;
    struct ly_path *p = NULL;
    int getnext_opts;
    const char *name;
    size_t name_len;

    assert(ctx);
    assert((expr->tokens[tok_idx] == LYXP_TOKEN_OPER_PATH) || (lref == LY_PATH_LREF_FALSE) || ctx_node);
    assert((lref == LY_PATH_LREF_TRUE) || (lref == LY_PATH_LREF_FALSE));
    assert((oper == LY_PATH_OPER_INPUT) || (oper == LY_PATH_OPER_OUTPUT));
    assert((target == LY_PATH_TARGET_SINGLE) || (target == LY_PATH_TARGET_MANY));

    if (lref == LY_PATH_LREF_TRUE) {
        /* remember original context node */
        cur_node = ctx_node;
    }
    *path = NULL;

    if (oper == LY_PATH_OPER_OUTPUT) {
        getnext_opts = LYS_GETNEXT_NOSTATECHECK | LYS_GETNEXT_OUTPUT;
    } else {
        getnext_opts = LYS_GETNEXT_NOSTATECHECK;
    }

    if (expr->tokens[tok_idx] == LYXP_TOKEN_OPER_PATH) {
        /* absolute path */
        ctx_node = NULL;

        ++tok_idx;
    } else {
        /* relative path */
        while ((lref == LY_PATH_LREF_TRUE) && (expr->tokens[tok_idx] == LYXP_TOKEN_DDOT)) {
            if (!ctx_node) {
                LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Too many parent references in path.");
                return LY_EINVAL;
            }

            /* get parent */
            ctx_node = lysc_data_parent(ctx_node);

            ++tok_idx;

            assert(expr->tokens[tok_idx] == LYXP_TOKEN_OPER_PATH);
            ++tok_idx;
        }

        /* we are not storing the parent */
        (void)ctx_node;
    }

    do {
        /* check last compiled inner node, whether it is uniquely identified (even key-less list) */
        if (p && (lref == LY_PATH_LREF_FALSE) && (p->node->nodetype == LYS_LIST) && !p->predicates) {
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Predicate missing for %s \"%s\" in path.",
                   lys_nodetype2str(p->node->nodetype), p->node->name);
            return LY_EINVAL;
        }

        /* get module and node name */
        LY_CHECK_GOTO(ret = ly_path_compile_prefix(ctx, cur_mod, ctx_node, expr, tok_idx, lref, resolve_prefix,
                                                   prefix_data, format, &mod, &name, &name_len), cleanup);
        ++tok_idx;

        /* find the next node */
        node2 = lys_find_child(ctx_node, mod, name, name_len, 0, getnext_opts);
        if (!node2) {
            LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Not found node \"%.*s\" in path.", name_len, name);
            ret = LY_EINVAL;
            goto cleanup;
        }
        ctx_node = node2;

        /* new path segment */
        LY_ARRAY_NEW_GOTO(ctx, *path, p, ret, cleanup);
        p->node = ctx_node;

        /* compile any predicates */
        if (lref == LY_PATH_LREF_TRUE) {
            ret = ly_path_compile_predicate_leafref(ctx_node, cur_node, expr, &tok_idx, resolve_prefix, prefix_data, format);
        } else {
            ret = ly_path_compile_predicate(ctx, cur_mod, ctx_node, expr, &tok_idx, resolve_prefix, prefix_data, format,
                                            &p->predicates, &p->pred_type);
        }
        LY_CHECK_GOTO(ret, cleanup);
    } while (!lyxp_next_token(NULL, expr, &tok_idx, LYXP_TOKEN_OPER_PATH));

    /* check last compiled node */
    if ((lref == LY_PATH_LREF_FALSE) && (target == LY_PATH_TARGET_SINGLE)
            && (p->node->nodetype & (LYS_LIST | LYS_LEAFLIST)) && !p->predicates) {
        LOGVAL(ctx, LY_VLOG_NONE, NULL, LYVE_XPATH, "Predicate missing for %s \"%s\" in path.",
               lys_nodetype2str(p->node->nodetype), p->node->name);
        return LY_EINVAL;
    }

cleanup:
    if (ret) {
        ly_path_free(ctx, *path);
        *path = NULL;
    }
    return ret;
}

LY_ERR
ly_path_eval_partial(const struct ly_path *path, const struct lyd_node *start, LY_ARRAY_SIZE_TYPE *path_idx,
                     struct lyd_node **match)
{
    LY_ARRAY_SIZE_TYPE u;
    struct lyd_node *prev_node = NULL, *node, *target;
    uint64_t pos;

    assert(path && start);

    if (lysc_data_parent(path[0].node)) {
        /* relative path, start from the parent children */
        start = lyd_node_children(start);
    } else {
        /* absolute path, start from the first top-level sibling */
        while (start->parent) {
            start = (struct lyd_node *)start->parent;
        }
        while (start->prev->next) {
            start = start->prev;
        }
    }

    LY_ARRAY_FOR(path, u) {
        switch (path[u].pred_type) {
        case LY_PATH_PREDTYPE_POSITION:
            /* we cannot use hashes and want an instance on a specific position */
            pos = 1;
            node = (struct lyd_node *)start;
            while (!lyd_find_sibling_next2(node, path[u].node, NULL, 0, &node)) {
                if (pos == path[u].predicates[0].position) {
                    break;
                }
                ++pos;
            }
            break;
        case LY_PATH_PREDTYPE_LEAFLIST:
            /* we will use hashes to find one leaf-list instance */
            LY_CHECK_RET(lyd_create_term2(path[u].node, &path[u].predicates[0].value, &target));
            lyd_find_sibling_first(start, target, &node);
            lyd_free_tree(target);
            break;
        case LY_PATH_PREDTYPE_LIST:
            /* we will use hashes to find one list instance */
            LY_CHECK_RET(lyd_create_list(path[u].node, path[u].predicates, &target));
            lyd_find_sibling_first(start, target, &node);
            lyd_free_tree(target);
            break;
        case LY_PATH_PREDTYPE_NONE:
            /* we will use hashes to find one any/container/leaf instance */
            lyd_find_sibling_val(start, path[u].node, NULL, 0, &node);
            break;
        }

        if (!node) {
            /* no matching nodes */
            break;
        }

        /* rememeber previous node */
        prev_node = node;

        /* next path segment, if any */
        start = lyd_node_children(node);
    }

    if (node) {
        /* we have found the full path */
        if (path_idx) {
            *path_idx = u;
        }
        if (match) {
            *match = node;
        }
        return LY_SUCCESS;

    } else if (prev_node) {
        /* we have found only some partial match */
        if (path_idx) {
            *path_idx = u - 1;
        }
        if (match) {
            *match = prev_node;
        }
        return LY_EINCOMPLETE;
    }

    /* we have not found any nodes */
    if (path_idx) {
        *path_idx = 0;
    }
    if (match) {
        *match = NULL;
    }
    return LY_ENOTFOUND;
}

LY_ERR
ly_path_eval(const struct ly_path *path, const struct lyd_node *start, struct lyd_node **match)
{
    LY_ERR ret;
    struct lyd_node *m;

    ret = ly_path_eval_partial(path, start, NULL, &m);

    if (ret == LY_SUCCESS) {
        /* last node was found */
        if (match) {
            *match = m;
        }
        return LY_SUCCESS;
    }

    /* not a full match */
    if (match) {
        *match = NULL;
    }
    return LY_ENOTFOUND;
}

LY_ERR
ly_path_dup(const struct ly_ctx *ctx, const struct ly_path *path, struct ly_path **dup)
{
    LY_ARRAY_SIZE_TYPE u, v;

    if (!path) {
        return LY_SUCCESS;
    }

    LY_ARRAY_CREATE_RET(ctx, *dup, LY_ARRAY_SIZE(path), LY_EMEM);
    LY_ARRAY_FOR(path, u) {
        LY_ARRAY_INCREMENT(*dup);
        (*dup)[u].node = path[u].node;
        if (path[u].predicates) {
            LY_ARRAY_CREATE_RET(ctx, (*dup)[u].predicates, LY_ARRAY_SIZE(path[u].predicates), LY_EMEM);
            (*dup)[u].pred_type = path[u].pred_type;
            LY_ARRAY_FOR(path[u].predicates, v) {
                struct ly_path_predicate *pred = &path[u].predicates[v];

                LY_ARRAY_INCREMENT((*dup)[u].predicates);
                switch (path[u].pred_type) {
                case LY_PATH_PREDTYPE_POSITION:
                    /* position-predicate */
                    (*dup)[u].predicates[v].position = pred->position;
                    break;
                case LY_PATH_PREDTYPE_LIST:
                case LY_PATH_PREDTYPE_LEAFLIST:
                    /* key-predicate or leaf-list-predicate */
                    (*dup)[u].predicates[v].key = pred->key;
                    (*dup)[u].predicates[v].value.realtype = pred->value.realtype;
                    pred->value.realtype->plugin->duplicate(ctx, &pred->value, &(*dup)[u].predicates[v].value);
                    break;
                case LY_PATH_PREDTYPE_NONE:
                    break;
                }
            }
        }
    }

    return LY_SUCCESS;
}

void
ly_path_predicates_free(const struct ly_ctx *ctx, enum ly_path_pred_type pred_type, const struct lysc_node *llist,
                        struct ly_path_predicate *predicates)
{
    LY_ARRAY_SIZE_TYPE u;

    if (!predicates) {
        return;
    }

    LY_ARRAY_FOR(predicates, u) {
        switch (pred_type) {
        case LY_PATH_PREDTYPE_POSITION:
        case LY_PATH_PREDTYPE_NONE:
            /* nothing to free */
            break;
        case LY_PATH_PREDTYPE_LIST:
            ((struct lysc_node_leaf *)predicates[u].key)->type->plugin->free(ctx, &predicates[u].value);
            break;
        case LY_PATH_PREDTYPE_LEAFLIST:
            ((struct lysc_node_leaflist *)llist)->type->plugin->free(ctx, &predicates[u].value);
            break;
        }
    }
    LY_ARRAY_FREE(predicates);
}

void
ly_path_free(const struct ly_ctx *ctx, struct ly_path *path)
{
    LY_ARRAY_SIZE_TYPE u;

    LY_ARRAY_FOR(path, u) {
        ly_path_predicates_free(ctx, path[u].pred_type, path[u].node, path[u].predicates);
    }
    LY_ARRAY_FREE(path);
}
