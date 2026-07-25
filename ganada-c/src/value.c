/* value.c — values, strings (utf8), lists, dicts, formatting, interning */
#include "ganada.h"
#include <math.h>

Value v_nil(void) { Value v; v.tag = VT_NIL; v.as.i = 0; return v; }
Value v_bool(int b) { Value v; v.tag = VT_BOOL; v.as.b = b != 0; return v; }
Value v_int(int64_t i) { Value v; v.tag = VT_INT; v.as.i = i; return v; }
Value v_float(double f) { Value v; v.tag = VT_FLOAT; v.as.f = f; return v; }
Value v_str(Str *s) { Value v; v.tag = VT_STR; v.as.s = s; return v; }
Value v_list(List *l) { Value v; v.tag = VT_LIST; v.as.l = l; return v; }
Value v_dict(Dict *d) { Value v; v.tag = VT_DICT; v.as.d = d; return v; }

/* ---------------------------------------------------------------- strings */
Str *str_new(const char *data, uint32_t len) {
    Str *s = gc_alloc(sizeof(Str) + len + 1, GC_STR);
    s->len = len; s->cplen = UINT32_MAX; s->flags = 0;
    memcpy(s->data, data, len); s->data[len] = 0;
    return s;
}

Str *str_from(const char *cstr) { return str_new(cstr, (uint32_t)strlen(cstr)); }

Str *str_perm(const char *cstr) {
    uint32_t len = (uint32_t)strlen(cstr);
    Str *s = gc_alloc_perm(sizeof(Str) + len + 1, GC_STR);
    s->len = len; s->cplen = UINT32_MAX; s->flags = 0;
    memcpy(s->data, cstr, len); s->data[len] = 0;
    return s;
}

Str *str_own(char *data, uint32_t len) {
    Str *s = gc_alloc(sizeof(Str) + len + 1, GC_STR);
    s->len = len; s->cplen = UINT32_MAX; s->flags = 0;
    memcpy(s->data, data, len); s->data[len] = 0;
    free(data);
    return s;
}

Str *str_concat(Str *a, Str *b) {
    Str *s = gc_alloc(sizeof(Str) + a->len + b->len + 1, GC_STR);
    s->len = a->len + b->len; s->cplen = UINT32_MAX;
    s->flags = a->flags & b->flags;      /* html only if both html */
    memcpy(s->data, a->data, a->len);
    memcpy(s->data + a->len, b->data, b->len);
    s->data[s->len] = 0;
    return s;
}

Str *str_concat3(Str *a, Str *b, Str *c) {
    uint32_t n = a->len + b->len + c->len;
    Str *s = gc_alloc(sizeof(Str) + n + 1, GC_STR);
    s->len = n; s->cplen = UINT32_MAX;
    s->flags = a->flags & b->flags & c->flags;
    memcpy(s->data, a->data, a->len);
    memcpy(s->data + a->len, b->data, b->len);
    memcpy(s->data + a->len + b->len, c->data, c->len);
    s->data[n] = 0;
    return s;
}

/* decode one codepoint; returns codepoint, advances *adv by bytes read */
uint32_t utf8_dec(const char *p, uint32_t *adv) {
    const unsigned char *u = (const unsigned char *)p;
    if (u[0] < 0x80) { *adv = 1; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0) { *adv = 2; return ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); }
    if ((u[0] & 0xF0) == 0xE0) { *adv = 3; return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); }
    if ((u[0] & 0xF8) == 0xF0) { *adv = 4; return ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F); }
    *adv = 1; return 0xFFFD;
}

uint32_t str_cplen(Str *s) {
    if (s->cplen != UINT32_MAX) return s->cplen;
    uint32_t n = 0;
    for (uint32_t i = 0; i < s->len;)
        i += (unsigned char)s->data[i] < 0x80 ? 1 :
             ((unsigned char)s->data[i] & 0xE0) == 0xC0 ? 2 :
             ((unsigned char)s->data[i] & 0xF0) == 0xE0 ? 3 : 4, n++;
    s->cplen = n;
    return n;
}

/* byte offset of codepoint index; negative counts from end; clamps to [0, len] */
long str_cp_byteoff(Str *s, long cpidx) {
    uint32_t total = str_cplen(s);
    if (cpidx < 0) cpidx += total;
    if (cpidx < 0) cpidx = 0;
    if (cpidx >= (long)total) return s->len;
    long off = 0;
    for (long k = 0; k < cpidx; k++) {
        unsigned char c = (unsigned char)s->data[off];
        off += c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
    }
    return off;
}

Str *str_sub_cp(Str *s, long a, long b) {
    long ba = str_cp_byteoff(s, a), bb = str_cp_byteoff(s, b);
    if (bb < ba) bb = ba;
    Str *r = str_new(s->data + ba, (uint32_t)(bb - ba));
    r->flags = s->flags;
    return r;
}

Str *str_char_at(Str *s, long cpidx) { return str_sub_cp(s, cpidx, cpidx + 1); }

/* ---------------------------------------------------------------- lists */
List *list_new(long cap) {
    List *l = gc_alloc(sizeof(List), GC_LIST);
    l->n = 0; l->cap = cap > 4 ? cap : 4;
    l->items = malloc(sizeof(Value) * l->cap);
    return l;
}

void list_push(List *l, Value v) {
    if (l->n == l->cap) {
        l->cap *= 2;
        l->items = realloc(l->items, sizeof(Value) * l->cap);
    }
    l->items[l->n++] = v;
}

/* ---------------------------------------------------------------- dicts */
static uint64_t mix64(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31; return x;
}

static uint64_t key_hash(Value k) {
    switch (k.tag) {
    case VT_STR: {
        uint64_t h = 1469598103934665603ULL;
        for (uint32_t i = 0; i < k.as.s->len; i++) {
            h ^= (unsigned char)k.as.s->data[i]; h *= 1099511628211ULL;
        }
        return h;
    }
    case VT_INT: return mix64((uint64_t)k.as.i + 0x9e3779b9ULL);
    case VT_FLOAT: {
        double f = k.as.f;
        if (f == floor(f) && fabs(f) < 9.0e18)
            return mix64((uint64_t)(int64_t)f + 0x9e3779b9ULL);   /* 1.0 == 1 */
        uint64_t u; memcpy(&u, &f, 8);
        return mix64(u ^ 0x51ed275bULL);
    }
    case VT_BOOL: return mix64(0xbe11 + (uint64_t)k.as.b);
    case VT_NIL: return mix64(0xd1ce);
    default: return mix64((uint64_t)(uintptr_t)k.as.s);
    }
}

static void dict_rehash(Dict *d, uint32_t ncap) {
    free(d->idx);
    d->icap = ncap;
    d->idx = calloc(d->icap, sizeof(uint32_t));
    for (long i = 0; i < d->n; i++) {
        uint64_t h = key_hash(d->items[i].key);
        uint32_t m = d->icap - 1, p = (uint32_t)h & m;
        while (d->idx[p]) p = (p + 1) & m;
        d->idx[p] = (uint32_t)i + 1;
    }
}

Dict *dict_new(void) {
    Dict *d = gc_alloc(sizeof(Dict), GC_DICT);
    d->n = 0; d->cap = 8;
    d->items = malloc(sizeof(DEntry) * d->cap);
    d->icap = 16; d->idx = calloc(d->icap, sizeof(uint32_t));
    return d;
}

/* find position of key or -1 */
static long dict_find(Dict *d, Value key) {
    uint64_t h = key_hash(key);
    uint32_t m = d->icap - 1, p = (uint32_t)h & m;
    while (d->idx[p]) {
        if (v_eq(d->items[d->idx[p] - 1].key, key)) return (long)(d->idx[p] - 1);
        p = (p + 1) & m;
    }
    return -1;
}

void dict_set(Dict *d, Value key, Value val) {
    long pos = dict_find(d, key);
    if (pos >= 0) { d->items[pos].val = val; return; }
    if (d->n == d->cap) {
        d->cap *= 2;
        d->items = realloc(d->items, sizeof(DEntry) * d->cap);
    }
    if ((d->n + 1) * 10 >= d->icap * 7) dict_rehash(d, d->icap * 2);
    d->items[d->n].key = key;
    d->items[d->n].val = val;
    uint64_t h = key_hash(key);
    uint32_t m = d->icap - 1, p = (uint32_t)h & m;
    while (d->idx[p]) p = (p + 1) & m;
    d->idx[p] = (uint32_t)d->n + 1;
    d->n++;
}

bool dict_get(Dict *d, Value key, Value *out) {
    long pos = dict_find(d, key);
    if (pos < 0) return false;
    *out = d->items[pos].val;
    return true;
}

/* ---------------------------------------------------------------- truthy/eq/cmp */
bool v_truthy(Value v) {
    switch (v.tag) {
    case VT_LIST: return v.as.l->n > 0;
    case VT_DICT: return v.as.d->n > 0;
    case VT_STR: return v.as.s->len > 0;
    case VT_BOOL: return v.as.b != 0;
    case VT_NIL: return false;
    case VT_INT: return v.as.i != 0;
    case VT_FLOAT: return v.as.f != 0;
    default: return true;
    }
}

/* numeric coercion: returns 1 if numeric (int/float/bool), value in *d */
static int as_num(Value v, double *d) {
    if (v.tag == VT_INT) { *d = (double)v.as.i; return 1; }
    if (v.tag == VT_FLOAT) { *d = v.as.f; return 1; }
    if (v.tag == VT_BOOL) { *d = v.as.b ? 1.0 : 0.0; return 1; }
    return 0;
}

bool v_eq(Value a, Value b) {
    double da, db;
    if (as_num(a, &da) && as_num(b, &db)) {
        if (a.tag == VT_INT && b.tag == VT_INT) return a.as.i == b.as.i;
        return da == db;
    }
    if (a.tag != b.tag) return false;
    switch (a.tag) {
    case VT_NIL: return true;
    case VT_STR: {
        Str *x = a.as.s, *y = b.as.s;
        return x->len == y->len && memcmp(x->data, y->data, x->len) == 0;
    }
    case VT_LIST: {
        if (a.as.l->n != b.as.l->n) return false;
        for (long i = 0; i < a.as.l->n; i++)
            if (!v_eq(a.as.l->items[i], b.as.l->items[i])) return false;
        return true;
    }
    case VT_DICT: {
        if (a.as.d->n != b.as.d->n) return false;
        for (long i = 0; i < a.as.d->n; i++) {
            Value ov;
            if (!dict_get(b.as.d, a.as.d->items[i].key, &ov)) return false;
            if (!v_eq(a.as.d->items[i].val, ov)) return false;
        }
        return true;
    }
    case VT_FUNC: return a.as.fn == b.as.fn;
    default: return false;
    }
}

static int cmp_num(double a, double b) { return a < b ? -1 : a > b ? 1 : 0; }

int v_cmp(Value a, Value b, bool *err) {
    double da, db;
    *err = false;
    if (as_num(a, &da) && as_num(b, &db)) return cmp_num(da, db);
    if (a.tag == VT_STR && b.tag == VT_STR) {
        Str *x = a.as.s, *y = b.as.s;
        uint32_t n = x->len < y->len ? x->len : y->len;
        int c = memcmp(x->data, y->data, n);
        if (c) return c < 0 ? -1 : 1;
        return x->len < y->len ? -1 : x->len > y->len ? 1 : 0;
    }
    if (a.tag == VT_LIST && b.tag == VT_LIST) {
        long n = a.as.l->n < b.as.l->n ? a.as.l->n : b.as.l->n;
        for (long i = 0; i < n; i++) {
            if (!v_eq(a.as.l->items[i], b.as.l->items[i])) {
                int c = v_cmp(a.as.l->items[i], b.as.l->items[i], err);
                if (*err) return 0;
                if (c) return c;
            }
        }
        return a.as.l->n < b.as.l->n ? -1 : a.as.l->n > b.as.l->n ? 1 : 0;
    }
    *err = true;
    return 0;
}

const char *v_typename(Value v) {
    switch (v.tag) {
    case VT_BOOL: return STR_TYPE_BOOL;
    case VT_NIL: return KW_EOBSEUM;
    case VT_INT: return STR_TYPE_INT;
    case VT_FLOAT: return STR_TYPE_FLOAT;
    case VT_STR: return STR_TYPE_STR;
    case VT_LIST: return STR_TYPE_LIST;
    case VT_DICT: return STR_TYPE_DICT;
    case VT_FUNC: return KW_HAMSU;
    }
    return STR_TYPE_UNKNOWN;
}

/* ---------------------------------------------------------------- float fmt */
void fmt_float(double x, char *buf) {
    if (isnan(x)) { strcpy(buf, "nan"); return; }
    if (isinf(x)) { strcpy(buf, x < 0 ? "-inf" : "inf"); return; }
    for (int p = 1; p <= 17; p++) {
        snprintf(buf, 40, "%.*g", p, x);
        if (strtod(buf, NULL) == x) return;
    }
    snprintf(buf, 40, "%.17g", x);
}

Str *str_float_repr(double x) {
    char buf[40];
    fmt_float(x, buf);
    if (!strpbrk(buf, ".en") && strcmp(buf, "inf") && strcmp(buf, "-inf"))
        strcat(buf, ".0");
    return str_from(buf);
}

/* ---------------------------------------------------------------- stringify */
typedef struct { char *p; size_t n, cap; } SBuf;

static void sb_put(SBuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        while (b->n + n + 1 > b->cap) b->cap *= 2;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void sb_cz(SBuf *b, const char *s) { sb_put(b, s, strlen(s)); }

static void stringify_into(SBuf *b, Value v) {
    char tmp[48];
    switch (v.tag) {
    case VT_BOOL: sb_cz(b, v.as.b ? KW_CHAM : KW_GEOJIS); return;
    case VT_NIL: sb_cz(b, KW_EOBSEUM); return;
    case VT_INT: snprintf(tmp, sizeof tmp, "%lld", (long long)v.as.i); sb_cz(b, tmp); return;
    case VT_FLOAT:
        if (v.as.f == floor(v.as.f) && fabs(v.as.f) < 9.0e18) {
            snprintf(tmp, sizeof tmp, "%lld", (long long)v.as.f);
        } else {
            fmt_float(v.as.f, tmp);
        }
        sb_cz(b, tmp); return;
    case VT_STR: sb_put(b, v.as.s->data, v.as.s->len); return;
    case VT_FUNC: {
        const char *nm = v.as.fn->name ? v.as.fn->name : "";
        size_t need = strlen(nm) + 32;
        char *buf = malloc(need);
        snprintf(buf, need, FMT_FUNC_REPR, nm);
        sb_cz(b, buf);
        free(buf);
        return;
    }
    case VT_LIST: {
        sb_cz(b, "[");
        for (long i = 0; i < v.as.l->n; i++) {
            if (i) sb_cz(b, ", ");
            stringify_into(b, v.as.l->items[i]);
        }
        sb_cz(b, "]");
        return;
    }
    case VT_DICT: {
        sb_cz(b, "{");
        for (long i = 0; i < v.as.d->n; i++) {
            if (i) sb_cz(b, ", ");
            stringify_into(b, v.as.d->items[i].key);
            sb_cz(b, ": ");
            stringify_into(b, v.as.d->items[i].val);
        }
        sb_cz(b, "}");
        return;
    }
    }
}

Str *v_stringify(Value v) {
    SBuf b = { malloc(64), 0, 64 };
    b.p[0] = 0;
    stringify_into(&b, v);
    return str_own(b.p, (uint32_t)b.n);
}

/* ---------------------------------------------------------------- interning */
static uint64_t fnv(const char *s, uint32_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < len; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ULL; }
    return h;
}

const char *intern(Interp *it, const char *s, uint32_t len) {
    if (it->in * 10 >= it->icap * 7) {
        uint32_t ncap = it->icap ? it->icap * 2 : 1024;
        char **nt = calloc(ncap, sizeof(char *));
        for (uint32_t i = 0; i < it->icap; i++) {
            if (it->itab[i]) {
                uint32_t p = (uint32_t)fnv(it->itab[i], (uint32_t)strlen(it->itab[i])) & (ncap - 1);
                while (nt[p]) p = (p + 1) & (ncap - 1);
                nt[p] = it->itab[i];
            }
        }
        free(it->itab); it->itab = nt; it->icap = ncap;
    }
    uint32_t m = it->icap - 1, p = (uint32_t)fnv(s, len) & m;
    while (it->itab[p]) {
        if (strlen(it->itab[p]) == len && memcmp(it->itab[p], s, len) == 0)
            return it->itab[p];
        p = (p + 1) & m;
    }
    char *cp = malloc(len + 1);
    memcpy(cp, s, len); cp[len] = 0;
    it->itab[p] = cp; it->in++;
    return cp;
}

const char *intern_cz(Interp *it, const char *s) { return intern(it, s, (uint32_t)strlen(s)); }
