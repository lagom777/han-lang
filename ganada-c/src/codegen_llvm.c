/* codegen_llvm.c — 나/가나다 subset → LLVM IR
 * i64 arith, vars (i64 or cstr), if/while/for, functions, 출력(i64|string)
 * string + string / string + i64 / i64 + string via na_rt
 * Per-local type: %%locN (i64) or %%slocN (ptr); first assign wins.
 * basic-block `term` flag prevents post-ret code.
 *
 * Full language (lists/dict/web/db/ai/…) still needs the interpreter.
 */
#include "ganada.h"
#include <stdarg.h>

typedef struct {
    char *buf; size_t n, cap;
    char *err;
    int tmp, lab, term, depth;
    const char **locals; int nlocals, caplocals;
    int *local_is_str; /* -1 unset, 0 i64, 1 cstr — parallel to locals[] */
    Interp *it;
    int brk_lab[32], cont_lab[32];
} CG;

/* Typed SSA result: i64 register or ptr (cstr / malloc'd). */
typedef struct { int t; int is_str; } CgV;
static CgV cg_bad(void) { return (CgV){ -1, 0 }; }
static CgV cg_i64(int t) { return (CgV){ t, 0 }; }
static CgV cg_str(int t) { return (CgV){ t, 1 }; }

typedef struct { const char *data; uint32_t len; } StrG;
static StrG *sg; static int nsg, capsg;

static void cg_err(CG *g, const char *fmt, ...) {
    if (g->err) return;
    char tmp[1024]; va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    g->err = strdup(tmp);
}
static void cg_grow(CG *g, size_t need) {
    if (g->n + need + 1 <= g->cap) return;
    size_t nc = g->cap ? g->cap * 2 : 4096;
    while (nc < g->n + need + 1) nc *= 2;
    g->buf = realloc(g->buf, nc); g->cap = nc;
}
static void cg_puts(CG *g, const char *s) {
    size_t L = strlen(s); cg_grow(g, L); memcpy(g->buf + g->n, s, L); g->n += L; g->buf[g->n] = 0;
}
static void cg_printf(CG *g, const char *fmt, ...) {
    char tmp[2048]; va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    cg_puts(g, tmp);
}
static int cg_newtmp(CG *g) { return g->tmp++; }
static int cg_newlab(CG *g) { return g->lab++; }
static void cg_label(CG *g, int lab) { cg_printf(g, "L%d:\n", lab); g->term = 0; }
static void cg_br(CG *g, int lab) {
    if (g->term) return;
    cg_printf(g, "  br label %%L%d\n", lab); g->term = 1;
}
static void cg_ret_i64(CG *g, int t) {
    if (g->term) return;
    if (t < 0) cg_puts(g, "  ret i64 0\n"); else cg_printf(g, "  ret i64 %%%d\n", t);
    g->term = 1;
}
static void cg_ret0(CG *g) { cg_ret_i64(g, -1); }
static void cg_ret_i32_0(CG *g) {
    if (g->term) return; cg_puts(g, "  ret i32 0\n"); g->term = 1;
}

static void cg_qid(CG *g, const char *name) {
    cg_puts(g, "\"");
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p >= 32 && *p < 127 && *p != '"' && *p != '\\') cg_printf(g, "%c", *p);
        else cg_printf(g, "\\%02X", *p);
    }
    cg_puts(g, "\"");
}

static int slot_of(CG *g, const char *name) {
    for (int i = 0; i < g->nlocals; i++) if (g->locals[i] == name) return i;
    cg_err(g, "LLVM: 변수 슬롯 없음"); return -1;
}
static void cg_add_local(CG *g, const char *name) {
    for (int i = 0; i < g->nlocals; i++) if (g->locals[i] == name) return;
    if (g->nlocals == g->caplocals) {
        g->caplocals = g->caplocals ? g->caplocals * 2 : 16;
        g->locals = realloc(g->locals, sizeof(char *) * (size_t)g->caplocals);
        g->local_is_str = realloc(g->local_is_str, sizeof(int) * (size_t)g->caplocals);
    }
    g->local_is_str[g->nlocals] = -1; /* unset until first assign / param / for */
    g->locals[g->nlocals++] = name;
}
static void cg_free_locals(CG *g) {
    free(g->locals); free(g->local_is_str);
    g->locals = NULL; g->local_is_str = NULL;
    g->nlocals = g->caplocals = 0;
}
/* Mark slot type; first set wins. Reassign same type OK; type change → error. */
static int set_local_type(CG *g, int slot, int is_str, int line) {
    if (slot < 0) return -1;
    if (g->local_is_str[slot] < 0) {
        g->local_is_str[slot] = is_str ? 1 : 0;
        return 0;
    }
    if (g->local_is_str[slot] != (is_str ? 1 : 0)) {
        cg_err(g, "LLVM: 변수 타입 변경 불가 (i64↔문자열, 행 %d)", line);
        return -1;
    }
    return 0;
}

static void collect_stmt(CG *g, Node *n);
static void collect_expr(CG *g, Node *n) {
    if (!n) return;
    switch (n->kind) {
    case N_BIN: collect_expr(g, n->a); collect_expr(g, n->b); break;
    case N_UN: collect_expr(g, n->a); break;
    case N_CALL:
        collect_expr(g, n->a);
        for (int i = 0; i < n->nitems; i++) collect_expr(g, n->items[i]);
        break;
    case N_VAR: cg_add_local(g, n->name); break;
    default: break;
    }
}
static void collect_stmt(CG *g, Node *n) {
    if (!n) return;
    if (n->kind == N_STMT) { collect_stmt(g, n->a); return; }
    switch (n->kind) {
    case N_BLOCK: for (int i = 0; i < n->nitems; i++) collect_stmt(g, n->items[i]); break;
    case N_ASSIGN: cg_add_local(g, n->name); collect_expr(g, n->a); break;
    case N_FOR:
        cg_add_local(g, n->name);
        collect_expr(g, n->a); collect_expr(g, n->b); collect_expr(g, n->c);
        if (n->nitems) collect_stmt(g, n->items[0]);
        break;
    case N_WHILE: collect_expr(g, n->a); collect_stmt(g, n->b); break;
    case N_IF: collect_expr(g, n->a); collect_stmt(g, n->b); if (n->c) collect_stmt(g, n->c); break;
    case N_RETURN: collect_expr(g, n->a); break;
    case N_EXPRSTMT: collect_expr(g, n->a); break;
    default: break;
    }
}

/* Allocate both i64 and ptr slots; use based on local_is_str at load/store. */
static void emit_allocas(CG *g) {
    for (int i = 0; i < g->nlocals; i++) {
        cg_printf(g, "  %%loc%d = alloca i64, align 8\n", i);
        cg_printf(g, "  store i64 0, ptr %%loc%d, align 8\n", i);
        cg_printf(g, "  %%sloc%d = alloca ptr, align 8\n", i);
        cg_printf(g, "  store ptr null, ptr %%sloc%d, align 8\n", i);
    }
}
static int load_slot(CG *g, int slot) {
    int t = cg_newtmp(g);
    cg_printf(g, "  %%%d = load i64, ptr %%loc%d, align 8\n", t, slot);
    return t;
}
static void store_slot(CG *g, int slot, int t) {
    cg_printf(g, "  store i64 %%%d, ptr %%loc%d, align 8\n", t, slot);
}
static int load_str_slot(CG *g, int slot) {
    int t = cg_newtmp(g);
    cg_printf(g, "  %%%d = load ptr, ptr %%sloc%d, align 8\n", t, slot);
    return t;
}
static void store_str_slot(CG *g, int slot, int t) {
    cg_printf(g, "  store ptr %%%d, ptr %%sloc%d, align 8\n", t, slot);
}
static CgV load_local(CG *g, int slot) {
    if (slot < 0) return cg_bad();
    if (g->local_is_str[slot] == 1) return cg_str(load_str_slot(g, slot));
    return cg_i64(load_slot(g, slot)); /* unset or i64 → i64 (default 0) */
}
static int store_local(CG *g, int slot, CgV v, int line) {
    if (slot < 0 || v.t < 0) return -1;
    if (set_local_type(g, slot, v.is_str, line) < 0) return -1;
    if (v.is_str) store_str_slot(g, slot, v.t);
    else store_slot(g, slot, v.t);
    return 0;
}

static int intern_str(const char *data, uint32_t len) {
    for (int i = 0; i < nsg; i++)
        if (sg[i].len == len && memcmp(sg[i].data, data, len) == 0) return i;
    if (nsg == capsg) { capsg = capsg ? capsg * 2 : 8; sg = realloc(sg, sizeof(StrG) * (size_t)capsg); }
    sg[nsg] = (StrG){ data, len }; return nsg++;
}

/* Static: expression yields a string (or concat involving a string). */
static int expr_is_str(Node *n) {
    if (!n) return 0;
    if (n->kind == N_LIT && n->lit.tag == VT_STR) return 1;
    if (n->kind == N_BIN && n->op == OP_ADD)
        return expr_is_str(n->a) || expr_is_str(n->b);
    return 0;
}

static int is_print(CG *g, Node *fn) {
    return fn && fn->kind == N_VAR && fn->name == g->it->builtin_ptr[B_CHULRYEOG];
}

static CgV emit_val(CG *g, Node *n);
static int emit_i64(CG *g, Node *n);
static int emit_bool(CG *g, Node *n);

static int to_bool(CG *g, int t) {
    int b = cg_newtmp(g);
    cg_printf(g, "  %%%d = icmp ne i64 %%%d, 0\n", b, t);
    return b;
}
static int from_bool(CG *g, int b) {
    int t = cg_newtmp(g);
    cg_printf(g, "  %%%d = zext i1 %%%d to i64\n", t, b);
    return t;
}

/* ptr to interned string global @.sN */
static int emit_str_global_ptr(CG *g, int sid) {
    int t = cg_newtmp(g);
    uint32_t n = sg[sid].len + 1;
    cg_printf(g, "  %%%d = getelementptr inbounds [%u x i8], ptr @.s%d, i64 0, i64 0\n",
              t, n, sid);
    return t;
}

/* Coerce any value to cstr (i64 → na_i64_to_str). */
static int coerce_cstr(CG *g, CgV v) {
    if (v.t < 0) return -1;
    if (v.is_str) return v.t;
    int t = cg_newtmp(g);
    cg_printf(g, "  %%%d = call ptr @na_i64_to_str(i64 %%%d)\n", t, v.t);
    return t;
}

static CgV emit_val(CG *g, Node *n) {
    if (!n || g->err) return cg_bad();
    switch (n->kind) {
    case N_LIT: {
        if (n->lit.tag == VT_STR) {
            int sid = intern_str(n->lit.as.s->data, n->lit.as.s->len);
            return cg_str(emit_str_global_ptr(g, sid));
        }
        int64_t v = 0;
        if (n->lit.tag == VT_INT) v = n->lit.as.i;
        else if (n->lit.tag == VT_BOOL) v = n->lit.as.b ? 1 : 0;
        else if (n->lit.tag == VT_FLOAT) v = (int64_t)n->lit.as.f;
        else if (n->lit.tag == VT_NIL) v = 0;
        else { cg_err(g, "LLVM: 미지원 리터럴 행%d", n->line); return cg_bad(); }
        int t = cg_newtmp(g);
        cg_printf(g, "  %%%d = add i64 0, %lld\n", t, (long long)v);
        return cg_i64(t);
    }
    case N_VAR: {
        int s = slot_of(g, n->name); if (s < 0) return cg_bad();
        return load_local(g, s);
    }
    case N_UN: {
        int a = emit_i64(g, n->a); if (a < 0) return cg_bad();
        int t = cg_newtmp(g);
        if (n->op == OP_NEG) cg_printf(g, "  %%%d = sub i64 0, %%%d\n", t, a);
        else if (n->op == OP_NOT) {
            int z = cg_newtmp(g);
            cg_printf(g, "  %%%d = icmp eq i64 %%%d, 0\n", z, a);
            cg_printf(g, "  %%%d = zext i1 %%%d to i64\n", t, z);
        } else { cg_err(g, "LLVM: 미지원 단항"); return cg_bad(); }
        return cg_i64(t);
    }
    case N_BIN: {
        if (n->op == OP_AND || n->op == OP_OR) {
            int a = emit_i64(g, n->a), b = emit_i64(g, n->b);
            if (a < 0 || b < 0) return cg_bad();
            int ab = to_bool(g, a), bb = to_bool(g, b), r = cg_newtmp(g);
            cg_printf(g, "  %%%d = %s i1 %%%d, %%%d\n", r, n->op == OP_AND ? "and" : "or", ab, bb);
            return cg_i64(from_bool(g, r));
        }
        /* string concat: "a"+x, x+"b", "a"+"b", nested, or string vars */
        if (n->op == OP_ADD) {
            CgV va = emit_val(g, n->a), vb = emit_val(g, n->b);
            if (va.t < 0 || vb.t < 0) return cg_bad();
            if (va.is_str || vb.is_str || expr_is_str(n->a) || expr_is_str(n->b)) {
                int sa = coerce_cstr(g, va), sb = coerce_cstr(g, vb);
                if (sa < 0 || sb < 0) return cg_bad();
                int t = cg_newtmp(g);
                cg_printf(g, "  %%%d = call ptr @na_str_concat(ptr %%%d, ptr %%%d)\n", t, sa, sb);
                return cg_str(t);
            }
            int t = cg_newtmp(g);
            cg_printf(g, "  %%%d = add i64 %%%d, %%%d\n", t, va.t, vb.t);
            return cg_i64(t);
        }
        int a = emit_i64(g, n->a), b = emit_i64(g, n->b);
        if (a < 0 || b < 0) return cg_bad();
        int t = cg_newtmp(g);
        switch (n->op) {
        case OP_ADD: /* handled above */ cg_printf(g, "  %%%d = add i64 %%%d, %%%d\n", t, a, b); break;
        case OP_SUB: cg_printf(g, "  %%%d = sub i64 %%%d, %%%d\n", t, a, b); break;
        case OP_MUL: cg_printf(g, "  %%%d = mul i64 %%%d, %%%d\n", t, a, b); break;
        case OP_DIV: cg_printf(g, "  %%%d = sdiv i64 %%%d, %%%d\n", t, a, b); break;
        case OP_MOD: cg_printf(g, "  %%%d = srem i64 %%%d, %%%d\n", t, a, b); break;
        case OP_EQ: case OP_NE: case OP_LT: case OP_GT: case OP_LE: case OP_GE: {
            const char *p = n->op==OP_EQ?"eq":n->op==OP_NE?"ne":n->op==OP_LT?"slt":n->op==OP_GT?"sgt":n->op==OP_LE?"sle":"sge";
            cg_printf(g, "  %%%d = icmp %s i64 %%%d, %%%d\n", t, p, a, b);
            return cg_i64(from_bool(g, t));
        }
        default: cg_err(g, "LLVM: 미지원 이항 (행 %d)", n->line); return cg_bad();
        }
        return cg_i64(t);
    }
    case N_CALL: {
        if (is_print(g, n->a)) {
            if (n->nitems != 1) { cg_err(g, "LLVM: 출력 인자 1개"); return cg_bad(); }
            CgV arg = emit_val(g, n->items[0]);
            if (arg.t < 0) return cg_bad();
            if (arg.is_str)
                cg_printf(g, "  call void @na_print_cstr(ptr %%%d)\n", arg.t);
            else
                cg_printf(g, "  call void @na_print_i64(i64 %%%d)\n", arg.t);
            int z = cg_newtmp(g); cg_printf(g, "  %%%d = add i64 0, 0\n", z);
            return cg_i64(z);
        }
        if (!n->a || n->a->kind != N_VAR) { cg_err(g, "LLVM: 간접 호출 미지원"); return cg_bad(); }
        int *args = n->nitems ? malloc(sizeof(int) * (size_t)n->nitems) : NULL;
        for (int i = 0; i < n->nitems; i++) {
            args[i] = emit_i64(g, n->items[i]);
            if (args[i] < 0) { free(args); return cg_bad(); }
        }
        int t = cg_newtmp(g);
        cg_printf(g, "  %%%d = call i64 @", t); cg_qid(g, n->a->name); cg_puts(g, "(");
        for (int i = 0; i < n->nitems; i++) { if (i) cg_puts(g, ", "); cg_printf(g, "i64 %%%d", args[i]); }
        cg_puts(g, ")\n"); free(args);
        return cg_i64(t);
    }
    default: cg_err(g, "LLVM: 미지원 식 kind=%d 행%d", n->kind, n->line); return cg_bad();
    }
}

static int emit_i64(CG *g, Node *n) {
    CgV v = emit_val(g, n);
    if (v.t < 0) return -1;
    if (v.is_str) {
        cg_err(g, "LLVM: 문자열이 정수가 필요한 자리에 있음 (행 %d)", n ? n->line : 0);
        return -1;
    }
    return v.t;
}

static int emit_bool(CG *g, Node *n) {
    /* Comparisons already produce 0/1 i64 via emit_val; for icmp we need i1.
     * Re-emit path: emit i64 then icmp ne 0. For cmp ops emit_val returns zext'd i64. */
    if (!n || g->err) return -1;
    if (n->kind == N_BIN) {
        switch (n->op) {
        case OP_EQ: case OP_NE: case OP_LT: case OP_GT: case OP_LE: case OP_GE: {
            if (expr_is_str(n->a) || expr_is_str(n->b)) {
                cg_err(g, "LLVM: 문자열 비교 미지원 (행 %d)", n->line); return -1;
            }
            int a = emit_i64(g, n->a), b = emit_i64(g, n->b);
            if (a < 0 || b < 0) return -1;
            int t = cg_newtmp(g);
            const char *p = n->op==OP_EQ?"eq":n->op==OP_NE?"ne":n->op==OP_LT?"slt":n->op==OP_GT?"sgt":n->op==OP_LE?"sle":"sge";
            cg_printf(g, "  %%%d = icmp %s i64 %%%d, %%%d\n", t, p, a, b);
            return t;
        }
        case OP_AND: case OP_OR: {
            int a = emit_i64(g, n->a), b = emit_i64(g, n->b);
            if (a < 0 || b < 0) return -1;
            int ab = to_bool(g, a), bb = to_bool(g, b), r = cg_newtmp(g);
            cg_printf(g, "  %%%d = %s i1 %%%d, %%%d\n", r, n->op == OP_AND ? "and" : "or", ab, bb);
            return r;
        }
        default: break;
        }
    }
    int v = emit_i64(g, n);
    if (v < 0) return -1;
    return to_bool(g, v);
}

static void emit_stmt(CG *g, Node *n);
static void emit_block(CG *g, Node *blk) {
    if (!blk) return;
    if (blk->kind == N_BLOCK) { for (int i = 0; i < blk->nitems; i++) emit_stmt(g, blk->items[i]); return; }
    emit_stmt(g, blk);
}

static void emit_stmt(CG *g, Node *n) {
    if (!n || g->err) return;
    if (n->kind == N_STMT) { emit_stmt(g, n->a); return; }
    if (g->term) return;
    switch (n->kind) {
    case N_BLOCK: emit_block(g, n); break;
    case N_ASSIGN: {
        CgV v = emit_val(g, n->a); int s = slot_of(g, n->name);
        if (v.t >= 0 && s >= 0) store_local(g, s, v, n->line);
        break;
    }
    case N_EXPRSTMT: (void)emit_val(g, n->a); break;
    case N_RETURN:
        if (n->a) { int v = emit_i64(g, n->a); if (v >= 0) cg_ret_i64(g, v); }
        else cg_ret0(g);
        break;
    case N_IF: {
        int c = emit_bool(g, n->a); if (c < 0) return;
        int Lt = cg_newlab(g), Lf = cg_newlab(g), Le = cg_newlab(g);
        if (n->c) cg_printf(g, "  br i1 %%%d, label %%L%d, label %%L%d\n", c, Lt, Lf);
        else cg_printf(g, "  br i1 %%%d, label %%L%d, label %%L%d\n", c, Lt, Le);
        g->term = 1;
        cg_label(g, Lt); emit_block(g, n->b); cg_br(g, Le);
        if (n->c) {
            cg_label(g, Lf);
            if (n->c->kind == N_IF) emit_stmt(g, n->c); else emit_block(g, n->c);
            cg_br(g, Le);
        }
        cg_label(g, Le);
        break;
    }
    case N_WHILE: {
        int Lc = cg_newlab(g), Lb = cg_newlab(g), Le = cg_newlab(g);
        if (g->depth < 32) { g->brk_lab[g->depth] = Le; g->cont_lab[g->depth] = Lc; g->depth++; }
        cg_br(g, Lc); cg_label(g, Lc);
        int c = emit_bool(g, n->a); if (c < 0) return;
        cg_printf(g, "  br i1 %%%d, label %%L%d, label %%L%d\n", c, Lb, Le); g->term = 1;
        cg_label(g, Lb); emit_block(g, n->b); cg_br(g, Lc);
        cg_label(g, Le); if (g->depth) g->depth--;
        break;
    }
    case N_FOR: {
        int slot = slot_of(g, n->name);
        int va = emit_i64(g, n->a), vb = emit_i64(g, n->b);
        if (va < 0 || vb < 0 || slot < 0) return;
        if (set_local_type(g, slot, 0, n->line) < 0) return; /* for index is i64 */
        int vs;
        if (n->c) { vs = emit_i64(g, n->c); if (vs < 0) return; }
        else { vs = cg_newtmp(g); cg_printf(g, "  %%%d = add i64 0, 1\n", vs); }
        store_slot(g, slot, va);
        int Lc = cg_newlab(g), Lb = cg_newlab(g), Li = cg_newlab(g), Le = cg_newlab(g);
        if (g->depth < 32) { g->brk_lab[g->depth] = Le; g->cont_lab[g->depth] = Li; g->depth++; }
        cg_br(g, Lc); cg_label(g, Lc);
        int cur = load_slot(g, slot); int ok = cg_newtmp(g);
        cg_printf(g, "  %%%d = icmp sle i64 %%%d, %%%d\n", ok, cur, vb);
        cg_printf(g, "  br i1 %%%d, label %%L%d, label %%L%d\n", ok, Lb, Le); g->term = 1;
        cg_label(g, Lb); if (n->nitems) emit_block(g, n->items[0]); cg_br(g, Li);
        cg_label(g, Li); cur = load_slot(g, slot); int nxt = cg_newtmp(g);
        cg_printf(g, "  %%%d = add i64 %%%d, %%%d\n", nxt, cur, vs);
        store_slot(g, slot, nxt); cg_br(g, Lc);
        cg_label(g, Le); if (g->depth) g->depth--;
        break;
    }
    case N_BREAK:
        if (g->depth <= 0) cg_err(g, "LLVM: 반복 밖 멈춤");
        else cg_br(g, g->brk_lab[g->depth - 1]);
        break;
    case N_CONT:
        if (g->depth <= 0) cg_err(g, "LLVM: 반복 밖 계속");
        else cg_br(g, g->cont_lab[g->depth - 1]);
        break;
    case N_FUNC: break;
    default: cg_err(g, "LLVM: 미지원 문장 kind=%d 행%d", n->kind, n->line); break;
    }
}

static void emit_function(CG *g, Node *fn) {
    cg_free_locals(g);
    g->tmp = g->lab = g->depth = g->term = 0;
    for (int i = 0; i < fn->nparams; i++) cg_add_local(g, fn->pnames[i]);
    collect_stmt(g, fn->a);
    cg_puts(g, "define i64 @"); cg_qid(g, fn->name); cg_puts(g, "(");
    for (int i = 0; i < fn->nparams; i++) { if (i) cg_puts(g, ", "); cg_printf(g, "i64 %%arg%d", i); }
    cg_puts(g, ") {\nentry:\n");
    emit_allocas(g);
    for (int i = 0; i < fn->nparams; i++) {
        int s = slot_of(g, fn->pnames[i]);
        if (s >= 0) {
            set_local_type(g, s, 0, fn->line); /* params are i64 */
            cg_printf(g, "  store i64 %%arg%d, ptr %%loc%d, align 8\n", i, s);
        }
    }
    emit_block(g, fn->a);
    cg_ret0(g);
    cg_puts(g, "}\n\n");
}

static void emit_str_globals(CG *g) {
    for (int i = 0; i < nsg; i++) {
        cg_printf(g, "@.s%d = private unnamed_addr constant [%u x i8] c\"", i, sg[i].len + 1);
        for (uint32_t k = 0; k < sg[i].len; k++) {
            unsigned char c = (unsigned char)sg[i].data[k];
            if (c == '"' || c == '\\') cg_printf(g, "\\%02X", c);
            else if (c >= 32 && c < 127) cg_printf(g, "%c", c);
            else if (c == '\n') cg_puts(g, "\\0A");
            else cg_printf(g, "\\%02X", c);
        }
        cg_puts(g, "\\00\"\n");
    }
}

char *llvm_emit_module(Interp *it, Node *root, char **errp) {
    CG g = {0}; g.it = it;
    free(sg); sg = NULL; nsg = capsg = 0;
    if (!root || root->kind != N_BLOCK) {
        if (errp) *errp = strdup("LLVM: 루트가 블록이 아님"); return NULL;
    }
    Node **funcs = NULL; int nf = 0, cf = 0;
    Node **mains = NULL; int nm = 0, cm = 0;
    for (int i = 0; i < root->nitems; i++) {
        Node *st = root->items[i];
        Node *inner = (st && st->kind == N_STMT) ? st->a : st;
        if (inner && inner->kind == N_FUNC) {
            if (nf == cf) { cf = cf ? cf * 2 : 4; funcs = realloc(funcs, sizeof(Node *) * (size_t)cf); }
            funcs[nf++] = inner;
        } else {
            if (nm == cm) { cm = cm ? cm * 2 : 8; mains = realloc(mains, sizeof(Node *) * (size_t)cm); }
            mains[nm++] = st;
        }
    }
    for (int i = 0; i < nf && !g.err; i++) emit_function(&g, funcs[i]);
    if (!g.err) {
        cg_free_locals(&g);
        g.tmp = g.lab = g.depth = g.term = 0;
        for (int i = 0; i < nm; i++) collect_stmt(&g, mains[i]);
        cg_puts(&g, "define i32 @main() {\nentry:\n");
        emit_allocas(&g);
        for (int i = 0; i < nm; i++) emit_stmt(&g, mains[i]);
        cg_ret_i32_0(&g);
        cg_puts(&g, "}\n");
    }
    free(funcs); free(mains); cg_free_locals(&g);
    if (g.err) {
        if (errp) *errp = g.err; free(g.buf);
        free(sg); sg = NULL; nsg = capsg = 0; return NULL;
    }
    CG out = {0};
    cg_puts(&out, "; 나/가나다 → LLVM IR (subset) + na_rt\n");
    cg_puts(&out, "target triple = \"arm64-apple-macosx\"\n\n");
    emit_str_globals(&out);
    cg_puts(&out, "\n");
    cg_puts(&out, "declare void @na_print_i64(i64)\n");
    cg_puts(&out, "declare void @na_print_cstr(ptr)\n");
    cg_puts(&out, "declare ptr @na_str_concat(ptr, ptr)\n");
    cg_puts(&out, "declare ptr @na_i64_to_str(i64)\n\n");
    cg_puts(&out, g.buf ? g.buf : "");
    free(g.buf); free(sg); sg = NULL; nsg = capsg = 0;
    if (errp) *errp = NULL;
    return out.buf;
}
