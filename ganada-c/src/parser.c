/* parser.c — recursive descent parser producing Node AST */
#include "ganada.h"

typedef struct {
    Interp *it;
    Tok *toks;
    int p;
} P;

static Node *node_new(P *p, int kind, int line) {
    (void)p;
    Node *nd = calloc(1, sizeof(Node));
    nd->kind = kind; nd->line = line;
    return nd;
}

static Tok *peek(P *p) { return &p->toks[p->p]; }

static bool at(P *p, int kind, const char *val) {
    Tok *t = &p->toks[p->p];
    if (t->kind != kind) return false;
    if (val && t->s != val) return false;   /* interned pointer compare */
    return true;
}

static Tok *eat(P *p, int kind, const char *val) {
    Tok *t = &p->toks[p->p];
    /* T_KW is 0 — must not use `if (kind && …)` or keyword checks are skipped. */
    if (t->kind != kind || (val && t->s != val)) {
        const char *want = val ? val : "?";
        char kb[8];
        if (!val) {
            const char *kn = "?";
            switch (kind) {
            case T_KW: kn = "KW"; break;
            case T_ID: kn = "ID"; break;
            case T_NUM: kn = "NUM"; break;
            case T_STR: kn = "STR"; break;
            case T_OP: kn = "OP"; break;
            case T_EOF: kn = "EOF"; break;
            }
            snprintf(kb, sizeof kb, "%s", kn);
            want = kb;
        }
        const char *got = t->s ? t->s : "?";
        g_error(p->it, ERR_SYNTAX_EXPECT, t->line, t->col, want, got);
    }
    p->p++;
    return t;
}

static Node *parse_expr(P *p);
static Node *parse_statement(P *p);

/* block: '{' statement* '}' */
static Node *parse_brace_block(P *p) {
    eat(p, T_OP, intern_cz(p->it, "{"));
    Node *blk = node_new(p, N_BLOCK, 0);
    Node **list = NULL; int n = 0, cap = 0;
    while (!at(p, T_OP, intern_cz(p->it, "}"))) {
        if (at(p, T_EOF, NULL))
            G_ERR0(p->it, ERR_BLOCK_UNCLOSED);
        if (n == cap) { cap = cap ? cap * 2 : 8; list = realloc(list, sizeof(Node *) * cap); }
        list[n++] = parse_statement(p);
    }
    eat(p, T_OP, intern_cz(p->it, "}"));
    blk->items = list; blk->nitems = n;
    return blk;
}

/* params: '(' [name [= default]] (',' ...)* ')' */
static void parse_params(P *p, const char ***names, Node ***defs, int *np) {
    Interp *it = p->it;
    eat(p, T_OP, intern_cz(it, "("));
    const char **ns = NULL; Node **ds = NULL; int n = 0, cap = 0;
    bool seen_default = false;
    while (!at(p, T_OP, intern_cz(it, ")"))) {
        Tok *t = eat(p, T_ID, NULL);
        Node *def = NULL;
        if (at(p, T_OP, intern_cz(it, "="))) {
            eat(p, T_OP, NULL);
            def = parse_expr(p);
            seen_default = true;
        } else if (seen_default) {
            g_error(it, ERR_DEFAULT_PARAM, t->line, t->col);
        }
        if (n == cap) {
            cap = cap ? cap * 2 : 4;
            ns = realloc(ns, sizeof(char *) * cap);
            ds = realloc(ds, sizeof(Node *) * cap);
        }
        ns[n] = t->s; ds[n] = def; n++;
        if (at(p, T_OP, intern_cz(it, ","))) eat(p, T_OP, NULL);
    }
    eat(p, T_OP, intern_cz(it, ")"));
    *names = ns; *defs = ds; *np = n;
}

static Node *parse_if(P *p);

static Node *parse_statement_inner(P *p) {
    Interp *it = p->it;
    Tok *t = peek(p);

    if (at(p, T_KW, it->kw_ptr[0])) {         /* hamsu */
        eat(p, T_KW, NULL);
        Node *nd = node_new(p, N_FUNC, t->line);
        nd->name = eat(p, T_ID, NULL)->s;
        parse_params(p, &nd->pnames, &nd->pdefs, &nd->nparams);
        nd->a = parse_brace_block(p);
        return nd;
    }
    if (at(p, T_KW, intern_cz(it, KW_GAJYEOOGI))) {
        eat(p, T_KW, NULL);
        Tok *pt = peek(p);
        if (pt->kind != T_STR)
            g_error(it, ERR_IMPORT_NEEDS_STR, pt->line, pt->col);
        eat(p, T_STR, NULL);
        Node *nd = node_new(p, N_IMPORT, t->line);
        nd->str = pt->s;
        return nd;
    }
    if (at(p, T_KW, intern_cz(it, KW_MEOMCHUM))) { eat(p, T_KW, NULL); return node_new(p, N_BREAK, t->line); }
    if (at(p, T_KW, intern_cz(it, KW_GYESOG))) { eat(p, T_KW, NULL); return node_new(p, N_CONT, t->line); }
    if (at(p, T_KW, intern_cz(it, KW_SIDO))) {
        eat(p, T_KW, NULL);
        Node *nd = node_new(p, N_TRY, t->line);
        nd->a = parse_brace_block(p);
        eat(p, T_KW, intern_cz(it, KW_JABGI));
        eat(p, T_OP, intern_cz(it, "("));
        nd->name = eat(p, T_ID, NULL)->s;
        eat(p, T_OP, intern_cz(it, ")"));
        nd->b = parse_brace_block(p);
        return nd;
    }
    if (at(p, T_KW, intern_cz(it, KW_MANYAG))) return parse_if(p);
    if (at(p, T_KW, intern_cz(it, KW_DONGAN))) {
        eat(p, T_KW, NULL);
        Node *nd = node_new(p, N_WHILE, t->line);
        nd->a = parse_expr(p);
        nd->b = parse_brace_block(p);
        return nd;
    }
    if (at(p, T_KW, intern_cz(it, KW_BANBOG))) {
        eat(p, T_KW, NULL);
        const char *var = eat(p, T_ID, NULL)->s;
        const char *idx_var = NULL;
        if (at(p, T_OP, intern_cz(it, ","))) {
            eat(p, T_OP, NULL);
            idx_var = var;
            var = eat(p, T_ID, NULL)->s;
        }
        eat(p, T_KW, intern_cz(it, KW_REUL));
        Node *first = parse_expr(p);
        if (at(p, T_KW, intern_cz(it, KW_ESEO))) {
            eat(p, T_KW, NULL);
            Node *nd = node_new(p, N_FOREACH, t->line);
            nd->name = var; nd->str = idx_var;
            nd->a = first; nd->b = parse_brace_block(p);
            return nd;
        }
        if (idx_var) G_ERR0(it, ERR_RANGE_INDEX);
        eat(p, T_KW, intern_cz(it, KW_BUTEO));
        Node *end = parse_expr(p);
        eat(p, T_KW, intern_cz(it, KW_KKAJI));
        Node *step = NULL;
        if (!at(p, T_OP, intern_cz(it, "{"))) {
            step = parse_expr(p);
            eat(p, T_ID, intern_cz(it, STR_SSIK));
        }
        Node *nd = node_new(p, N_FOR, t->line);
        nd->name = var;
        nd->a = first; nd->b = end; nd->c = step;
        Node *body = parse_brace_block(p);
        /* reuse items[0] for body */
        nd->items = malloc(sizeof(Node *)); nd->items[0] = body; nd->nitems = 1;
        return nd;
    }
    if (at(p, T_KW, intern_cz(it, KW_BANHWAN))) {
        eat(p, T_KW, NULL);
        Node *nd = node_new(p, N_RETURN, t->line);
        if (at(p, T_OP, intern_cz(it, "}")) || at(p, T_EOF, NULL))
            nd->a = NULL;
        else
            nd->a = parse_expr(p);
        return nd;
    }
    /* assignment / destructure / expression */
    Node *node = parse_expr(p);
    if (node->kind == N_VAR && at(p, T_OP, intern_cz(it, ","))) {
        const char **names = NULL; int n = 0, cap = 0;
        if (n == cap) { cap = 4; names = malloc(sizeof(char *) * cap); }
        names[n++] = node->name;
        while (at(p, T_OP, intern_cz(it, ","))) {
            eat(p, T_OP, NULL);
            if (n == cap) { cap *= 2; names = realloc(names, sizeof(char *) * cap); }
            names[n++] = eat(p, T_ID, NULL)->s;
        }
        eat(p, T_OP, intern_cz(it, "="));
        Node *nd = node_new(p, N_DESTRUCT, t->line);
        nd->pnames = names; nd->nparams = n;
        nd->a = parse_expr(p);
        return nd;
    }
    if (at(p, T_OP, intern_cz(it, "="))) {
        eat(p, T_OP, NULL);
        Node *rhs = parse_expr(p);
        if (node->kind == N_VAR) {
            Node *nd = node_new(p, N_ASSIGN, t->line);
            nd->name = node->name; nd->a = rhs;
            return nd;
        }
        if (node->kind == N_INDEX) {
            Node *nd = node_new(p, N_SETINDEX, t->line);
            nd->a = node->a; nd->b = node->b; nd->c = rhs;
            return nd;
        }
        G_ERR0(it, ERR_BAD_ASSIGN);
    }
    static const char *cop[4] = { "+=", "-=", "*=", "/=" };
    for (int k = 0; k < 4; k++) {
        if (at(p, T_OP, intern_cz(it, cop[k]))) {
            eat(p, T_OP, NULL);
            Node *bin = node_new(p, N_BIN, node->line);
            bin->op = OP_ADD + k;
            bin->a = node; bin->b = parse_expr(p);
            if (node->kind == N_VAR) {
                Node *nd = node_new(p, N_ASSIGN, t->line);
                nd->name = node->name; nd->a = bin;
                return nd;
            }
            if (node->kind == N_INDEX) {
                Node *nd = node_new(p, N_SETINDEX, t->line);
                nd->a = node->a; nd->b = node->b; nd->c = bin;
                return nd;
            }
            G_ERR0(it, ERR_BAD_COMPOUND);
        }
    }
    Node *nd = node_new(p, N_EXPRSTMT, t->line);
    nd->a = node;
    return nd;
}

static Node *parse_statement(P *p) {
    int ln = peek(p)->line;
    Node *inner = parse_statement_inner(p);
    Node *st = node_new(p, N_STMT, ln);
    st->a = inner;
    return st;
}

static Node *parse_if(P *p) {
    Interp *it = p->it;
    Tok *t = eat(p, T_KW, intern_cz(it, KW_MANYAG));
    Node *nd = node_new(p, N_IF, t->line);
    nd->a = parse_expr(p);
    nd->b = parse_brace_block(p);
    if (at(p, T_KW, intern_cz(it, KW_ANIMYEON))) {
        eat(p, T_KW, NULL);
        nd->c = at(p, T_KW, intern_cz(it, KW_MANYAG)) ? parse_if(p) : parse_brace_block(p);
    }
    return nd;
}

/* ---- expressions (precedence climbing) ---- */
static Node *parse_logic_or(P *p);

static Node *bin(P *p, int op, Node *l, Node *r, int line) {
    Node *nd = node_new(p, N_BIN, line);
    nd->op = op; nd->a = l; nd->b = r;
    return nd;
}

static Node *parse_logic_and(P *p);

static Node *parse_logic_or(P *p) {
    Interp *it = p->it;
    Node *nd = parse_logic_and(p);
    while (at(p, T_KW, intern_cz(it, KW_TTONEUN))) {
        Tok *t = eat(p, T_KW, NULL);
        nd = bin(p, OP_OR, nd, parse_logic_and(p), t->line);
    }
    return nd;
}

static Node *parse_equality(P *p);

static Node *parse_logic_and(P *p) {
    Interp *it = p->it;
    Node *nd = parse_equality(p);
    while (at(p, T_KW, intern_cz(it, KW_GEURIGO))) {
        Tok *t = eat(p, T_KW, NULL);
        nd = bin(p, OP_AND, nd, parse_equality(p), t->line);
    }
    return nd;
}

static Node *parse_comparison(P *p);

static Node *parse_equality(P *p) {
    Interp *it = p->it;
    Node *nd = parse_comparison(p);
    const char *eq = intern_cz(it, "=="), *ne = intern_cz(it, "!=");
    while (at(p, T_OP, eq) || at(p, T_OP, ne)) {
        Tok *t = eat(p, T_OP, NULL);
        nd = bin(p, t->s == eq ? OP_EQ : OP_NE, nd, parse_comparison(p), t->line);
    }
    return nd;
}

static Node *parse_term(P *p);

static Node *parse_comparison(P *p) {
    Interp *it = p->it;
    Node *nd = parse_term(p);
    const char *lt = intern_cz(it, "<"), *gt = intern_cz(it, ">");
    const char *le = intern_cz(it, "<="), *ge = intern_cz(it, ">=");
    while (at(p, T_OP, lt) || at(p, T_OP, gt) || at(p, T_OP, le) || at(p, T_OP, ge)) {
        Tok *t = eat(p, T_OP, NULL);
        int op = t->s == lt ? OP_LT : t->s == gt ? OP_GT : t->s == le ? OP_LE : OP_GE;
        nd = bin(p, op, nd, parse_term(p), t->line);
    }
    return nd;
}

static Node *parse_factor(P *p);

static Node *parse_term(P *p) {
    Interp *it = p->it;
    Node *nd = parse_factor(p);
    const char *pl = intern_cz(it, "+"), *mi = intern_cz(it, "-");
    while (at(p, T_OP, pl) || at(p, T_OP, mi)) {
        Tok *t = eat(p, T_OP, NULL);
        nd = bin(p, t->s == pl ? OP_ADD : OP_SUB, nd, parse_factor(p), t->line);
    }
    return nd;
}

static Node *parse_unary(P *p);

static Node *parse_factor(P *p) {
    Interp *it = p->it;
    Node *nd = parse_unary(p);
    const char *mu = intern_cz(it, "*"), *di = intern_cz(it, "/"), *mo = intern_cz(it, "%");
    while (at(p, T_OP, mu) || at(p, T_OP, di) || at(p, T_OP, mo)) {
        Tok *t = eat(p, T_OP, NULL);
        int op = t->s == mu ? OP_MUL : t->s == di ? OP_DIV : OP_MOD;
        nd = bin(p, op, nd, parse_unary(p), t->line);
    }
    return nd;
}

static Node *parse_call(P *p);

static Node *parse_unary(P *p) {
    Interp *it = p->it;
    Tok *t = peek(p);
    if (at(p, T_KW, intern_cz(it, KW_ANIDA))) {
        eat(p, T_KW, NULL);
        Node *nd = node_new(p, N_UN, t->line);
        nd->op = OP_NOT; nd->a = parse_unary(p);
        return nd;
    }
    if (at(p, T_OP, intern_cz(it, "-"))) {
        eat(p, T_OP, NULL);
        Node *nd = node_new(p, N_UN, t->line);
        nd->op = OP_NEG; nd->a = parse_unary(p);
        return nd;
    }
    return parse_call(p);
}

static Node *parse_primary(P *p);

static Node *parse_call(P *p) {
    Interp *it = p->it;
    Node *nd = parse_primary(p);
    for (;;) {
        if (at(p, T_OP, intern_cz(it, "("))) {
            Tok *t = eat(p, T_OP, NULL);
            Node **args = NULL; int n = 0, cap = 0;
            while (!at(p, T_OP, intern_cz(it, ")"))) {
                if (n == cap) { cap = cap ? cap * 2 : 4; args = realloc(args, sizeof(Node *) * cap); }
                args[n++] = parse_expr(p);
                if (at(p, T_OP, intern_cz(it, ","))) eat(p, T_OP, NULL);
            }
            eat(p, T_OP, intern_cz(it, ")"));
            Node *cn = node_new(p, N_CALL, t->line);
            cn->a = nd; cn->items = args; cn->nitems = n;
            nd = cn;
        } else if (at(p, T_OP, intern_cz(it, "["))) {
            Tok *t = eat(p, T_OP, NULL);
            Node *start = at(p, T_OP, intern_cz(it, ":")) ? NULL : parse_expr(p);
            if (at(p, T_OP, intern_cz(it, ":"))) {
                eat(p, T_OP, NULL);
                Node *end = at(p, T_OP, intern_cz(it, "]")) ? NULL : parse_expr(p);
                eat(p, T_OP, intern_cz(it, "]"));
                Node *sn = node_new(p, N_SLICE, t->line);
                sn->a = nd; sn->b = start; sn->c = end;
                nd = sn;
            } else {
                eat(p, T_OP, intern_cz(it, "]"));
                Node *in = node_new(p, N_INDEX, t->line);
                in->a = nd; in->b = start;
                nd = in;
            }
        } else break;
    }
    return nd;
}

static Node *parse_primary(P *p) {
    Interp *it = p->it;
    Tok *t = peek(p);
    if (t->kind == T_NUM) {
        eat(p, T_NUM, NULL);
        Node *nd = node_new(p, N_LIT, t->line);
        nd->lit = t->num;
        return nd;
    }
    if (t->kind == T_STR) {
        eat(p, T_STR, NULL);
        Node *nd = node_new(p, N_LIT, t->line);
        nd->lit = v_str(str_perm(t->s));   /* ast outlives every collection */
        return nd;
    }
    if (t->kind == T_KW && t->s == intern_cz(it, KW_CHAM)) { eat(p, T_KW, NULL); Node *nd = node_new(p, N_LIT, t->line); nd->lit = v_bool(1); return nd; }
    if (t->kind == T_KW && t->s == intern_cz(it, KW_GEOJIS)) { eat(p, T_KW, NULL); Node *nd = node_new(p, N_LIT, t->line); nd->lit = v_bool(0); return nd; }
    if (t->kind == T_KW && t->s == intern_cz(it, KW_EOBSEUM)) { eat(p, T_KW, NULL); Node *nd = node_new(p, N_LIT, t->line); nd->lit = v_nil(); return nd; }
    if (t->kind == T_ID) {
        eat(p, T_ID, NULL);
        Node *nd = node_new(p, N_VAR, t->line);
        nd->name = t->s;
        return nd;
    }
    if (t->kind == T_OP && t->s == intern_cz(it, "(")) {
        eat(p, T_OP, NULL);
        Node *nd = parse_expr(p);
        eat(p, T_OP, intern_cz(it, ")"));
        return nd;
    }
    if (t->kind == T_OP && t->s == intern_cz(it, "[")) {
        eat(p, T_OP, NULL);
        Node **elems = NULL; int n = 0, cap = 0;
        while (!at(p, T_OP, intern_cz(it, "]"))) {
            if (n == cap) { cap = cap ? cap * 2 : 8; elems = realloc(elems, sizeof(Node *) * cap); }
            elems[n++] = parse_expr(p);
            if (at(p, T_OP, intern_cz(it, ","))) eat(p, T_OP, NULL);
        }
        eat(p, T_OP, intern_cz(it, "]"));
        Node *nd = node_new(p, N_LIST, t->line);
        nd->items = elems; nd->nitems = n;
        return nd;
    }
    if (t->kind == T_OP && t->s == intern_cz(it, "{")) {
        eat(p, T_OP, NULL);
        Node **pairs = NULL; int n = 0, cap = 0;
        while (!at(p, T_OP, intern_cz(it, "}"))) {
            if (n == cap) { cap = cap ? cap * 2 : 8; pairs = realloc(pairs, sizeof(Node *) * 2 * cap); }
            pairs[2 * n] = parse_expr(p);
            eat(p, T_OP, intern_cz(it, ":"));
            pairs[2 * n + 1] = parse_expr(p);
            n++;
            if (at(p, T_OP, intern_cz(it, ","))) eat(p, T_OP, NULL);
        }
        eat(p, T_OP, intern_cz(it, "}"));
        Node *nd = node_new(p, N_DICT, t->line);
        nd->items = pairs; nd->nitems = n;
        return nd;
    }
    if (t->kind == T_KW && t->s == intern_cz(it, KW_RAMDA)) {
        eat(p, T_KW, NULL);
        Node *nd = node_new(p, N_LAMBDA, t->line);
        parse_params(p, &nd->pnames, &nd->pdefs, &nd->nparams);
        nd->a = parse_brace_block(p);
        return nd;
    }
    const char *got = t->s ? t->s : "?";
    g_error(it, ERR_SYNTAX_UNEXPECTED, t->line, t->col, got);
}

static Node *parse_expr(P *p) { return parse_logic_or(p); }

Node *parse_all(Interp *it, Tok *toks) {
    P p = { it, toks, 0 };
    Node *blk = node_new(&p, N_BLOCK, 0);
    Node **list = NULL; int n = 0, cap = 0;
    while (!at(&p, T_EOF, NULL)) {
        if (n == cap) { cap = cap ? cap * 2 : 16; list = realloc(list, sizeof(Node *) * cap); }
        list[n++] = parse_statement(&p);
    }
    blk->items = list; blk->nitems = n;
    return blk;
}
