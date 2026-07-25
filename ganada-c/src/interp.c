/* interp.c — environments, evaluator, signals, import/module, suggestions */
#include "ganada.h"
#include <math.h>
#include <stdarg.h>

/* ---------------------------------------------------------------- errors */
void g_throw(Interp *it, int code) {
    if (!it->top) { fprintf(stderr, "fatal: uncaught signal %d\n", code); exit(2); }
    _longjmp(it->top->jb, code);
}

void g_error(Interp *it, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *buf = malloc(1024);
    vsnprintf(buf, 1024, fmt, ap);
    va_end(ap);
    free(it->err);
    it->err = buf;
    g_throw(it, GS_ERR);
}

#define PUSH_H(it, h) do { (h).prev = (it)->top; (it)->top = &(h); } while (0)
#define POP_H(it) ((it)->top = (it)->top->prev)

/* ---------------------------------------------------------------- interp */
static uint64_t ptr_hash(const void *p) {
    uint64_t x = (uint64_t)(uintptr_t)p;
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31; return x;
}

int builtin_id_of(Interp *it, const char *name) {
    uint32_t m = it->bmap_cap - 1, p = (uint32_t)ptr_hash(name) & m;
    while (it->bmap[p].ptr) {
        if (it->bmap[p].ptr == name) return it->bmap[p].id;
        p = (p + 1) & m;
    }
    return -1;
}

Interp *interp_new(void) {
    Interp *it = calloc(1, sizeof(Interp));
    gc_set_interp(it);
    it->out = stdout;
    it->base_dir = strdup(".");
    it->ai_model = str_from(STR_DEFAULT_MODEL);
    for (int i = 0; i < NUM_KEYWORDS; i++)
        it->kw_ptr[i] = intern_cz(it, KEYWORD_TABLE[i]);
    for (int i = 0; i < B_COUNT; i++)
        it->builtin_ptr[i] = intern_cz(it, BUILTIN_NAMES[i]);
    it->goreugi_ptr = intern_cz(it, STR_GOREUGI);
    it->bmap_cap = 512;
    it->bmap = calloc(it->bmap_cap, sizeof(PtrEnt));
    for (int i = 0; i < B_COUNT; i++) {
        uint32_t m = it->bmap_cap - 1, p = (uint32_t)ptr_hash(it->builtin_ptr[i]) & m;
        while (it->bmap[p].ptr) p = (p + 1) & m;
        it->bmap[p].ptr = it->builtin_ptr[i];
        it->bmap[p].id = i;
    }
    it->g = env_new(it, NULL);
    return it;
}

/* ---------------------------------------------------------------- env */
Env *env_new(Interp *it, Env *parent) {
    Env *e;
    if (it->env_pool) { e = it->env_pool; it->env_pool = e->pool_next; }
    else {
        /* envs are pooled, never freed; the gc header only lets a stack word
         * pointing at an env be recognised as a root */
        e = gc_alloc_perm(sizeof(Env), GC_ENV);
        e->gcmark = 0;
    }
    e->parent = parent;
    e->rc = 1;
    e->n = 0; e->cap = ENV_INLINE;
    e->keys = e->ikeys; e->vals = e->ivals;
    e->shared = NULL;
    if (parent) parent->rc++;
    return e;
}

void env_release(Interp *it, Env *e) {
    if (!e) return;
    if (--e->rc > 0) return;
    Env *p = e->parent;
    if (e->keys != e->ikeys) { free(e->keys); free(e->vals); }
    e->pool_next = it->env_pool;
    it->env_pool = e;
    if (p) env_release(it, p);
}

static Value *env_lookup(Env *e, const char *name) {
    if (e->cap == ENV_INLINE) {
        for (int i = 0; i < e->n; i++)
            if (e->keys[i] == name) return &e->vals[i];
        return NULL;
    }
    uint32_t m = (uint32_t)e->cap - 1, p = (uint32_t)ptr_hash(name) & m;
    while (e->keys[p]) {
        if (e->keys[p] == name) return &e->vals[p];
        p = (p + 1) & m;
    }
    return NULL;
}

static void env_grow(Env *e, int ncap) {
    const char **nk = calloc((size_t)ncap, sizeof(char *));
    Value *nv = malloc(sizeof(Value) * (size_t)ncap);
    uint32_t m = (uint32_t)ncap - 1;
    for (int i = 0; i < e->cap; i++) {
        if (e->keys[i]) {
            uint32_t p = (uint32_t)ptr_hash(e->keys[i]) & m;
            while (nk[p]) p = (p + 1) & m;
            nk[p] = e->keys[i]; nv[p] = e->vals[i];
        }
    }
    if (e->keys != e->ikeys) { free(e->keys); free(e->vals); }
    e->keys = nk; e->vals = nv; e->cap = ncap;
}

void env_define(Env *e, const char *name, Value v) {
    if (e->shared) { dict_set(e->shared, v_str(str_from(name)), v); return; }
    if (e->cap == ENV_INLINE) {
        for (int i = 0; i < e->n; i++)
            if (e->keys[i] == name) { e->vals[i] = v; return; }
        if (e->n < ENV_INLINE) {
            e->keys[e->n] = name; e->vals[e->n] = v; e->n++;
            return;
        }
        env_grow(e, 32);
    }
    if ((e->n + 1) * 10 >= e->cap * 7) env_grow(e, e->cap * 2);
    uint32_t m = (uint32_t)e->cap - 1, p = (uint32_t)ptr_hash(name) & m;
    while (e->keys[p] && e->keys[p] != name) p = (p + 1) & m;
    if (!e->keys[p]) { e->keys[p] = name; e->n++; }
    e->vals[p] = v;
}

Value env_get(Interp *it, Env *e, const char *name) {
    for (Env *x = e; x; x = x->parent) {
        if (x->shared) {
            Value out;
            if (dict_get(x->shared, v_str(str_from(name)), &out)) return out;
            continue;
        }
        Value *v = env_lookup(x, name);
        if (v) return *v;
    }
    const char *sug = suggest_name(it, e, name);
    char *buf = malloc(strlen(name) + strlen(sug) + 128);
    snprintf(buf, strlen(name) + strlen(sug) + 128, ERR_NAME_UNDEFINED, name);
    strcat(buf, sug);
    g_error(it, "%s", buf);
}

void env_set_existing_or_define(Env *e, const char *name, Value v) {
    for (Env *x = e; x; x = x->parent) {
        if (x->shared) {
            Value tmp;
            if (dict_get(x->shared, v_str(str_from(name)), &tmp)) {
                dict_set(x->shared, v_str(str_from(name)), v);
                return;
            }
            continue;
        }
        Value *slot = env_lookup(x, name);
        if (slot) { *slot = v; return; }
    }
    env_define(e, name, v);
}

/* ---------------------------------------------------------------- difflib ratio */
static uint32_t *to_cps(const char *s, int *np) {
    int n = 0, cap = 16;
    uint32_t *out = malloc(sizeof(uint32_t) * cap);
    while (*s) {
        uint32_t adv;
        uint32_t cp = utf8_dec(s, &adv);
        if (n == cap) { cap *= 2; out = realloc(out, sizeof(uint32_t) * cap); }
        out[n++] = cp;
        s += adv;
    }
    *np = n;
    return out;
}

static void longest_match(const uint32_t *a, int alo, int ahi,
                          const uint32_t *b, int blo, int bhi,
                          int *bi, int *bj, int *bk) {
    *bi = alo; *bj = blo; *bk = 0;
    for (int i = alo; i < ahi; i++) {
        for (int j = blo; j < bhi; j++) {
            int k = 0;
            while (i + k < ahi && j + k < bhi && a[i + k] == b[j + k]) k++;
            if (k > *bk) { *bi = i; *bj = j; *bk = k; }
        }
    }
}

static long match_sum(const uint32_t *a, int alo, int ahi,
                      const uint32_t *b, int blo, int bhi) {
    if (alo >= ahi || blo >= bhi) return 0;
    int i, j, k;
    longest_match(a, alo, ahi, b, blo, bhi, &i, &j, &k);
    if (k == 0) return 0;
    return k + match_sum(a, alo, i, b, blo, j)
             + match_sum(a, i + k, ahi, b, j + k, bhi);
}

double seq_ratio(const char *sa, const char *sb) {
    int la, lb;
    uint32_t *a = to_cps(sa, &la), *b = to_cps(sb, &lb);
    double r;
    if (la + lb == 0) r = 1.0;
    else {
        long m = match_sum(a, 0, la, b, 0, lb);
        r = 2.0 * (double)m / (double)(la + lb);
    }
    free(a); free(b);
    return r;
}

/* heap string: best suggestion or "" — caller frees... we leak it (short lived) */
const char *suggest_name(Interp *it, Env *env, const char *name) {
    double best = 0.6;
    const char *bestname = NULL;
    int nbetter = 0;
    /* gather candidates: builtins first, then env chain */
    for (int i = 0; i < B_COUNT; i++) {
        const char *c = it->builtin_ptr[i];
        if (!strcmp(c, name)) continue;
        double r = seq_ratio(name, c);
        if (r >= 0.6 && (r > best || (r == best && bestname && strcmp(c, bestname) > 0))) {
            best = r; bestname = c; nbetter++;
        }
    }
    for (Env *x = env; x; x = x->parent) {
        if (x->shared) continue;
        int lim = x->cap;
        for (int i = 0; i < lim; i++) {
            const char *c = NULL;
            if (x->cap == ENV_INLINE) { if (i < x->n) c = x->keys[i]; }
            else c = x->keys[i];
            if (!c || !strcmp(c, name)) continue;
            double r = seq_ratio(name, c);
            if (r >= 0.6 && (r > best || (r == best && bestname && strcmp(c, bestname) > 0))) {
                best = r; bestname = c; nbetter++;
            }
        }
    }
    (void)nbetter;
    if (!bestname) return "";
    size_t need = strlen(bestname) + 32;
    char *buf = malloc(need);
    snprintf(buf, need, FMT_SUGGEST, bestname);
    return buf;
}

static const char *suggest_key(Dict *d, Value key) {
    if (key.tag != VT_STR) return "";
    /* count string keys */
    long nstr = 0;
    for (long i = 0; i < d->n; i++)
        if (d->items[i].key.tag == VT_STR) nstr++;
    if (!nstr) return "";
    double best = 0.6;
    const char *bestname = NULL;
    for (long i = 0; i < d->n; i++) {
        if (d->items[i].key.tag != VT_STR) continue;
        const char *c = d->items[i].key.as.s->data;
        if (!strcmp(c, key.as.s->data)) continue;
        double r = seq_ratio(key.as.s->data, c);
        if (r >= 0.6 && (r > best || (r == best && bestname && strcmp(c, bestname) > 0))) {
            best = r; bestname = c;
        }
    }
    if (bestname) {
        size_t need = strlen(bestname) + 32;
        char *buf = malloc(need);
        snprintf(buf, need, FMT_SUGGEST, bestname);
        return buf;
    }
    /* list up to 6 string keys */
    size_t cap = 128, n = 0;
    char *buf = malloc(cap); buf[0] = 0;
    long shown = 0;
    for (long i = 0; i < d->n && shown < 6; i++) {
        if (d->items[i].key.tag != VT_STR) continue;
        Str *k = d->items[i].key.as.s;
        size_t need = n + k->len + 4;
        if (need > cap) { cap *= 2; buf = realloc(buf, cap); }
        if (shown) { memcpy(buf + n, ", ", 2); n += 2; }
        memcpy(buf + n, k->data, k->len); n += k->len; buf[n] = 0;
        shown++;
    }
    size_t need2 = n + 64;
    char *out = malloc(need2);
    snprintf(out, need2, FMT_KEYS_LIST, buf);
    if (nstr > 6) strcat(out, STR_ELLIPSIS);
    free(buf);
    return out;
}

/* ---------------------------------------------------------------- eval */
static Value eval(Interp *it, Node *node, Env *env);

static Value num_add(Interp *it, Value a, Value b);
static Value num_sub(Interp *it, Value a, Value b);
static Value num_mul(Interp *it, Value a, Value b);

static int is_num_v(Value v) { return v.tag == VT_INT || v.tag == VT_FLOAT || v.tag == VT_BOOL; }

static double to_dbl(Value v) {
    return v.tag == VT_INT ? (double)v.as.i : v.tag == VT_BOOL ? (double)v.as.b : v.as.f;
}

static void type_err2(Interp *it, const char *op, Value a, Value b) {
    size_t need = strlen(v_typename(a)) + strlen(v_typename(b)) + 64;
    char *tb = malloc(need);
    snprintf(tb, need, "%s%s%s", v_typename(a), STR_WA, v_typename(b));
    g_error(it, ERR_BINOP_TYPES, op, tb);
}

static void type_err1(Interp *it, const char *op, Value a) {
    g_error(it, ERR_BINOP_TYPES, op, v_typename(a));
}

static Value binop(Interp *it, int op, Node *ln, Node *rn, Env *env) {
    if (op == OP_AND) {
        Value l = eval(it, ln, env);
        return v_truthy(l) ? eval(it, rn, env) : l;
    }
    if (op == OP_OR) {
        Value l = eval(it, ln, env);
        return v_truthy(l) ? l : eval(it, rn, env);
    }
    Value a = eval(it, ln, env);
    Value b = eval(it, rn, env);
    switch (op) {
    case OP_ADD: {
        if (a.tag == VT_STR || b.tag == VT_STR) {
            Str *s = str_concat(v_stringify(a), v_stringify(b));
            s->flags = 0;    /* concat of stringified values is plain text */
            if (a.tag == VT_STR && b.tag == VT_STR)
                s->flags = a.as.s->flags & b.as.s->flags;
            return v_str(s);
        }
        if (a.tag == VT_LIST && b.tag == VT_LIST) {
            List *l = list_new(a.as.l->n + b.as.l->n);
            for (long i = 0; i < a.as.l->n; i++) list_push(l, a.as.l->items[i]);
            for (long i = 0; i < b.as.l->n; i++) list_push(l, b.as.l->items[i]);
            return v_list(l);
        }
        return num_add(it, a, b);
    }
    case OP_SUB: return num_sub(it, a, b);
    case OP_MUL: {
        /* sequence repetition: str*int, list*int (either order) */
        Value seq = a, cnt = b;
        if (is_num_v(a) && (b.tag == VT_STR || b.tag == VT_LIST)) { seq = b; cnt = a; }
        if ((seq.tag == VT_STR || seq.tag == VT_LIST) && is_num_v(cnt) && cnt.tag != VT_FLOAT) {
            int64_t n = cnt.tag == VT_INT ? cnt.as.i : (int64_t)cnt.as.b;
            if (seq.tag == VT_STR) {
                if (n <= 0) return v_str(str_from(""));
                uint32_t bl = seq.as.s->len;
                Str *r = gc_alloc(sizeof(Str) + bl * (uint64_t)n + 1, GC_STR);
                r->len = bl * (uint32_t)n; r->cplen = UINT32_MAX; r->flags = seq.as.s->flags;
                for (int64_t i = 0; i < n; i++) memcpy(r->data + i * bl, seq.as.s->data, bl);
                r->data[r->len] = 0;
                return v_str(r);
            }
            List *l = list_new(n > 0 ? seq.as.l->n * n : 0);
            for (int64_t i = 0; i < n; i++)
                for (long j = 0; j < seq.as.l->n; j++) list_push(l, seq.as.l->items[j]);
            return v_list(l);
        }
        return num_mul(it, a, b);
    }
    case OP_DIV: {
        if (!is_num_v(a) || !is_num_v(b)) type_err2(it, "/", a, b);
        if (to_dbl(b) == 0) G_ERR0(it, ERR_DIV_ZERO);
        return v_float(to_dbl(a) / to_dbl(b));
    }
    case OP_MOD: {
        if (!is_num_v(a) || !is_num_v(b)) type_err2(it, "%", a, b);
        if (to_dbl(b) == 0) G_ERR0(it, ERR_DIV_ZERO);
        if (a.tag == VT_INT && b.tag == VT_INT && b.as.i != 0) {
            int64_t r = a.as.i % b.as.i;
            if (r != 0 && ((r < 0) != (b.as.i < 0))) r += b.as.i;
            return v_int(r);
        }
        double da = to_dbl(a), db = to_dbl(b);
        double r = fmod(da, db);
        if (r != 0 && signbit(r) != signbit(db)) r += db;
        return v_float(r);
    }
    case OP_EQ: return v_bool(v_eq(a, b));
    case OP_NE: return v_bool(!v_eq(a, b));
    case OP_LT: case OP_GT: case OP_LE: case OP_GE: {
        bool err;
        int c = v_cmp(a, b, &err);
        if (err) {
            const char *os = op == OP_LT ? "<" : op == OP_GT ? ">" : op == OP_LE ? "<=" : ">=";
            type_err2(it, os, a, b);
        }
        return v_bool(op == OP_LT ? c < 0 : op == OP_GT ? c > 0 : op == OP_LE ? c <= 0 : c >= 0);
    }
    }
    g_error(it, "unknown op %d", op);
}

static Value num_add(Interp *it, Value a, Value b) {
    if (!is_num_v(a) || !is_num_v(b)) type_err2(it, "+", a, b);
    if (a.tag == VT_FLOAT || b.tag == VT_FLOAT) return v_float(to_dbl(a) + to_dbl(b));
    return v_int((a.tag == VT_INT ? a.as.i : a.as.b) + (b.tag == VT_INT ? b.as.i : b.as.b));
}

static Value num_sub(Interp *it, Value a, Value b) {
    if (!is_num_v(a) || !is_num_v(b)) type_err2(it, "-", a, b);
    if (a.tag == VT_FLOAT || b.tag == VT_FLOAT) return v_float(to_dbl(a) - to_dbl(b));
    return v_int((a.tag == VT_INT ? a.as.i : a.as.b) - (b.tag == VT_INT ? b.as.i : b.as.b));
}

static Value num_mul(Interp *it, Value a, Value b) {
    if (!is_num_v(a) || !is_num_v(b)) type_err2(it, "*", a, b);
    if (a.tag == VT_FLOAT || b.tag == VT_FLOAT) return v_float(to_dbl(a) * to_dbl(b));
    return v_int((a.tag == VT_INT ? a.as.i : a.as.b) * (b.tag == VT_INT ? b.as.i : b.as.b));
}

/* ---------------------------------------------------------------- index/slice */
static long py_clamp(long i, long n, bool is_start) {
    (void)is_start;
    if (i < 0) i += n;
    if (i < 0) i = 0;
    if (i > n) i = n;
    return i;
}

static Value index_get(Interp *it, Value obj, Value idx) {
    if (obj.tag == VT_LIST || obj.tag == VT_STR) {
        long n = obj.tag == VT_LIST ? obj.as.l->n : (long)str_cplen(obj.as.s);
        long i;
        if (idx.tag == VT_INT) i = (long)idx.as.i;
        else if (idx.tag == VT_BOOL) i = idx.as.b;
        else G_ERR0(it, ERR_INDEX_INT);
        if (i < -n || i >= n) {
            Str *is = v_stringify(idx);
            g_error(it, ERR_INDEX_RANGE, is->data);
        }
        if (i < 0) i += n;
        if (obj.tag == VT_LIST) return obj.as.l->items[i];
        return v_str(str_char_at(obj.as.s, i));
    }
    if (obj.tag == VT_DICT) {
        Value out;
        if (dict_get(obj.as.d, idx, &out)) return out;
        Str *ks = v_stringify(idx);
        const char *sug = suggest_key(obj.as.d, idx);
        char *buf = malloc(ks->len + strlen(sug) + 64);
        snprintf(buf, ks->len + 64, ERR_KEY_MISSING, ks->data);
        strcat(buf, sug);
        g_error(it, "%s", buf);
    }
    G_ERR0(it, ERR_NOT_INDEXABLE);
}

static void index_set(Interp *it, Value obj, Value idx, Value v) {
    if (obj.tag == VT_LIST) {
        long n = obj.as.l->n, i;
        if (idx.tag == VT_INT) i = (long)idx.as.i;
        else if (idx.tag == VT_BOOL) i = idx.as.b;
        else G_ERR0(it, ERR_INDEX_INT);
        if (i < -n || i >= n) {
            Str *is = v_stringify(idx);
            g_error(it, ERR_INDEX_RANGE, is->data);
        }
        if (i < 0) i += n;
        obj.as.l->items[i] = v;
        return;
    }
    if (obj.tag == VT_DICT) { dict_set(obj.as.d, idx, v); return; }
    G_ERR0(it, ERR_NOT_SETTABLE);
}

static Value slice_eval(Interp *it, Value obj, Node *sn, Node *en, Env *env) {
    if (obj.tag != VT_LIST && obj.tag != VT_STR) G_ERR0(it, ERR_NOT_SLICEABLE);
    long n = obj.tag == VT_LIST ? obj.as.l->n : (long)str_cplen(obj.as.s);
    long a = 0, b = n;
    if (sn) {
        Value sv = eval(it, sn, env);
        if (sv.tag == VT_INT) a = (long)sv.as.i;
        else if (sv.tag == VT_BOOL) a = sv.as.b;
        else a = 0;
    }
    if (en) {
        Value ev = eval(it, en, env);
        if (ev.tag == VT_INT) b = (long)ev.as.i;
        else if (ev.tag == VT_BOOL) b = ev.as.b;
        else b = n;
    }
    a = py_clamp(a, n, true); b = py_clamp(b, n, false);
    if (obj.tag == VT_STR) return v_str(str_sub_cp(obj.as.s, a, b));
    List *l = list_new(b > a ? b - a : 0);
    for (long i = a; i < b; i++) list_push(l, obj.as.l->items[i]);
    return v_list(l);
}

/* ---------------------------------------------------------------- funcs */
Value apply_func(Interp *it, Value fnv, Value *args, int nargs) {
    if (fnv.tag != VT_FUNC) G_ERR0(it, ERR_NOT_CALLABLE);
    Func *fn = fnv.as.fn;
    int required = 0;
    for (int i = 0; i < fn->nparams; i++) if (!fn->pdefs[i]) required++;
    if (nargs < required || nargs > fn->nparams) {
        char need[32];
        if (required == fn->nparams) snprintf(need, sizeof need, "%d", required);
        else snprintf(need, sizeof need, "%d~%d", required, fn->nparams);
        g_error(it, ERR_ARG_COUNT, fn->name, need, nargs);
    }
    Env *local = env_new(it, fn->env);
    Handler h;
    PUSH_H(it, h);
    volatile int depth_added = 0;
    int code = _setjmp(h.jb);
    if (code == 0) {
        for (int i = 0; i < fn->nparams; i++) {
            Value v = i < nargs ? args[i] : eval(it, fn->pdefs[i], local);
            env_define(local, fn->pnames[i], v);
        }
        it->depth++;
        depth_added = 1;
        if (it->depth > 700) G_ERR0(it, ERR_RECURSION);
        exec_block(it, fn->body, local);
        it->depth--;
        POP_H(it);
        env_release(it, local);
        return v_nil();
    }
    if (depth_added) it->depth--;
    POP_H(it);
    env_release(it, local);
    if (code == GS_RET) return it->retval;
    g_throw(it, code);
}

Value eval_node(Interp *it, Node *node, Env *env) { return eval(it, node, env); }

static Value call_node(Interp *it, Node *node, Env *env) {
    Node *callee = node->a;
    /* goreugi(cond, a, b) — lazy special form */
    if (callee->kind == N_VAR && callee->name == it->goreugi_ptr && node->nitems == 3) {
        Value cond = eval(it, node->items[0], env);
        return eval(it, v_truthy(cond) ? node->items[1] : node->items[2], env);
    }
    Value stack_args[16];
    Value *args = stack_args;
    List *spill = NULL;                  /* overflow args live in a gc value */
    if (node->nitems > 16) {
        spill = list_new(node->nitems);
        for (int i = 0; i < node->nitems; i++) list_push(spill, v_nil());
        args = spill->items;
    }
    for (int i = 0; i < node->nitems; i++) args[i] = eval(it, node->items[i], env);
    if (callee->kind == N_VAR) {
        int id = builtin_id_of(it, callee->name);
        if (id >= 0) {
            Value r = builtin_dispatch(it, id, args, node->nitems);
            gc_keep_alive(spill);
            return r;
        }
    }
    Value fnv = eval(it, callee, env);
    Value r = apply_func(it, fnv, args, node->nitems);
    gc_keep_alive(spill);
    return r;
}

static Value eval(Interp *it, Node *node, Env *env) {
    switch (node->kind) {
    case N_LIT: return node->lit;
    case N_VAR: return env_get(it, env, node->name);
    case N_UN: {
        Value v = eval(it, node->a, env);
        if (node->op == OP_NOT) return v_bool(!v_truthy(v));
        if (v.tag == VT_INT) return v_int(-v.as.i);
        if (v.tag == VT_FLOAT) return v_float(-v.as.f);
        if (v.tag == VT_BOOL) return v_int(-v.as.b);
        type_err1(it, "-", v);
    }
    case N_BIN: return binop(it, node->op, node->a, node->b, env);
    case N_LIST: {
        List *l = list_new(node->nitems);
        for (int i = 0; i < node->nitems; i++) list_push(l, eval(it, node->items[i], env));
        return v_list(l);
    }
    case N_DICT: {
        Dict *d = dict_new();
        for (int i = 0; i < node->nitems; i++) {
            Value k = eval(it, node->items[2 * i], env);
            Value v = eval(it, node->items[2 * i + 1], env);
            if (k.tag == VT_LIST || k.tag == VT_DICT || k.tag == VT_FUNC)
                g_error(it, FMT_CALL_ERR, STR_TYPE_DICT, "unhashable type");
            dict_set(d, k, v);
        }
        return v_dict(d);
    }
    case N_LAMBDA: {
        Func *fn = gc_alloc(sizeof(Func), GC_FUNC);
        fn->name = STR_LAMBDA_NAME;
        fn->pnames = node->pnames; fn->pdefs = node->pdefs; fn->nparams = node->nparams;
        fn->body = node->a;
        fn->env = env; env->rc++;
        Value v; v.tag = VT_FUNC; v.as.fn = fn;
        return v;
    }
    case N_INDEX: {
        Value obj = eval(it, node->a, env);
        Value idx = eval(it, node->b, env);
        return index_get(it, obj, idx);
    }
    case N_SLICE: {
        Value obj = eval(it, node->a, env);
        return slice_eval(it, obj, node->b, node->c, env);
    }
    case N_CALL: return call_node(it, node, env);
    default:
        g_error(it, "bad expr node %d", node->kind);
    }
}

/* ---------------------------------------------------------------- import */
static char *path_join_norm(const char *base, const char *path) {
    size_t need = strlen(base) + strlen(path) + 2;
    char *full = malloc(need);
    snprintf(full, need, "%s/%s", base, path);
    /* normalize: split on '/', resolve . and .. */
    int abs = full[0] == '/';
    char **parts = malloc(sizeof(char *) * (strlen(full) / 2 + 2));
    int np = 0;
    char *save = NULL;
    for (char *tok = strtok_r(full, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (!strcmp(tok, ".")) continue;
        if (!strcmp(tok, "..")) { if (np > 0) np--; continue; }
        parts[np++] = tok;
    }
    size_t outcap = need + 1;      /* strtok_r cut `full` up: use its old length */
    char *out = malloc(outcap);
    out[0] = 0;
    if (abs) strcat(out, "/");
    for (int i = 0; i < np; i++) {
        strcat(out, parts[i]);
        if (i + 1 < np) strcat(out, "/");
    }
    if (!np && !abs) strcat(out, ".");
    free(parts); free(full);
    return out;
}

static char *path_dirname(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return strdup(".");
    if (slash == path) return strdup("/");
    size_t n = (size_t)(slash - path);
    char *out = malloc(n + 1);
    memcpy(out, path, n); out[n] = 0;
    return out;
}

char *read_file_utf8(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n && n > 0) { fclose(f); free(buf); return NULL; }
    fclose(f);
    buf[n] = 0;
    return buf;
}

static void do_import(Interp *it, const char *path) {
    char *full = path_join_norm(it->base_dir, path);
    for (int i = 0; i < it->nimported; i++)
        if (!strcmp(it->imported[i], full)) { free(full); return; }
    if (it->nimported == it->capimported) {
        it->capimported = it->capimported ? it->capimported * 2 : 8;
        it->imported = realloc(it->imported, sizeof(char *) * it->capimported);
    }
    it->imported[it->nimported++] = full;
    char *src = read_file_utf8(full);
    if (!src) g_error(it, ERR_IMPORT_FAIL, path);
    int n;
    Tok *toks = lex_all(it, src, &n);
    Node *ast = parse_all(it, toks);
    free(toks);                    /* the ast keeps no Tok pointers */
    char *prev = it->base_dir;
    it->base_dir = path_dirname(full);
    exec_block(it, ast, it->g);
    it->base_dir = prev;
    free(src);
}

/* ---------------------------------------------------------------- exec */
static void exec(Interp *it, Node *node, Env *env) {
    switch (node->kind) {
    case N_BLOCK: {
        Env *inner = env_new(it, env);
        Handler h;
        PUSH_H(it, h);
        int code = _setjmp(h.jb);
        if (code == 0) { exec_block(it, node, inner); POP_H(it); env_release(it, inner); }
        else { POP_H(it); env_release(it, inner); g_throw(it, code); }
        return;
    }
    case N_IMPORT: do_import(it, node->str); return;
    case N_ASSIGN: {
        Value v = eval(it, node->a, env);
        env_set_existing_or_define(env, node->name, v);
        return;
    }
    case N_DESTRUCT: {
        Value vals = eval(it, node->a, env);
        long n = -1;
        if (vals.tag == VT_LIST) n = vals.as.l->n;
        else if (vals.tag == VT_STR) n = (long)str_cplen(vals.as.s);
        if (n < 0 || n != node->nparams)
            g_error(it, ERR_DESTRUCTURE, node->nparams);
        for (int i = 0; i < node->nparams; i++) {
            Value v = vals.tag == VT_LIST ? vals.as.l->items[i]
                                          : v_str(str_char_at(vals.as.s, i));
            env_set_existing_or_define(env, node->pnames[i], v);
        }
        return;
    }
    case N_FUNC: {
        Func *fn = gc_alloc(sizeof(Func), GC_FUNC);
        fn->name = node->name;
        fn->pnames = node->pnames; fn->pdefs = node->pdefs; fn->nparams = node->nparams;
        fn->body = node->a;
        fn->env = env; env->rc++;
        Value v; v.tag = VT_FUNC; v.as.fn = fn;
        env_define(env, node->name, v);
        return;
    }
    case N_IF:
        if (v_truthy(eval(it, node->a, env))) exec_block(it, node->b, env);
        else if (node->c) {
            if (node->c->kind == N_IF) exec(it, node->c, env);
            else exec_block(it, node->c, env);
        }
        return;
    case N_WHILE: {
        Handler h;
        PUSH_H(it, h);
        for (;;) {
            int code = _setjmp(h.jb);
            if (code == 0) {
                if (!v_truthy(eval(it, node->a, env))) break;
                exec_block(it, node->b, env);
                continue;
            }
            if (code == GS_CONT) continue;
            if (code == GS_BRK) break;
            POP_H(it);
            g_throw(it, code);
        }
        POP_H(it);
        return;
    }
    case N_FOR: {
        Value a = eval(it, node->a, env);
        Value b = eval(it, node->b, env);
        Value step = node->c ? eval(it, node->c, env) : v_int(1);
        if (!is_num_v(step)) G_ERR0(it, ERR_STEP_TYPE);
        double sd = to_dbl(step);
        if (sd == 0) G_ERR0(it, ERR_STEP_ZERO);
        Node *body = node->items[0];
        bool use_int = a.tag == VT_INT && b.tag == VT_INT && step.tag == VT_INT;
        Handler h;
        PUSH_H(it, h);
        volatile double vi = to_dbl(a);
        volatile int64_t ii = use_int ? a.as.i : 0;
        int64_t iend = use_int ? b.as.i : 0, ist = use_int ? step.as.i : 1;
        double dend = to_dbl(b);
        Env *volatile loop = NULL;      /* survives the jump: 계속/멈춤 must free it */
        for (;;) {
            if (use_int) { if (ist > 0 ? ii > iend : ii < iend) break; }
            else { if (sd > 0 ? vi > dend : vi < dend) break; }
            int code = _setjmp(h.jb);
            if (code == 0) {
                loop = env_new(it, env);
                env_define(loop, node->name, use_int ? v_int(ii) : v_float(vi));
                exec_block(it, body, loop);
                env_release(it, loop);
                loop = NULL;
            } else {
                if (loop) { env_release(it, loop); loop = NULL; }
                if (code == GS_BRK) break;
                if (code != GS_CONT) { POP_H(it); g_throw(it, code); }
            }
            if (use_int) ii += ist; else vi += sd;
        }
        POP_H(it);
        return;
    }
    case N_FOREACH: {
        Value coll = eval(it, node->a, env);
        long n;
        if (coll.tag == VT_DICT) n = coll.as.d->n;
        else if (coll.tag == VT_LIST) n = coll.as.l->n;
        else if (coll.tag == VT_STR) n = (long)str_cplen(coll.as.s);
        else G_ERR0(it, ERR_NOT_ITERABLE);
        Node *body = node->b;
        Handler h;
        PUSH_H(it, h);
        volatile long idx = 0;
        Env *volatile loop = NULL;      /* survives the jump: 계속/멈춤 must free it */
        while (idx < n) {
            int code = _setjmp(h.jb);
            if (code == 0) {
                Value item;
                if (coll.tag == VT_DICT) item = coll.as.d->items[idx].key;
                else if (coll.tag == VT_LIST) item = coll.as.l->items[idx];
                else item = v_str(str_char_at(coll.as.s, idx));
                loop = env_new(it, env);
                env_define(loop, node->name, item);
                if (node->str) env_define(loop, node->str, v_int(idx));
                exec_block(it, body, loop);
                env_release(it, loop);
                loop = NULL;
            } else {
                if (loop) { env_release(it, loop); loop = NULL; }
                if (code == GS_BRK) break;
                if (code != GS_CONT) { POP_H(it); g_throw(it, code); }
            }
            idx++;
        }
        POP_H(it);
        return;
    }
    case N_BREAK: g_throw(it, GS_BRK);
    case N_CONT: g_throw(it, GS_CONT);
    case N_TRY: {
        Handler h;
        PUSH_H(it, h);
        int code = _setjmp(h.jb);
        if (code == 0) {
            exec_block(it, node->a, env);
            POP_H(it);
            return;
        }
        POP_H(it);
        if (code != GS_ERR) g_throw(it, code);
        Env *scope = env_new(it, env);
        env_define(scope, node->name, v_str(str_from(it->err)));
        Handler h2;
        PUSH_H(it, h2);
        int code2 = _setjmp(h2.jb);
        if (code2 == 0) { exec_block(it, node->b, scope); POP_H(it); env_release(it, scope); }
        else { POP_H(it); env_release(it, scope); g_throw(it, code2); }
        return;
    }
    case N_RETURN: {
        it->retval = node->a ? eval(it, node->a, env) : v_nil();
        g_throw(it, GS_RET);
    }
    case N_SETINDEX: {
        Value obj = eval(it, node->a, env);
        Value idx = eval(it, node->b, env);
        Value v = eval(it, node->c, env);
        index_set(it, obj, idx, v);
        return;
    }
    case N_EXPRSTMT: eval(it, node->a, env); return;
    default:
        g_error(it, "bad stmt node %d", node->kind);
    }
}

void exec_block(Interp *it, Node *blk, Env *env) {
    for (int i = 0; i < blk->nitems; i++) {
        Node *st = blk->items[i];
        if (st->kind == N_STMT) {
            it->cur_line = st->line;
            exec(it, st->a, env);
        } else {
            exec(it, st, env);
        }
    }
}

/* ---------------------------------------------------------------- run */
char *interp_run_source(Interp *it, const char *src) {
    Handler h;
    PUSH_H(it, h);
    int code = _setjmp(h.jb);
    if (code == 0) {
        int n;
        Tok *toks = lex_all(it, src, &n);
        Node *ast = parse_all(it, toks);
        free(toks);                /* the ast keeps no Tok pointers */
        exec_block(it, ast, it->g);
        POP_H(it);
        return NULL;
    }
    POP_H(it);
    if (code == GS_RET) return NULL;   /* bare top-level return: stop quietly */
    char *msg;
    if (code == GS_ERR) {
        msg = it->err;
        it->err = NULL;
    } else {
        const char *base = ERR_BREAK_OUTSIDE;
        msg = malloc(strlen(base) + 32);
        sprintf(msg, "[%d%s] %s", it->cur_line, "\xED\x96\x89", base);
    }
    if (msg[0] != '[') {
        size_t need = strlen(msg) + 32;
        char *nm = malloc(need);
        snprintf(nm, need, "[%d\xED\x96\x89] %s", it->cur_line, msg);
        free(msg);
        msg = nm;
    }
    return msg;
}
