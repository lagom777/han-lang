/* builtins.c — builtin functions + json + html helpers */
#include "ganada.h"
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

/* ---------------------------------------------------------------- helpers */
typedef struct { char *p; size_t n, cap; } GB;

static void gb_init(GB *b) { b->cap = 128; b->n = 0; b->p = malloc(b->cap); b->p[0] = 0; }
static void gb_put(GB *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { while (b->n + n + 1 > b->cap) b->cap *= 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void gb_cz(GB *b, const char *s) { gb_put(b, s, strlen(s)); }
static void gb_str(GB *b, Str *s) { gb_put(b, s->data, s->len); }
static void gb_ch(GB *b, char c) { gb_put(b, &c, 1); }
static Str *gb_done(GB *b) { return str_own(b->p, (uint32_t)b->n); }

static Value need_arg(Interp *it, Value *args, int n, int i, const char *fn) {
    if (i >= n) g_error(it, FMT_CALL_ERR, fn, "missing argument");
    return args[i];
}

static bool is_num(Value v) { return v.tag == VT_INT || v.tag == VT_FLOAT || v.tag == VT_BOOL; }

static double num_d(Value v) {
    return v.tag == VT_INT ? (double)v.as.i : v.tag == VT_BOOL ? (double)v.as.b : v.as.f;
}

/* python int(): trunc float, parse str, bool -> 0/1 */
static int64_t to_int64(Interp *it, Value v, const char *fn) {
    if (v.tag == VT_INT) return v.as.i;
    if (v.tag == VT_BOOL) return v.as.b;
    if (v.tag == VT_FLOAT) return (int64_t)v.as.f;
    if (v.tag == VT_STR) {
        Str *s = v.as.s;
        char *buf = malloc(s->len + 1);
        memcpy(buf, s->data, s->len); buf[s->len] = 0;
        char *p = buf;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f') p++;
        char *end;
        errno = 0;
        long long r = strtoll(p, &end, 10);
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r' || *end == '\v' || *end == '\f') end++;
        if (end == p || *end) { free(buf); g_error(it, FMT_CALL_ERR, fn, "invalid literal for int()"); }
        free(buf);
        return (int64_t)r;
    }
    g_error(it, FMT_CALL_ERR, fn, "not a number");
}

static double to_float(Interp *it, Value v, const char *fn) {
    if (is_num(v)) return num_d(v);
    if (v.tag == VT_STR) {
        Str *s = v.as.s;
        char *buf = malloc(s->len + 1);
        memcpy(buf, s->data, s->len); buf[s->len] = 0;
        char *end;
        double r = strtod(buf, &end);
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
        if (end == buf || *end) { free(buf); g_error(it, FMT_CALL_ERR, fn, "could not convert string to float"); }
        free(buf);
        return r;
    }
    g_error(it, FMT_CALL_ERR, fn, "not a number");
}

static Str *need_str(Interp *it, Value v, const char *fn) {
    if (v.tag != VT_STR) g_error(it, FMT_CALL_ERR, fn, "expected string");
    return v.as.s;
}

static List *need_list(Interp *it, Value v, const char *fn) {
    if (v.tag != VT_LIST) g_error(it, FMT_CALL_ERR, fn, "expected list");
    return v.as.l;
}

static Dict *need_dict(Interp *it, Value v, const char *fn) {
    if (v.tag != VT_DICT) g_error(it, FMT_CALL_ERR, fn, "expected dict");
    return v.as.d;
}

static bool is_ws_cp(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\v' || cp == '\f'
        || cp == 0xA0 || cp == 0x2028 || cp == 0x2029 || cp == 0x3000 || cp == 0x85;
}

static bool is_alpha_cp(uint32_t cp) {
    if (cp < 0x80) return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    if (cp == 0x3000) return 0;
    if (cp >= 0x2000 && cp <= 0x206F) return 0;
    if (cp >= 0x3001 && cp <= 0x303F) return 0;
    if (cp >= 0xFF00 && cp <= 0xFF65) return 0;
    /* digits (ascii handled above; other unicode digits: reject common ones) */
    if (cp >= 0x0660 && cp <= 0x0669) return 0;
    if (cp >= 0xFF10 && cp <= 0xFF19) return 0;
    return 1;
}

static Str *str_strip(Str *s, int left, int right) {
    uint32_t a = 0, b = s->len;
    if (left) {
        while (a < b) {
            uint32_t adv; uint32_t cp = utf8_dec(s->data + a, &adv);
            if (!is_ws_cp(cp)) break;
            a += adv;
        }
    }
    if (right) {
        while (b > a) {
            /* find previous codepoint start */
            uint32_t p = b - 1;
            while (p > a && ((unsigned char)s->data[p] & 0xC0) == 0x80) p--;
            uint32_t adv; uint32_t cp = utf8_dec(s->data + p, &adv);
            if (!is_ws_cp(cp)) break;
            b = p;
        }
    }
    return str_new(s->data + a, b - a);
}

/* stable merge sort of Value array by v_cmp; may throw */
static void msort(Interp *it, Value *a, Value *tmp, long lo, long hi, const char *fn) {
    if (hi - lo <= 1) return;
    long mid = (lo + hi) / 2;
    msort(it, a, tmp, lo, mid, fn);
    msort(it, a, tmp, mid, hi, fn);
    long i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        bool err;
        int c = v_cmp(a[i], a[j], &err);
        if (err) g_error(it, FMT_CALL_ERR, fn, "unorderable types");
        tmp[k++] = c <= 0 ? a[i++] : a[j++];
    }
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi) tmp[k++] = a[j++];
    for (i = lo; i < hi; i++) a[i] = tmp[i];
}

static void sort_list(Interp *it, List *l, const char *fn) {
    if (l->n < 2) return;
    Value *tmp = malloc(sizeof(Value) * l->n);
    msort(it, l->items, tmp, 0, l->n, fn);
    free(tmp);
}

/* ---------------------------------------------------------------- core builtins */
static Value b_print(Interp *it, Value *args, int n) {
    for (int i = 0; i < n; i++) {
        if (i) fputc(' ', it->out);
        Str *s = v_stringify(args[i]);
        fwrite(s->data, 1, s->len, it->out);
    }
    fputc('\n', it->out);
    return v_nil();
}

static Value b_len(Interp *it, Value *args, int n) {
    Value v = need_arg(it, args, n, 0, BUILTIN_NAMES[B_GILI]);
    if (v.tag == VT_STR) return v_int(str_cplen(v.as.s));
    if (v.tag == VT_LIST) return v_int(v.as.l->n);
    if (v.tag == VT_DICT) return v_int(v.as.d->n);
    g_error(it, FMT_CALL_ERR, BUILTIN_NAMES[B_GILI], "object has no len()");
}

static Value b_number(Interp *it, Value *args, int n) {
    Value v = need_arg(it, args, n, 0, BUILTIN_NAMES[B_SUSJA]);
    const char *fn = BUILTIN_NAMES[B_SUSJA];
    if (v.tag == VT_STR) {
        /* '.' in s -> float, else int */
        if (memchr(v.as.s->data, '.', v.as.s->len)) return v_float(to_float(it, v, fn));
        return v_int(to_int64(it, v, fn));
    }
    if (v.tag == VT_FLOAT) return v_int((int64_t)v.as.f);
    if (v.tag == VT_BOOL) return v_int(v.as.b);
    if (v.tag == VT_INT) return v;
    g_error(it, FMT_CALL_ERR, fn, "not a number");
}

static Value b_append(Interp *it, Value *args, int n) {
    List *l = need_list(it, need_arg(it, args, n, 0, BUILTIN_NAMES[B_CHUGA]), BUILTIN_NAMES[B_CHUGA]);
    list_push(l, need_arg(it, args, n, 1, BUILTIN_NAMES[B_CHUGA]));
    return v_nil();
}

static Value b_sum(Interp *it, Value *args, int n) {
    List *l = need_list(it, need_arg(it, args, n, 0, BUILTIN_NAMES[B_HAB]), BUILTIN_NAMES[B_HAB]);
    int64_t isum = 0; double fsum = 0; int anyfloat = 0;
    for (long i = 0; i < l->n; i++) {
        Value v = l->items[i];
        if (!is_num(v)) g_error(it, FMT_CALL_ERR, BUILTIN_NAMES[B_HAB], "unsupported operand");
        if (v.tag == VT_FLOAT) anyfloat = 1;
        if (anyfloat) fsum += num_d(v);
        else isum += v.tag == VT_INT ? v.as.i : v.as.b;
    }
    if (anyfloat) return v_float(fsum + (double)isum);
    return v_int(isum);
}

static Value b_mean(Interp *it, Value *args, int n) {
    List *l = need_list(it, need_arg(it, args, n, 0, BUILTIN_NAMES[B_PYEONGGYUN]), BUILTIN_NAMES[B_PYEONGGYUN]);
    if (!l->n) G_ERR0(it, ERR_AVG_EMPTY);
    double s = 0;
    for (long i = 0; i < l->n; i++) {
        if (!is_num(l->items[i])) g_error(it, FMT_CALL_ERR, BUILTIN_NAMES[B_PYEONGGYUN], "not a number");
        s += num_d(l->items[i]);
    }
    return v_float(s / (double)l->n);
}

static Value b_median(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JUNGANGGABS];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    if (!l->n) G_ERR0(it, ERR_MEDIAN_EMPTY);
    List *c = list_new(l->n);
    for (long i = 0; i < l->n; i++) list_push(c, l->items[i]);
    sort_list(it, c, fn);
    long m = c->n / 2;
    if (c->n % 2) return c->items[m];
    Value a = c->items[m - 1], b = c->items[m];
    if (a.tag == VT_INT && b.tag == VT_INT) return v_float(((double)a.as.i + (double)b.as.i) / 2.0);
    return v_float((num_d(a) + num_d(b)) / 2.0);
}

static Value b_stdev(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_PYOJUNPYEONCHA];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    if (!l->n) return v_int(0);
    double m = 0;
    for (long i = 0; i < l->n; i++) m += num_d(l->items[i]);
    m /= (double)l->n;
    double var = 0;
    for (long i = 0; i < l->n; i++) { double d = num_d(l->items[i]) - m; var += d * d; }
    var /= (double)l->n;
    return v_float(sqrt(var));
}

static Value b_mode(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_CHOEBINGABS];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    if (!l->n) G_ERR0(it, ERR_MODE_EMPTY);
    Dict *counts = dict_new();
    for (long i = 0; i < l->n; i++) {
        Value c;
        int64_t cur = dict_get(counts, l->items[i], &c) ? c.as.i : 0;
        dict_set(counts, l->items[i], v_int(cur + 1));
    }
    Value best = counts->items[0].key;
    int64_t bc = counts->items[0].val.as.i;
    for (long i = 1; i < counts->n; i++)
        if (counts->items[i].val.as.i > bc) { bc = counts->items[i].val.as.i; best = counts->items[i].key; }
    return best;
}

static Value b_normalize(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JEONGGYUHWA];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    List *out = list_new(l->n);
    if (!l->n) return v_list(out);
    double lo = num_d(l->items[0]), hi = lo;
    for (long i = 1; i < l->n; i++) {
        double d = num_d(l->items[i]);
        if (d < lo) lo = d;
        if (d > hi) hi = d;
    }
    if (hi == lo) { for (long i = 0; i < l->n; i++) list_push(out, v_int(0)); return v_list(out); }
    for (long i = 0; i < l->n; i++) list_push(out, v_float((num_d(l->items[i]) - lo) / (hi - lo)));
    return v_list(out);
}

static Value b_sort(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JEONGRYEOL];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    List *c = list_new(l->n);
    for (long i = 0; i < l->n; i++) list_push(c, l->items[i]);
    sort_list(it, c, fn);
    if (n > 1 && v_truthy(args[1])) {
        for (long i = 0, j = c->n - 1; i < j; i++, j--) { Value t = c->items[i]; c->items[i] = c->items[j]; c->items[j] = t; }
    }
    return v_list(c);
}

static Value b_topn(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_SANGWI];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    int64_t cnt = n > 1 ? to_int64(it, args[1], fn) : 1;
    List *c = list_new(l->n);
    for (long i = 0; i < l->n; i++) list_push(c, l->items[i]);
    sort_list(it, c, fn);
    for (long i = 0, j = c->n - 1; i < j; i++, j--) { Value t = c->items[i]; c->items[i] = c->items[j]; c->items[j] = t; }
    List *out = list_new(cnt);
    for (long i = 0; i < cnt && i < c->n; i++) list_push(out, c->items[i]);
    return v_list(out);
}

static Value b_maxmin(Interp *it, Value *args, int n, int ismax) {
    const char *fn = ismax ? BUILTIN_NAMES[B_CHOEDAE] : BUILTIN_NAMES[B_CHOESO];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    if (!l->n) g_error(it, FMT_CALL_ERR, fn, "empty list");
    Value best = l->items[0];
    for (long i = 1; i < l->n; i++) {
        bool err;
        int c = v_cmp(l->items[i], best, &err);
        if (err) g_error(it, FMT_CALL_ERR, fn, "unorderable types");
        if (ismax ? c > 0 : c < 0) best = l->items[i];
    }
    return best;
}

static Value b_range(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_BEOMWI];
    int64_t a = 0, b, step = 1;
    if (n == 1) { b = to_int64(it, args[0], fn); }
    else if (n == 2) { a = to_int64(it, args[0], fn); b = to_int64(it, args[1], fn); }
    else if (n >= 3) { a = to_int64(it, args[0], fn); b = to_int64(it, args[1], fn); step = to_int64(it, args[2], fn); }
    else g_error(it, FMT_CALL_ERR, fn, "missing argument");
    if (step == 0) g_error(it, FMT_CALL_ERR, fn, "step cannot be zero");
    List *out = list_new(8);
    if (step > 0) for (int64_t i = a; i < b; i += step) list_push(out, v_int(i));
    else for (int64_t i = a; i > b; i += step) list_push(out, v_int(i));
    return v_list(out);
}

static Value b_linspace(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_GANGYEOG];
    Value a = need_arg(it, args, n, 0, fn), b = need_arg(it, args, n, 1, fn);
    int64_t cnt = n > 2 ? to_int64(it, args[2], fn) : 0;
    List *out = list_new(cnt > 0 ? cnt : 0);
    if (cnt <= 1) {
        if (cnt == 1) list_push(out, a);
        return v_list(out);
    }
    double step = (num_d(b) - num_d(a)) / (double)(cnt - 1);
    for (int64_t i = 0; i < cnt; i++) list_push(out, v_float(num_d(a) + step * (double)i));
    return v_list(out);
}

/* ---------------------------------------------------------------- strings */
static Value b_split(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_NANUGI];
    Str *s = need_str(it, need_arg(it, args, n, 0, fn), fn);
    List *out = list_new(8);
    if (n > 1) {
        Str *sep = need_str(it, args[1], fn);
        if (!sep->len) g_error(it, FMT_CALL_ERR, fn, "empty separator");
        uint32_t start = 0;
        for (uint32_t i = 0; i + sep->len <= s->len;) {
            if (!memcmp(s->data + i, sep->data, sep->len)) {
                list_push(out, v_str(str_new(s->data + start, i - start)));
                i += sep->len; start = i;
            } else i++;
        }
        list_push(out, v_str(str_new(s->data + start, s->len - start)));
    } else {
        /* whitespace split: runs, skip empties */
        uint32_t i = 0;
        while (i < s->len) {
            uint32_t adv; uint32_t cp = utf8_dec(s->data + i, &adv);
            if (is_ws_cp(cp)) { i += adv; continue; }
            uint32_t start = i;
            while (i < s->len) {
                cp = utf8_dec(s->data + i, &adv);
                if (is_ws_cp(cp)) break;
                i += adv;
            }
            list_push(out, v_str(str_new(s->data + start, i - start)));
        }
    }
    return v_list(out);
}

static Value b_splitlines(Interp *it, Value *args, int n) {
    Str *s = n > 0 ? v_stringify(args[0]) : str_from("");
    List *out = list_new(8);
    uint32_t start = 0, i = 0;
    while (i < s->len) {
        char c = s->data[i];
        if (c == '\n' || c == '\r' || c == '\v' || c == '\f' ||
            (unsigned char)c == 0x1C || (unsigned char)c == 0x1D || (unsigned char)c == 0x1E) {
            list_push(out, v_str(str_new(s->data + start, i - start)));
            if (c == '\r' && i + 1 < s->len && s->data[i + 1] == '\n') i++;
            i++; start = i;
        } else i++;
    }
    if (start < s->len) list_push(out, v_str(str_new(s->data + start, s->len - start)));
    return v_list(out);
}

static Value b_find(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_WICHI];
    Str *s = v_stringify(need_arg(it, args, n, 0, fn));
    Str *sub = v_stringify(need_arg(it, args, n, 1, fn));
    if (!sub->len) return v_int(0);
    for (uint32_t i = 0; i + sub->len <= s->len; i++) {
        if (!memcmp(s->data + i, sub->data, sub->len)) {
            /* codepoint index of byte i */
            long cp = 0;
            for (uint32_t j = 0; j < i;) {
                unsigned char c = (unsigned char)s->data[j];
                j += c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
                cp++;
            }
            return v_int(cp);
        }
    }
    return v_int(-1);
}

static Value b_join(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_HABCHIGI];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Str *sep = n > 1 ? v_stringify(args[1]) : str_from("");
    GB b; gb_init(&b);
    for (long i = 0; i < l->n; i++) {
        if (i) gb_str(&b, sep);
        gb_str(&b, v_stringify(l->items[i]));
    }
    return v_str(gb_done(&b));
}

static Value b_reverse(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_GEOKKURO];
    Value v = need_arg(it, args, n, 0, fn);
    if (v.tag == VT_STR) {
        Str *s = v.as.s;
        Str *r = malloc(sizeof(Str) + s->len + 1);
        r->len = s->len; r->cplen = UINT32_MAX; r->flags = s->flags;
        uint32_t o = 0;
        uint32_t i = s->len;
        while (i > 0) {
            uint32_t p = i - 1;
            while (p > 0 && ((unsigned char)s->data[p] & 0xC0) == 0x80) p--;
            uint32_t clen = i - p;
            memcpy(r->data + o, s->data + p, clen);
            o += clen; i = p;
        }
        r->data[r->len] = 0;
        return v_str(r);
    }
    List *l = need_list(it, v, fn);
    List *out = list_new(l->n);
    for (long i = l->n - 1; i >= 0; i--) list_push(out, l->items[i]);
    return v_list(out);
}

static Value b_case(Interp *it, Value *args, int n, int upper) {
    const char *fn = upper ? BUILTIN_NAMES[B_DAEMUNJA] : BUILTIN_NAMES[B_SOMUNJA];
    Str *s = need_str(it, need_arg(it, args, n, 0, fn), fn);
    Str *r = str_new(s->data, s->len);
    for (uint32_t i = 0; i < r->len; i++) {
        char c = r->data[i];
        if (upper && c >= 'a' && c <= 'z') r->data[i] = c - 32;
        if (!upper && c >= 'A' && c <= 'Z') r->data[i] = c + 32;
    }
    return v_str(r);
}

static Value b_strip(Interp *it, Value *args, int n, int mode) {
    const char *fn = BUILTIN_NAMES[B_DADEUMGI];
    Str *s = need_str(it, need_arg(it, args, n, 0, fn), fn);
    return v_str(str_strip(s, mode & 1, mode & 2));
}

static Value b_truncate(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_MALJULIM];
    Str *s = v_stringify(need_arg(it, args, n, 0, fn));
    int64_t cnt = n > 1 ? to_int64(it, args[1], fn) : 0;
    if ((int64_t)str_cplen(s) <= cnt) return v_str(s);
    Str *cut = str_sub_cp(s, 0, cnt > 0 ? (long)cnt : 0);
    return v_str(str_concat(cut, str_from(STR_ELLIPSIS)));
}

static Value b_replace(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_BAKKUGI];
    Str *s = need_str(it, need_arg(it, args, n, 0, fn), fn);
    Str *old = need_str(it, need_arg(it, args, n, 1, fn), fn);
    Str *nw = need_str(it, need_arg(it, args, n, 2, fn), fn);
    if (!old->len) return v_str(s);
    GB b; gb_init(&b);
    uint32_t start = 0;
    for (uint32_t i = 0; i + old->len <= s->len;) {
        if (!memcmp(s->data + i, old->data, old->len)) {
            gb_put(&b, s->data + start, i - start);
            gb_str(&b, nw);
            i += old->len; start = i;
        } else i++;
    }
    gb_put(&b, s->data + start, s->len - start);
    return v_str(gb_done(&b));
}

static Value b_contains(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_POHAM];
    Value c = need_arg(it, args, n, 0, fn);
    Value v = need_arg(it, args, n, 1, fn);
    if (c.tag == VT_STR) {
        Str *sub = v_stringify(v);
        if (!sub->len) return v_bool(1);
        for (uint32_t i = 0; i + sub->len <= c.as.s->len; i++)
            if (!memcmp(c.as.s->data + i, sub->data, sub->len)) return v_bool(1);
        return v_bool(0);
    }
    if (c.tag == VT_LIST) {
        for (long i = 0; i < c.as.l->n; i++)
            if (v_eq(c.as.l->items[i], v)) return v_bool(1);
        return v_bool(0);
    }
    if (c.tag == VT_DICT) {
        Value out;
        return v_bool(dict_get(c.as.d, v, &out));
    }
    g_error(it, FMT_CALL_ERR, fn, "argument not iterable");
}

static Value b_startswith(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_SIJAG];
    Str *s = need_str(it, need_arg(it, args, n, 0, fn), fn);
    Str *pre = need_str(it, need_arg(it, args, n, 1, fn), fn);
    return v_bool(s->len >= pre->len && !memcmp(s->data, pre->data, pre->len));
}

static Value b_endswith(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_KKEUT];
    Str *s = need_str(it, need_arg(it, args, n, 0, fn), fn);
    Str *suf = need_str(it, need_arg(it, args, n, 1, fn), fn);
    return v_bool(s->len >= suf->len && !memcmp(s->data + s->len - suf->len, suf->data, suf->len));
}

static Value b_isnumber(Interp *it, Value *args, int n) {
    Value v = n > 0 ? args[0] : v_nil();
    if (v.tag == VT_BOOL) return v_bool(0);
    if (v.tag == VT_INT || v.tag == VT_FLOAT) return v_bool(1);
    if (v.tag != VT_STR) return v_bool(0);
    Str *st = str_strip(v.as.s, 1, 1);
    if (!st->len) return v_bool(0);
    char *buf = malloc(st->len + 1);
    memcpy(buf, st->data, st->len); buf[st->len] = 0;
    char *end;
    strtod(buf, &end);
    int ok = end != buf && !*end;
    free(buf);
    return v_bool(ok);
}

static Value b_isalpha(Interp *it, Value *args, int n) {
    Value v = n > 0 ? args[0] : v_nil();
    if (v.tag != VT_STR || !v.as.s->len) return v_bool(0);
    uint32_t i = 0;
    while (i < v.as.s->len) {
        uint32_t adv; uint32_t cp = utf8_dec(v.as.s->data + i, &adv);
        if (!is_alpha_cp(cp)) return v_bool(0);
        i += adv;
    }
    return v_bool(1);
}

static Value b_isspace(Interp *it, Value *args, int n) {
    Value v = n > 0 ? args[0] : v_nil();
    if (v.tag != VT_STR || !v.as.s->len) return v_bool(0);
    uint32_t i = 0;
    while (i < v.as.s->len) {
        uint32_t adv; uint32_t cp = utf8_dec(v.as.s->data + i, &adv);
        if (!is_ws_cp(cp)) return v_bool(0);
        i += adv;
    }
    return v_bool(1);
}

static Value b_type(Interp *it, Value *args, int n) {
    (void)it;
    return v_str(str_from(v_typename(need_arg(it, args, n, 0, BUILTIN_NAMES[B_TAIB]))));
}

/* ---------------------------------------------------------------- math */
static Value b_abs(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JEOLDAESGABS];
    Value v = need_arg(it, args, n, 0, fn);
    if (v.tag == VT_INT) return v_int(v.as.i < 0 ? -v.as.i : v.as.i);
    if (v.tag == VT_FLOAT) return v_float(fabs(v.as.f));
    if (v.tag == VT_BOOL) return v_int(v.as.b);
    g_error(it, FMT_CALL_ERR, fn, "bad operand for abs()");
}

/* round decimal string (shortest repr) half-up at `digits` fractional places.
 * writes result decimal string into out (size >= 128) */
static void round_half_up(const char *in, int64_t digits, char *out) {
    int neg = 0;
    const char *p = in;
    if (*p == '-') { neg = 1; p++; }
    /* collect digits */
    char dig[128]; int nd = 0;
    int fracdigits = 0, intdigits = 0, exp10 = 0, seendot = 0, seenany = 0;
    for (; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            if (nd < 120) dig[nd++] = *p;
            if (seendot) fracdigits++; else intdigits++;
            if (*p != '0') seenany = 1;
        } else if (*p == '.') seendot = 1;
        else if (*p == 'e' || *p == 'E') { exp10 = atoi(p + 1); break; }
        else break;
    }
    (void)intdigits; (void)seenany;
    /* value = D x 10^(exp10 - fracdigits) = 0.D x 10^E */
    int lead = 0;
    while (lead < nd && dig[lead] == '0') lead++;
    if (lead == nd) { strcpy(out, "0"); return; }   /* zero */
    int m = nd - lead;
    int64_t E = (int64_t)exp10 - fracdigits + m;
    /* keep c = E + digits digits */
    int64_t c = E + digits;
    char kept[128]; int nk = 0;
    if (c <= 0) {
        if (c == 0 && dig[lead] >= '5') { kept[nk++] = '1'; }
        /* else zero */
    } else if (c >= m) {
        /* keep all significant digits + pad zeros (nk == c, else 10^-digits scale breaks) */
        if (c > 120) c = 120;
        memcpy(kept, dig + lead, m);
        memset(kept + m, '0', (size_t)(c - m));
        nk = (int)c;
    } else {
        memcpy(kept, dig + lead, c); nk = (int)c;
        if (dig[lead + c] >= '5') {
            int i = nk - 1;
            for (; i >= 0; i--) {
                if (kept[i] != '9') { kept[i]++; break; }
                kept[i] = '0';
            }
            if (i < 0) { memmove(kept + 1, kept, nk); kept[0] = '1'; nk++; E++; }
        }
    }
    if (nk == 0) { strcpy(out, "0"); return; }
    /* result = keptInt x 10^-digits */
    char *o = out;
    if (neg) *o++ = '-';
    if (digits <= 0) {
        memcpy(o, kept, nk); o += nk;
        for (int64_t i = 0; i < -digits; i++) *o++ = '0';
    } else {
        int64_t intlen = (int64_t)nk - digits;
        if (intlen > 0) {
            memcpy(o, kept, intlen); o += intlen;
            *o++ = '.';
            memcpy(o, kept + intlen, digits); o += digits;
        } else {
            *o++ = '0'; *o++ = '.';
            for (int64_t i = 0; i < -intlen; i++) *o++ = '0';
            memcpy(o, kept, nk); o += nk;
        }
    }
    *o = 0;
}

static Value b_round(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_BANOLRIM];
    Value x = need_arg(it, args, n, 0, fn);
    int64_t digits = n > 1 ? to_int64(it, args[1], fn) : 0;
    char buf[64];
    if (x.tag == VT_INT) snprintf(buf, sizeof buf, "%lld", (long long)x.as.i);
    else if (x.tag == VT_BOOL) snprintf(buf, sizeof buf, "%d", x.as.b);
    else if (x.tag == VT_FLOAT) {
        if (isinf(x.as.f) || isnan(x.as.f)) return n > 1 ? x : v_int((int64_t)x.as.f);
        fmt_float(x.as.f, buf);
    } else g_error(it, FMT_CALL_ERR, fn, "not a number");
    char out[160];
    round_half_up(buf, digits, out);
    if (n > 1) return v_float(strtod(out, NULL));
    return v_int(strtoll(out, NULL, 10));
}

static Value b_commas(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_CHEONDANWI];
    Value v = need_arg(it, args, n, 0, fn);
    char buf[64];
    if (v.tag == VT_INT || v.tag == VT_BOOL) {
        snprintf(buf, sizeof buf, "%lld", (long long)(v.tag == VT_INT ? v.as.i : v.as.b));
    } else if (v.tag == VT_FLOAT) fmt_float(v.as.f, buf);
    else g_error(it, FMT_CALL_ERR, fn, "not a number");
    /* insert commas into integer part */
    char *dot = strpbrk(buf, ".eE");
    int ilen = dot ? (int)(dot - buf) : (int)strlen(buf);
    int neg = buf[0] == '-';
    int dnum = ilen - neg;
    char out[80];
    char *o = out;
    if (neg) *o++ = '-';
    for (int i = 0; i < dnum; i++) {
        if (i && (dnum - i) % 3 == 0) *o++ = ',';
        *o++ = buf[neg + i];
    }
    if (dot) { strcpy(o, dot); o += strlen(dot); }
    *o = 0;
    return v_str(str_from(out));
}

static Value b_clamp(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_SAIGABS];
    Value v = need_arg(it, args, n, 0, fn), lo = need_arg(it, args, n, 1, fn), hi = need_arg(it, args, n, 2, fn);
    bool err;
    if (v_cmp(v, lo, &err) < 0) { if (err) goto bad; return lo; }
    if (v_cmp(v, hi, &err) > 0) { if (err) goto bad; return hi; }
    return v;
bad:
    g_error(it, FMT_CALL_ERR, fn, "unorderable types");
}

static int64_t gcd64(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { int64_t t = a % b; a = b; b = t; }
    return a;
}

static Value b_gcd(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_CHOEDAEGONGYAGSU];
    int64_t a = to_int64(it, need_arg(it, args, n, 0, fn), fn);
    int64_t b = to_int64(it, need_arg(it, args, n, 1, fn), fn);
    return v_int(gcd64(a, b));
}

static Value b_lcm(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_CHOESOGONGBAESU];
    int64_t a = to_int64(it, need_arg(it, args, n, 0, fn), fn);
    int64_t b = to_int64(it, need_arg(it, args, n, 1, fn), fn);
    if (!a || !b) return v_int(0);
    int64_t g = gcd64(a, b);
    int64_t r = a * b / g;
    return v_int(r < 0 ? -r : r);
}

static Value b_ceilfloor(Interp *it, Value *args, int n, int isceil) {
    const char *fn = isceil ? BUILTIN_NAMES[B_OLRIM] : BUILTIN_NAMES[B_NAERIM];
    Value v = need_arg(it, args, n, 0, fn);
    if (v.tag == VT_INT) return v;
    if (!is_num(v)) g_error(it, FMT_CALL_ERR, fn, "not a number");
    double d = num_d(v);
    return v_int((int64_t)(isceil ? ceil(d) : floor(d)));
}

static Value b_sqrt(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JEGOBGEUN];
    Value v = need_arg(it, args, n, 0, fn);
    if (!is_num(v)) g_error(it, FMT_CALL_ERR, fn, "not a number");
    double d = num_d(v);
    if (d < 0) g_error(it, FMT_CALL_ERR, fn, "math domain error");
    return v_float(sqrt(d));
}

static Value b_pow(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_GEODEUBJEGOB];
    Value a = need_arg(it, args, n, 0, fn), b = need_arg(it, args, n, 1, fn);
    if (!is_num(a) || !is_num(b)) g_error(it, FMT_CALL_ERR, fn, "not a number");
    if (a.tag == VT_INT && b.tag == VT_INT && b.as.i >= 0) {
        int64_t base = a.as.i, e = b.as.i, r = 1;
        while (e > 0) {
            if (e & 1) r *= base;
            base *= base; e >>= 1;
        }
        return v_int(r);
    }
    return v_float(pow(num_d(a), num_d(b)));
}

/* ---------------------------------------------------------------- io/system */
static Value b_input(Interp *it, Value *args, int n) {
    if (n > 0) {
        Str *s = v_stringify(args[0]);
        fwrite(s->data, 1, s->len, it->out);
        fflush(it->out);
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t r = getline(&line, &cap, stdin);
    if (r < 0) { free(line); return v_nil(); }
    while (r > 0 && line[r - 1] == '\n') line[--r] = 0;
    return v_str(str_own(line, (uint32_t)r));
}

static Value b_readfile(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_PAILILGGI];
    Str *p = v_stringify(need_arg(it, args, n, 0, fn));
    char *src = read_file_utf8(p->data);
    if (!src) g_error(it, ERR_FILE_READ, p->data);
    return v_str(str_own(src, (uint32_t)strlen(src)));
}

static Value write_common(Interp *it, Value *args, int n, const char *mode, const char *fn) {
    Str *p = v_stringify(need_arg(it, args, n, 0, fn));
    Str *s = n > 1 ? v_stringify(args[1]) : str_from("");
    FILE *f = fopen(p->data, mode);
    if (!f) g_error(it, ERR_FILE_WRITE, p->data);
    fwrite(s->data, 1, s->len, f);
    fclose(f);
    return v_int(str_cplen(s));
}

static Value b_writefile(Interp *it, Value *args, int n) {
    return write_common(it, args, n, "wb", BUILTIN_NAMES[B_PAILSSEUGI]);
}

static Value b_appendfile(Interp *it, Value *args, int n) {
    return write_common(it, args, n, "ab", BUILTIN_NAMES[B_IEOSSEUGI]);
}

static Value b_fileexists(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_PAILJONJAE];
    Str *p = v_stringify(need_arg(it, args, n, 0, fn));
    struct stat st;
    return v_bool(stat(p->data, &st) == 0);
}

static int cstr_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static Value b_listdir(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_PAILMOGROG];
    Str *p = v_stringify(need_arg(it, args, n, 0, fn));
    DIR *d = opendir(p->data);
    if (!d) g_error(it, FMT_CALL_ERR, fn, strerror(errno));
    char **names = NULL; int cnt = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (cnt == cap) { cap = cap ? cap * 2 : 16; names = realloc(names, sizeof(char *) * cap); }
        names[cnt++] = strdup(de->d_name);
    }
    closedir(d);
    qsort(names, cnt, sizeof(char *), cstr_cmp);
    List *out = list_new(cnt);
    for (int i = 0; i < cnt; i++) { list_push(out, v_str(str_from(names[i]))); free(names[i]); }
    free(names);
    return v_list(out);
}

static Value b_getcwd(Interp *it, Value *args, int n) {
    (void)args; (void)n; (void)it;
    char buf[4096];
    if (!getcwd(buf, sizeof buf)) return v_str(str_from(""));
    return v_str(str_from(buf));
}

static Value b_chdir(Interp *it, Value *args, int n) {
    Str *p = n > 0 ? v_stringify(args[0]) : str_from("");
    struct stat st;
    if (stat(p->data, &st) || !S_ISDIR(st.st_mode)) g_error(it, ERR_CD_NOFOLDER, p->data);
    if (chdir(p->data)) g_error(it, ERR_CD_NOFOLDER, p->data);
    return b_getcwd(it, args, 0);
}

static Value b_makedirs(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_POLDEOSAENGSEONG];
    Str *p = v_stringify(need_arg(it, args, n, 0, fn));
    char *tmp = strdup(p->data);
    for (char *q = tmp + 1; ; q++) {
        if (*q == '/' || !*q) {
            char save = *q;
            *q = 0;
            mkdir(tmp, 0777);
            *q = save;
            if (!save) break;
        }
    }
    free(tmp);
    return v_bool(1);
}

static Value b_delete(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_PAILSAGJE];
    Str *p = v_stringify(need_arg(it, args, n, 0, fn));
    struct stat st;
    if (stat(p->data, &st) == 0 && S_ISDIR(st.st_mode)) {
        DIR *d = opendir(p->data);
        int empty = 1;
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)))
                if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) { empty = 0; break; }
            closedir(d);
        }
        if (!empty) g_error(it, ERR_DEL_NOTEMPTY, p->data);
        rmdir(p->data);
        return v_bool(1);
    }
    if (stat(p->data, &st)) g_error(it, ERR_DEL_MISSING, p->data);
    unlink(p->data);
    return v_bool(1);
}

static Value b_copy(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_PAILBOGSA];
    Str *src = v_stringify(need_arg(it, args, n, 0, fn));
    Str *dst = v_stringify(need_arg(it, args, n, 1, fn));
    struct stat st;
    if (stat(src->data, &st) || !S_ISREG(st.st_mode)) g_error(it, ERR_COPY_MISSING, src->data);
    char *final = strdup(dst->data);
    struct stat ds;
    if (stat(dst->data, &ds) == 0 && S_ISDIR(ds.st_mode)) {
        const char *base = strrchr(src->data, '/');
        base = base ? base + 1 : src->data;
        free(final);
        final = malloc(strlen(dst->data) + strlen(base) + 2);
        sprintf(final, "%s/%s", dst->data, base);
    }
    FILE *fi = fopen(src->data, "rb");
    FILE *fo = final ? fopen(final, "wb") : NULL;
    if (!fi || !fo) {
        if (fi) fclose(fi);
        g_error(it, ERR_FILE_WRITE, final);
    }
    char buf[8192]; size_t r;
    while ((r = fread(buf, 1, sizeof buf, fi)) > 0) fwrite(buf, 1, r, fo);
    fclose(fi); fclose(fo);
    return v_str(str_own(final, (uint32_t)strlen(final)));
}

static Value b_rename(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_IREUMBAKKUGI];
    Str *old = v_stringify(need_arg(it, args, n, 0, fn));
    Str *nw = v_stringify(need_arg(it, args, n, 1, fn));
    struct stat st;
    if (stat(old->data, &st)) g_error(it, ERR_REN_MISSING, old->data);
    rename(old->data, nw->data);
    return v_bool(1);
}

static Value b_fileinfo(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_PAILJEONGBO];
    Str *p = v_stringify(need_arg(it, args, n, 0, fn));
    struct stat st;
    if (stat(p->data, &st)) g_error(it, ERR_INFO_MISSING, p->data);
    Dict *d = dict_new();
    dict_set(d, v_str(str_from(STR_JONGNYU)),
             v_str(str_from(S_ISDIR(st.st_mode) ? STR_POLDEEO : STR_PAIL)));
    dict_set(d, v_str(str_from(STR_KEUGI)), v_int((int64_t)st.st_size));
    dict_set(d, v_str(str_from(STR_SUJEONGSIGAG)), v_float((double)st.st_mtime));
    return v_dict(d);
}

static Value b_system(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_OEBUSILHAENG];
    Str *cmd = v_stringify(need_arg(it, args, n, 0, fn));
    fflush(it->out);
    int rc = system(cmd->data);
    if (rc == -1) return v_int(-1);
    if (WIFEXITED(rc)) return v_int(WEXITSTATUS(rc));
    return v_int(rc);
}

static Value b_datetime(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_NALJJASIGAN];
    time_t t;
    if (n > 0 && args[0].tag != VT_NIL) {
        double d = to_float(it, args[0], fn);
        t = (time_t)d;
    } else t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
    return v_str(str_from(buf));
}

static const struct { const char *name; int code; } COLOR_TABLE[] = {
    { STR_COLOR_RED, 31 }, { STR_COLOR_GREEN, 32 }, { STR_COLOR_YELLOW, 33 },
    { STR_COLOR_BLUE, 34 }, { STR_COLOR_MAGENTA, 35 }, { STR_COLOR_CYAN, 36 },
    { STR_COLOR_GRAY, 90 }, { STR_COLOR_BOLD, 1 },
};

static Value b_color(Interp *it, Value *args, int n) {
    Str *text = n > 0 ? v_stringify(args[0]) : str_from("");
    Str *cname = n > 1 ? v_stringify(args[1]) : str_from("");
    int code = -1;
    for (size_t i = 0; i < sizeof(COLOR_TABLE) / sizeof(COLOR_TABLE[0]); i++)
        if (!strcmp(cname->data, COLOR_TABLE[i].name)) { code = COLOR_TABLE[i].code; break; }
    if (code < 0) {
        GB b; gb_init(&b);
        for (size_t i = 0; i < sizeof(COLOR_TABLE) / sizeof(COLOR_TABLE[0]); i++) {
            if (i) gb_cz(&b, ", ");
            gb_cz(&b, COLOR_TABLE[i].name);
        }
        g_error(it, ERR_BAD_COLOR, cname->data, b.p);
    }
    char pre[16];
    snprintf(pre, sizeof pre, "\x1b[%dm", code);
    GB b; gb_init(&b);
    gb_cz(&b, pre); gb_str(&b, text); gb_cz(&b, "\x1b[0m");
    return v_str(gb_done(&b));
}

static Value b_getenv(Interp *it, Value *args, int n) {
    Str *name = n > 0 ? v_stringify(args[0]) : str_from("");
    const char *v = getenv(name->data);
    if (v) return v_str(str_from(v));
    if (n > 1) return args[1];
    return v_nil();
}

static Value b_now(Interp *it, Value *args, int n) {
    (void)args; (void)n; (void)it;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return v_float((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

/* ---------------------------------------------------------------- dict helpers */
static Value b_keys(Interp *it, Value *args, int n) {
    Dict *d = need_dict(it, need_arg(it, args, n, 0, BUILTIN_NAMES[B_KIDEUL]), BUILTIN_NAMES[B_KIDEUL]);
    List *out = list_new(d->n);
    for (long i = 0; i < d->n; i++) list_push(out, d->items[i].key);
    return v_list(out);
}

static Value b_values(Interp *it, Value *args, int n) {
    Dict *d = need_dict(it, need_arg(it, args, n, 0, BUILTIN_NAMES[B_GABSDEUL]), BUILTIN_NAMES[B_GABSDEUL]);
    List *out = list_new(d->n);
    for (long i = 0; i < d->n; i++) list_push(out, d->items[i].val);
    return v_list(out);
}

static Value b_items(Interp *it, Value *args, int n) {
    Dict *d = need_dict(it, need_arg(it, args, n, 0, BUILTIN_NAMES[B_HANGMOGDEUL]), BUILTIN_NAMES[B_HANGMOGDEUL]);
    List *out = list_new(d->n);
    for (long i = 0; i < d->n; i++) {
        List *pair = list_new(2);
        list_push(pair, d->items[i].key);
        list_push(pair, d->items[i].val);
        list_push(out, v_list(pair));
    }
    return v_list(out);
}

static Value b_dictfrom(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_SAJEONMANDEULGI];
    List *ks = need_list(it, need_arg(it, args, n, 0, fn), fn);
    List *vs = need_list(it, need_arg(it, args, n, 1, fn), fn);
    Dict *d = dict_new();
    long m = ks->n < vs->n ? ks->n : vs->n;
    for (long i = 0; i < m; i++) dict_set(d, ks->items[i], vs->items[i]);
    return v_dict(d);
}

static Value b_dictget(Interp *it, Value *args, int n) {
    Value d = need_arg(it, args, n, 0, BUILTIN_NAMES[B_GABSEODGI]);
    Value k = need_arg(it, args, n, 1, BUILTIN_NAMES[B_GABSEODGI]);
    if (d.tag == VT_DICT) {
        Value out;
        if (dict_get(d.as.d, k, &out)) return out;
    }
    return n > 2 ? args[2] : v_nil();
}

static Value b_haskey(Interp *it, Value *args, int n) {
    Value d = need_arg(it, args, n, 0, BUILTIN_NAMES[B_KIISSNA]);
    Value k = need_arg(it, args, n, 1, BUILTIN_NAMES[B_KIISSNA]);
    if (d.tag != VT_DICT) return v_bool(0);
    Value out;
    return v_bool(dict_get(d.as.d, k, &out));
}

static Value b_merge(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_BYEONGHAB];
    Dict *out = dict_new();
    for (int i = 0; i < n; i++) {
        Dict *d = need_dict(it, args[i], fn);
        for (long j = 0; j < d->n; j++) dict_set(out, d->items[j].key, d->items[j].val);
    }
    return v_dict(out);
}

/* ---------------------------------------------------------------- higher order */
static Value b_map(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_BYEONHWAN];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value f = need_arg(it, args, n, 1, fn);
    List *out = list_new(l->n);
    for (long i = 0; i < l->n; i++) list_push(out, apply_func(it, f, &l->items[i], 1));
    return v_list(out);
}

static Value b_generate(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_SAENGSEONG];
    int64_t cnt = to_int64(it, need_arg(it, args, n, 0, fn), fn);
    Value f = need_arg(it, args, n, 1, fn);
    List *out = list_new(cnt > 0 ? cnt : 0);
    for (int64_t i = 0; i < cnt; i++) {
        Value iv = v_int(i);
        list_push(out, apply_func(it, f, &iv, 1));
    }
    return v_list(out);
}

static Value b_filter(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_GEOREUGI];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value f = need_arg(it, args, n, 1, fn);
    List *out = list_new(l->n);
    for (long i = 0; i < l->n; i++)
        if (v_truthy(apply_func(it, f, &l->items[i], 1))) list_push(out, l->items[i]);
    return v_list(out);
}

static Value b_partition(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_BUNHAL];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value f = need_arg(it, args, n, 1, fn);
    List *yes = list_new(4), *no = list_new(4);
    for (long i = 0; i < l->n; i++) {
        if (v_truthy(apply_func(it, f, &l->items[i], 1))) list_push(yes, l->items[i]);
        else list_push(no, l->items[i]);
    }
    List *out = list_new(2);
    list_push(out, v_list(yes)); list_push(out, v_list(no));
    return v_list(out);
}

static Value b_groupby(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_GEURUBHWA];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value f = need_arg(it, args, n, 1, fn);
    Dict *out = dict_new();
    for (long i = 0; i < l->n; i++) {
        Value k = apply_func(it, f, &l->items[i], 1);
        Value ex;
        if (dict_get(out, k, &ex)) list_push(ex.as.l, l->items[i]);
        else {
            List *g = list_new(4);
            list_push(g, l->items[i]);
            dict_set(out, k, v_list(g));
        }
    }
    return v_dict(out);
}

static Value b_reduce(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JEOBGI];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value acc = need_arg(it, args, n, 1, fn);
    Value f = need_arg(it, args, n, 2, fn);
    for (long i = 0; i < l->n; i++) {
        Value pair[2] = { acc, l->items[i] };
        acc = apply_func(it, f, pair, 2);
    }
    return acc;
}

static Value b_format(Interp *it, Value *args, int n) {
    Str *templ = n > 0 ? v_stringify(args[0]) : str_from("");
    GB b; gb_init(&b);
    int hole = 0;
    uint32_t start = 0;
    uint32_t i = 0;
    while (i < templ->len) {
        if (templ->data[i] == '{' && i + 1 < templ->len && templ->data[i + 1] == '}') {
            gb_put(&b, templ->data + start, i - start);
            if (hole < n - 1) gb_str(&b, v_stringify(args[hole + 1]));
            else gb_cz(&b, "{}");
            hole++;
            i += 2; start = i;
        } else i++;
    }
    gb_put(&b, templ->data + start, templ->len - start);
    return v_str(gb_done(&b));
}

/* keyed sort: decorate (key, idx, val), stable sort by key */
typedef struct { Value key, val; long idx; } KEnt;

static void ksort(Interp *it, KEnt *a, KEnt *tmp, long lo, long hi, const char *fn) {
    if (hi - lo <= 1) return;
    long mid = (lo + hi) / 2;
    ksort(it, a, tmp, lo, mid, fn);
    ksort(it, a, tmp, mid, hi, fn);
    long i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        bool err;
        int c = v_cmp(a[i].key, a[j].key, &err);
        if (err) g_error(it, FMT_CALL_ERR, fn, "unorderable types");
        tmp[k++] = c <= 0 ? a[i++] : a[j++];
    }
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi) tmp[k++] = a[j++];
    for (i = lo; i < hi; i++) a[i] = tmp[i];
}

static Value b_sortby(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JEONGRYEOLGIJUN];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value f = need_arg(it, args, n, 1, fn);
    KEnt *a = malloc(sizeof(KEnt) * (l->n ? l->n : 1));
    KEnt *tmp = malloc(sizeof(KEnt) * (l->n ? l->n : 1));
    for (long i = 0; i < l->n; i++) {
        a[i].val = l->items[i];
        a[i].key = apply_func(it, f, &l->items[i], 1);
        a[i].idx = i;
    }
    ksort(it, a, tmp, 0, l->n, fn);
    List *out = list_new(l->n);
    if (n > 2 && v_truthy(args[2])) for (long i = l->n - 1; i >= 0; i--) list_push(out, a[i].val);
    else for (long i = 0; i < l->n; i++) list_push(out, a[i].val);
    free(a); free(tmp);
    return v_list(out);
}

static Value b_topby(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_SANGWIGIJUN];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    int64_t cnt = n > 1 ? to_int64(it, args[1], fn) : 1;
    Value f = need_arg(it, args, n, 2, fn);
    KEnt *a = malloc(sizeof(KEnt) * (l->n ? l->n : 1));
    KEnt *tmp = malloc(sizeof(KEnt) * (l->n ? l->n : 1));
    for (long i = 0; i < l->n; i++) {
        a[i].val = l->items[i];
        a[i].key = apply_func(it, f, &l->items[i], 1);
        a[i].idx = i;
    }
    ksort(it, a, tmp, 0, l->n, fn);
    List *out = list_new(cnt);
    for (long i = l->n - 1; i >= 0 && (long)(l->n - 1 - i) < cnt; i--) list_push(out, a[i].val);
    free(a); free(tmp);
    return v_list(out);
}

static Value b_bestby(Interp *it, Value *args, int n, int ismax) {
    const char *fn = ismax ? BUILTIN_NAMES[B_CHOEDAEGIJUN] : BUILTIN_NAMES[B_CHOESOGIJUN];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value f = need_arg(it, args, n, 1, fn);
    if (!l->n) G_ERR0(it, ismax ? ERR_ARGMAX_EMPTY : ERR_ARGMIN_EMPTY);
    Value best = l->items[0], bestk = apply_func(it, f, &l->items[0], 1);
    for (long i = 1; i < l->n; i++) {
        Value k = apply_func(it, f, &l->items[i], 1);
        bool err;
        int c = v_cmp(k, bestk, &err);
        if (err) g_error(it, FMT_CALL_ERR, fn, "unorderable types");
        if (ismax ? c > 0 : c < 0) { best = l->items[i]; bestk = k; }
    }
    return best;
}

static Value b_findfn(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_CHAJGI];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value f = need_arg(it, args, n, 1, fn);
    for (long i = 0; i < l->n; i++)
        if (v_truthy(apply_func(it, f, &l->items[i], 1))) return l->items[i];
    return v_nil();
}

static Value b_any(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_ISSNA];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value f = need_arg(it, args, n, 1, fn);
    for (long i = 0; i < l->n; i++)
        if (v_truthy(apply_func(it, f, &l->items[i], 1))) return v_bool(1);
    return v_bool(0);
}

static Value b_all(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_MODU];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value f = need_arg(it, args, n, 1, fn);
    for (long i = 0; i < l->n; i++)
        if (!v_truthy(apply_func(it, f, &l->items[i], 1))) return v_bool(0);
    return v_bool(1);
}

/* ---------------------------------------------------------------- list transforms */
static bool list_contains(List *l, Value v) {
    for (long i = 0; i < l->n; i++) if (v_eq(l->items[i], v)) return true;
    return false;
}

static Value b_unique(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_GOYU];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    List *out = list_new(l->n);
    for (long i = 0; i < l->n; i++)
        if (!list_contains(out, l->items[i])) list_push(out, l->items[i]);
    return v_list(out);
}

static Value b_count(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_GAESU];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Value v = need_arg(it, args, n, 1, fn);
    int64_t c = 0;
    for (long i = 0; i < l->n; i++) if (v_eq(l->items[i], v)) c++;
    return v_int(c);
}

static Value b_freq(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_BINDO];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    Dict *d = dict_new();
    for (long i = 0; i < l->n; i++) {
        Value c;
        int64_t cur = dict_get(d, l->items[i], &c) ? c.as.i : 0;
        dict_set(d, l->items[i], v_int(cur + 1));
    }
    return v_dict(d);
}

static Value b_chunk(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_MUKKEUM];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    int64_t sz = to_int64(it, need_arg(it, args, n, 1, fn), fn);
    if (sz <= 0) G_ERR0(it, ERR_CHUNK_SIZE);
    List *out = list_new(l->n / sz + 1);
    for (long i = 0; i < l->n; i += sz) {
        List *part = list_new(sz);
        for (long j = i; j < i + sz && j < l->n; j++) list_push(part, l->items[j]);
        list_push(out, v_list(part));
    }
    return v_list(out);
}

static Value b_flatten(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_PYEONGTANHWA];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    List *out = list_new(l->n);
    for (long i = 0; i < l->n; i++) {
        if (l->items[i].tag == VT_LIST)
            for (long j = 0; j < l->items[i].as.l->n; j++) list_push(out, l->items[i].as.l->items[j]);
        else list_push(out, l->items[i]);
    }
    return v_list(out);
}

static Value b_cumsum(Interp *it, Value *args, int n, int isprod) {
    const char *fn = isprod ? BUILTIN_NAMES[B_NUJEOGGOB] : BUILTIN_NAMES[B_NUJEOGHAB];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    List *out = list_new(l->n);
    int64_t ia = isprod ? 1 : 0; double fa = ia; int anyfloat = 0;
    for (long i = 0; i < l->n; i++) {
        Value v = l->items[i];
        if (!is_num(v)) g_error(it, FMT_CALL_ERR, fn, "not a number");
        if (v.tag == VT_FLOAT && !anyfloat) { anyfloat = 1; fa = (double)ia; }
        if (anyfloat) {
            if (isprod) fa *= num_d(v);
            else fa += num_d(v);
            list_push(out, v_float(fa));
        } else {
            if (isprod) ia *= (v.tag == VT_INT ? v.as.i : v.as.b);
            else ia += (v.tag == VT_INT ? v.as.i : v.as.b);
            list_push(out, v_int(ia));
        }
    }
    return v_list(out);
}

static Value b_rotate(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_HOEJEON];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    List *out = list_new(l->n);
    if (!l->n) return v_list(out);
    int64_t k = n > 1 ? to_int64(it, args[1], fn) : 0;
    k %= l->n; if (k < 0) k += l->n;
    for (long i = k; i < l->n; i++) list_push(out, l->items[i]);
    for (long i = 0; i < k; i++) list_push(out, l->items[i]);
    return v_list(out);
}

static Value b_transpose(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JEONCHI];
    List *rows = need_list(it, need_arg(it, args, n, 0, fn), fn);
    List *out = list_new(4);
    if (!rows->n) return v_list(out);
    long minlen = -1;
    for (long i = 0; i < rows->n; i++) {
        List *r = need_list(it, rows->items[i], fn);
        if (minlen < 0 || r->n < minlen) minlen = r->n;
    }
    for (long c = 0; c < minlen; c++) {
        List *col = list_new(rows->n);
        for (long r = 0; r < rows->n; r++) list_push(col, rows->items[r].as.l->items[c]);
        list_push(out, v_list(col));
    }
    return v_list(out);
}

static Value b_zip(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_MUKKGI];
    List *a = need_list(it, need_arg(it, args, n, 0, fn), fn);
    List *b = need_list(it, need_arg(it, args, n, 1, fn), fn);
    long m = a->n < b->n ? a->n : b->n;
    List *out = list_new(m);
    for (long i = 0; i < m; i++) {
        List *pair = list_new(2);
        list_push(pair, a->items[i]);
        list_push(pair, b->items[i]);
        list_push(out, v_list(pair));
    }
    return v_list(out);
}

static Value b_setop(Interp *it, Value *args, int n, int op) {
    static const char *fns[3] = { BUILTIN_NAMES[B_GYOJIBHAB], BUILTIN_NAMES[B_HABJIBHAB], BUILTIN_NAMES[B_CHAJIBHAB] };
    const char *fn = fns[op];
    List *a = need_list(it, need_arg(it, args, n, 0, fn), fn);
    List *b = need_list(it, need_arg(it, args, n, 1, fn), fn);
    List *out = list_new(4);
    if (op == 0) {          /* intersection: in a and b, a order, dedup */
        for (long i = 0; i < a->n; i++)
            if (list_contains(b, a->items[i]) && !list_contains(out, a->items[i]))
                list_push(out, a->items[i]);
    } else if (op == 1) {   /* union */
        for (long i = 0; i < a->n; i++)
            if (!list_contains(out, a->items[i])) list_push(out, a->items[i]);
        for (long i = 0; i < b->n; i++)
            if (!list_contains(out, b->items[i])) list_push(out, b->items[i]);
    } else {                /* difference */
        for (long i = 0; i < a->n; i++)
            if (!list_contains(b, a->items[i]) && !list_contains(out, a->items[i]))
                list_push(out, a->items[i]);
    }
    return v_list(out);
}

/* ---------------------------------------------------------------- random */
static Value b_randomf(Interp *it, Value *args, int n) {
    (void)args; (void)n; (void)it;
    return v_float((double)arc4random() / 4294967296.0);
}

static Value b_randint(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_MUJAGWIJEONGSU];
    int64_t a = to_int64(it, need_arg(it, args, n, 0, fn), fn);
    int64_t b = to_int64(it, need_arg(it, args, n, 1, fn), fn);
    if (b < a) g_error(it, FMT_CALL_ERR, fn, "empty range");
    return v_int(a + (int64_t)arc4random_uniform((uint32_t)(b - a + 1)));
}

static Value b_choice(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_MUJAGWISEONTAEG];
    List *l = need_list(it, need_arg(it, args, n, 0, fn), fn);
    if (!l->n) g_error(it, FMT_CALL_ERR, fn, "empty list");
    return l->items[(long)arc4random_uniform((uint32_t)l->n)];
}

/* ---------------------------------------------------------------- misc */
static Value b_raise(Interp *it, Value *args, int n) {
    Str *msg = n > 0 ? v_stringify(args[0]) : str_from(STR_OORYU);
    g_error(it, "%s", msg->data);
}

static Value pad_common(Interp *it, Value *args, int n, int mode, const char *fn) {
    Str *s = v_stringify(need_arg(it, args, n, 0, fn));
    int64_t width = to_int64(it, need_arg(it, args, n, 1, fn), fn);
    Str *fill = n > 2 ? v_stringify(args[2]) : str_from(" ");
    uint32_t fadv;
    utf8_dec(fill->len ? fill->data : " ", &fadv);
    char fc[4] = {0};
    memcpy(fc, fill->len ? fill->data : " ", fill->len ? fadv : 1);
    int64_t len = (int64_t)str_cplen(s);
    if (len >= width) return v_str(s);
    int64_t marg = width - len, left, right;
    if (mode == 0) { left = marg; right = 0; }          /* rjust */
    else if (mode == 1) { left = 0; right = marg; }     /* ljust */
    else { left = marg / 2 + (marg & width & 1); right = marg - left; }  /* center */
    GB b; gb_init(&b);
    for (int64_t i = 0; i < left; i++) gb_put(&b, fc, fadv);
    gb_str(&b, s);
    for (int64_t i = 0; i < right; i++) gb_put(&b, fc, fadv);
    return v_str(gb_done(&b));
}

static Value b_model(Interp *it, Value *args, int n) {
    if (n > 0) it->ai_model = v_stringify(args[0]);
    return v_str(it->ai_model);
}

static Str *ai_stub(Str *content) {
    Str *cut = str_sub_cp(content, 0, 30);
    size_t need = strlen(STR_AI_NO_KEY) + cut->len + 8;
    char *buf = malloc(need);
    snprintf(buf, need, STR_AI_NO_KEY, cut->data);
    return str_own(buf, (uint32_t)strlen(buf));
}

static Value b_ai_q(Interp *it, Value *args, int n) {
    (void)it;
    return v_str(ai_stub(n > 0 ? v_stringify(args[0]) : str_from("")));
}

static Value b_ai_sq(Interp *it, Value *args, int n) {
    (void)it;
    return v_str(ai_stub(n > 1 ? v_stringify(args[1]) : str_from("")));
}

static Value b_ai_classify(Interp *it, Value *args, int n) {
    Str *text = n > 0 ? v_stringify(args[0]) : str_from("");
    GB labels; gb_init(&labels);
    if (n > 1 && args[1].tag == VT_LIST) {
        for (long i = 0; i < args[1].as.l->n; i++) {
            if (i) gb_cz(&labels, ", ");
            gb_str(&labels, v_stringify(args[1].as.l->items[i]));
        }
    }
    GB user; gb_init(&user);
    gb_cz(&user, STR_TEKST);
    gb_str(&user, text);
    gb_ch(&user, '\n');
    gb_cz(&user, STR_BOGI);
    gb_put(&user, labels.p, labels.n);
    Str *u = gb_done(&user);
    free(labels.p);
    return v_str(ai_stub(u));
}

static Value b_ai_text1(Interp *it, Value *args, int n) {
    (void)it;
    /* yoyag/beonyeog: user content is the text itself */
    return v_str(ai_stub(n > 0 ? v_stringify(args[0]) : str_from("")));
}

static Value b_ai_extract(Interp *it, Value *args, int n) {
    (void)it;
    Str *text = n > 0 ? v_stringify(args[0]) : str_from("");
    Str *instr = n > 1 ? v_stringify(args[1]) : str_from("");
    GB user; gb_init(&user);
    gb_cz(&user, STR_YOCHEONG);
    gb_str(&user, instr);
    gb_ch(&user, '\n');
    gb_cz(&user, STR_TEKST);
    gb_str(&user, text);
    return v_str(ai_stub(gb_done(&user)));
}

/* ---------------------------------------------------------------- module */
static char *mod_path_join(const char *base, const char *path) {
    size_t need = strlen(base) + strlen(path) + 2;
    char *full = malloc(need);
    snprintf(full, need, "%s/%s", base, path);
    int abs = full[0] == '/';
    size_t pc = strlen(full) / 2 + 2;
    char **parts = malloc(sizeof(char *) * pc);
    int np = 0;
    char *save = NULL;
    for (char *tok = strtok_r(full, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (!strcmp(tok, ".")) continue;
        if (!strcmp(tok, "..")) { if (np > 0) np--; continue; }
        parts[np++] = tok;
    }
    char *out = malloc(strlen(full) + 2);
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

static Value b_module(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_MODYUL];
    Str *p = v_stringify(need_arg(it, args, n, 0, fn));
    char *full = mod_path_join(it->base_dir, p->data);
    for (int i = 0; i < it->nmods; i++)
        if (!strcmp(it->mod_paths[i], full)) { free(full); return v_dict(it->mod_dicts[i]); }
    char *src = read_file_utf8(full);
    if (!src) { free(full); g_error(it, ERR_MODULE_FAIL, p->data); }
    Env *env = env_new(it, NULL);
    Dict *d = dict_new();
    env->shared = d;
    char *prev_dir = it->base_dir;
    int prev_line = it->cur_line;
    const char *slash = strrchr(full, '/');
    if (slash && slash != full) it->base_dir = strndup(full, (size_t)(slash - full));
    else if (slash == full) it->base_dir = strdup("/");
    else it->base_dir = strdup(".");
    Handler h;
    h.prev = it->top; it->top = &h;
    int code = _setjmp(h.jb);
    if (code == 0) {
        int nt;
        Tok *toks = lex_all(it, src, &nt);
        Node *ast = parse_all(it, toks);
        exec_block(it, ast, env);
        it->top = h.prev;
    } else {
        it->top = h.prev;
        it->base_dir = prev_dir; it->cur_line = prev_line;
        free(full);
        if (code == GS_ERR) {
            char *inner = it->err; it->err = NULL;
            g_error(it, ERR_MODULE_INNER, p->data, inner);
        }
        g_throw(it, code);
    }
    it->base_dir = prev_dir; it->cur_line = prev_line;
    if (it->nmods == it->capmods) {
        it->capmods = it->capmods ? it->capmods * 2 : 8;
        it->mod_paths = realloc(it->mod_paths, sizeof(char *) * it->capmods);
        it->mod_dicts = realloc(it->mod_dicts, sizeof(Dict *) * it->capmods);
    }
    it->mod_paths[it->nmods] = full;
    it->mod_dicts[it->nmods] = d;
    it->nmods++;
    return v_dict(d);
}

/* ---------------------------------------------------------------- json */
static void json_str(GB *b, Str *s) {
    gb_ch(b, '"');
    for (uint32_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->data[i];
        switch (c) {
        case '"': gb_cz(b, "\\\""); break;
        case '\\': gb_cz(b, "\\\\"); break;
        case '\b': gb_cz(b, "\\b"); break;
        case '\f': gb_cz(b, "\\f"); break;
        case '\n': gb_cz(b, "\\n"); break;
        case '\r': gb_cz(b, "\\r"); break;
        case '\t': gb_cz(b, "\\t"); break;
        default:
            if (c < 0x20) {
                char tmp[8];
                snprintf(tmp, sizeof tmp, "\\u%04x", c);
                gb_cz(b, tmp);
            } else gb_ch(b, (char)c);
        }
    }
    gb_ch(b, '"');
}

static void json_key(GB *b, Value k) {
    if (k.tag == VT_STR) { json_str(b, k.as.s); return; }
    if (k.tag == VT_INT) { char t[24]; snprintf(t, sizeof t, "%lld", (long long)k.as.i); gb_cz(b, "\""); gb_cz(b, t); gb_cz(b, "\""); return; }
    if (k.tag == VT_FLOAT) { gb_cz(b, "\""); gb_str(b, str_float_repr(k.as.f)); gb_cz(b, "\""); return; }
    if (k.tag == VT_BOOL) { gb_cz(b, k.as.b ? "\"true\"" : "\"false\""); return; }
    if (k.tag == VT_NIL) { gb_cz(b, "\"null\""); return; }
    json_str(b, v_stringify(k));
}

static void json_val(GB *b, Value v, int indent, int level) {
    char tmp[48];
    switch (v.tag) {
    case VT_NIL: gb_cz(b, "null"); return;
    case VT_BOOL: gb_cz(b, v.as.b ? "true" : "false"); return;
    case VT_INT: snprintf(tmp, sizeof tmp, "%lld", (long long)v.as.i); gb_cz(b, tmp); return;
    case VT_FLOAT:
        if (isnan(v.as.f)) { gb_cz(b, "NaN"); return; }
        if (isinf(v.as.f)) { gb_cz(b, v.as.f < 0 ? "-Infinity" : "Infinity"); return; }
        gb_str(b, str_float_repr(v.as.f));
        return;
    case VT_STR: json_str(b, v.as.s); return;
    case VT_LIST: {
        List *l = v.as.l;
        if (!l->n) { gb_cz(b, "[]"); return; }
        gb_ch(b, '[');
        for (long i = 0; i < l->n; i++) {
            if (i) gb_ch(b, ',');
            if (indent > 0) { gb_ch(b, '\n'); for (int k = 0; k < indent * (level + 1); k++) gb_ch(b, ' '); }
            else if (i) gb_ch(b, ' ');
            json_val(b, l->items[i], indent, level + 1);
        }
        if (indent > 0) { gb_ch(b, '\n'); for (int k = 0; k < indent * level; k++) gb_ch(b, ' '); }
        gb_ch(b, ']');
        return;
    }
    case VT_DICT: {
        Dict *d = v.as.d;
        if (!d->n) { gb_cz(b, "{}"); return; }
        gb_ch(b, '{');
        for (long i = 0; i < d->n; i++) {
            if (i) gb_ch(b, ',');
            if (indent > 0) { gb_ch(b, '\n'); for (int k = 0; k < indent * (level + 1); k++) gb_ch(b, ' '); }
            else if (i) gb_ch(b, ' ');
            json_key(b, d->items[i].key);
            gb_cz(b, ": ");
            json_val(b, d->items[i].val, indent, level + 1);
        }
        if (indent > 0) { gb_ch(b, '\n'); for (int k = 0; k < indent * level; k++) gb_ch(b, ' '); }
        gb_ch(b, '}');
        return;
    }
    case VT_FUNC: json_str(b, v_stringify(v)); return;
    }
}

static Value b_json_dumps(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JEISEUNMUNJAYEOL];
    int indent = 0;
    if (n > 1) indent = (int)to_int64(it, args[1], fn);
    GB b; gb_init(&b);
    json_val(&b, need_arg(it, args, n, 0, fn), indent, 0);
    return v_str(gb_done(&b));
}

typedef struct { const char *p; Interp *it; } JP;

static void jp_ws(JP *j) {
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') j->p++;
}

static Value jp_value(JP *j);

static Value jp_string(JP *j) {
    GB b; gb_init(&b);
    j->p++;   /* opening quote */
    while (*j->p && *j->p != '"') {
        unsigned char c = (unsigned char)*j->p;
        if (c == '\\') {
            j->p++;
            switch (*j->p) {
            case 'n': gb_ch(&b, '\n'); j->p++; break;
            case 't': gb_ch(&b, '\t'); j->p++; break;
            case 'r': gb_ch(&b, '\r'); j->p++; break;
            case 'b': gb_ch(&b, '\b'); j->p++; break;
            case 'f': gb_ch(&b, '\f'); j->p++; break;
            case '/': gb_ch(&b, '/'); j->p++; break;
            case '"': gb_ch(&b, '"'); j->p++; break;
            case '\\': gb_ch(&b, '\\'); j->p++; break;
            case 'u': {
                unsigned code = (unsigned)strtoul(j->p + 1, NULL, 16);
                j->p += 5;
                if (code >= 0xD800 && code <= 0xDBFF && j->p[0] == '\\' && j->p[1] == 'u') {
                    unsigned lo = (unsigned)strtoul(j->p + 2, NULL, 16);
                    code = 0x10000 + ((code - 0xD800) << 10) + (lo - 0xDC00);
                    j->p += 6;
                }
                char u8[4]; int un = 0;
                if (code < 0x80) u8[un++] = (char)code;
                else if (code < 0x800) { u8[un++] = (char)(0xC0 | (code >> 6)); u8[un++] = (char)(0x80 | (code & 0x3F)); }
                else if (code < 0x10000) { u8[un++] = (char)(0xE0 | (code >> 12)); u8[un++] = (char)(0x80 | ((code >> 6) & 0x3F)); u8[un++] = (char)(0x80 | (code & 0x3F)); }
                else { u8[un++] = (char)(0xF0 | (code >> 18)); u8[un++] = (char)(0x80 | ((code >> 12) & 0x3F)); u8[un++] = (char)(0x80 | ((code >> 6) & 0x3F)); u8[un++] = (char)(0x80 | (code & 0x3F)); }
                gb_put(&b, u8, un);
                break;
            }
            default: free(b.p); g_error(j->it, ERR_JSON_PARSE, "bad escape");
            }
        } else {
            gb_ch(&b, (char)c);
            j->p++;
        }
    }
    if (*j->p != '"') { free(b.p); g_error(j->it, ERR_JSON_PARSE, "unterminated string"); }
    j->p++;
    return v_str(gb_done(&b));
}

static Value jp_value(JP *j) {
    jp_ws(j);
    char c = *j->p;
    if (c == '{') {
        j->p++;
        Dict *d = dict_new();
        jp_ws(j);
        if (*j->p == '}') { j->p++; return v_dict(d); }
        for (;;) {
            jp_ws(j);
            if (*j->p != '"') g_error(j->it, ERR_JSON_PARSE, "expected string key");
            Value k = jp_string(j);
            jp_ws(j);
            if (*j->p != ':') g_error(j->it, ERR_JSON_PARSE, "expected ':'");
            j->p++;
            Value v = jp_value(j);
            dict_set(d, k, v);
            jp_ws(j);
            if (*j->p == ',') { j->p++; continue; }
            if (*j->p == '}') { j->p++; return v_dict(d); }
            g_error(j->it, ERR_JSON_PARSE, "expected ',' or '}'");
        }
    }
    if (c == '[') {
        j->p++;
        List *l = list_new(8);
        jp_ws(j);
        if (*j->p == ']') { j->p++; return v_list(l); }
        for (;;) {
            list_push(l, jp_value(j));
            jp_ws(j);
            if (*j->p == ',') { j->p++; continue; }
            if (*j->p == ']') { j->p++; return v_list(l); }
            g_error(j->it, ERR_JSON_PARSE, "expected ',' or ']'");
        }
    }
    if (c == '"') return jp_string(j);
    if (!strncmp(j->p, "true", 4)) { j->p += 4; return v_bool(1); }
    if (!strncmp(j->p, "false", 5)) { j->p += 5; return v_bool(0); }
    if (!strncmp(j->p, "null", 4)) { j->p += 4; return v_nil(); }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const char *start = j->p;
        int isfloat = 0;
        if (*j->p == '-') j->p++;
        while (*j->p >= '0' && *j->p <= '9') j->p++;
        if (*j->p == '.') { isfloat = 1; j->p++; while (*j->p >= '0' && *j->p <= '9') j->p++; }
        if (*j->p == 'e' || *j->p == 'E') {
            isfloat = 1; j->p++;
            if (*j->p == '+' || *j->p == '-') j->p++;
            while (*j->p >= '0' && *j->p <= '9') j->p++;
        }
        if (j->p == start || (j->p == start + 1 && *start == '-'))
            g_error(j->it, ERR_JSON_PARSE, "bad number");
        char tmp[64];
        size_t tl = (size_t)(j->p - start);
        if (tl > 63) tl = 63;
        memcpy(tmp, start, tl); tmp[tl] = 0;
        if (isfloat) return v_float(strtod(tmp, NULL));
        return v_int(strtoll(tmp, NULL, 10));
    }
    g_error(j->it, ERR_JSON_PARSE, "unexpected character");
}

static Value b_json_loads(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JEISEUNPASING];
    Str *s = need_str(it, need_arg(it, args, n, 0, fn), fn);
    JP j = { s->data, it };
    Value v = jp_value(&j);
    jp_ws(&j);
    if (*j.p) g_error(it, ERR_JSON_PARSE, "trailing data");
    return v;
}

/* ---------------------------------------------------------------- html */
static Str *html_escape(Value v) {
    if (v.tag == VT_STR && (v.as.s->flags & 1)) return v.as.s;
    Str *s = v_stringify(v);
    GB b; gb_init(&b);
    for (uint32_t i = 0; i < s->len; i++) {
        char c = s->data[i];
        switch (c) {
        case '&': gb_cz(&b, "&amp;"); break;
        case '<': gb_cz(&b, "&lt;"); break;
        case '>': gb_cz(&b, "&gt;"); break;
        case '"': gb_cz(&b, "&quot;"); break;
        case '\'': gb_cz(&b, "&#39;"); break;
        default: gb_ch(&b, c);
        }
    }
    return gb_done(&b);
}

static Str *html_flag(Str *s) { s->flags |= 1; return s; }

static const char *DOC_STYLE =
    "body{font-family:sans-serif;max-width:40em;margin:2em auto;padding:0 1em;line-height:1.6}"
    "input,button{font:inherit;padding:.3em .6em;margin:.2em 0}"
    "button{cursor:pointer}ul{padding-left:1.2em}";

static Value b_document(Interp *it, Value *args, int n) {
    (void)it;
    GB b; gb_init(&b);
    gb_cz(&b, "<!DOCTYPE html><html lang=\"ko\"><head><meta charset=\"utf-8\">"
              "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
              "<title>");
    gb_str(&b, html_escape(n > 0 ? args[0] : v_str(str_from(""))));
    gb_cz(&b, "</title><style>");
    gb_cz(&b, DOC_STYLE);
    gb_cz(&b, "</style></head><body>\n");
    for (int i = 1; i < n; i++) {
        if (i > 1) gb_ch(&b, '\n');
        gb_str(&b, html_escape(args[i]));
    }
    gb_cz(&b, "\n</body></html>");
    return v_str(html_flag(gb_done(&b)));
}

static Value b_para(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_GEUL];
    Str *text = html_escape(n > 0 ? args[0] : v_str(str_from("")));
    int64_t size = n > 1 ? to_int64(it, args[1], fn) : 0;
    GB b; gb_init(&b);
    char tag = 'p';
    char open[8], close[8];
    if (size >= 1 && size <= 3) {
        snprintf(open, sizeof open, "<h%lld>", (long long)size);
        snprintf(close, sizeof close, "</h%lld>", (long long)size);
    } else {
        snprintf(open, sizeof open, "<%c>", tag);
        snprintf(close, sizeof close, "</%c>", tag);
    }
    gb_cz(&b, open); gb_str(&b, text); gb_cz(&b, close);
    return v_str(html_flag(gb_done(&b)));
}

static Value b_listtag(Interp *it, Value *args, int n) {
    Value items = n > 0 ? args[0] : v_list(list_new(0));
    if (items.tag != VT_LIST) G_ERR0(it, ERR_LISTARG);
    GB b; gb_init(&b);
    gb_cz(&b, "<ul>");
    for (long i = 0; i < items.as.l->n; i++) {
        gb_cz(&b, "<li>");
        gb_str(&b, html_escape(items.as.l->items[i]));
        gb_cz(&b, "</li>");
    }
    gb_cz(&b, "</ul>");
    return v_str(html_flag(gb_done(&b)));
}

static Value b_link(Interp *it, Value *args, int n) {
    (void)it;
    Str *href = html_escape(n > 0 ? args[0] : v_str(str_from("")));
    Str *text = html_escape(n > 1 ? args[1] : (n > 0 ? args[0] : v_str(str_from(""))));
    GB b; gb_init(&b);
    gb_cz(&b, "<a href=\""); gb_str(&b, href); gb_cz(&b, "\">"); gb_str(&b, text); gb_cz(&b, "</a>");
    return v_str(html_flag(gb_done(&b)));
}

static Value b_form(Interp *it, Value *args, int n) {
    (void)it;
    Str *action = html_escape(n > 0 ? args[0] : v_str(str_from("")));
    Value fields = n > 1 ? args[1] : v_list(list_new(0));
    if (fields.tag != VT_LIST) G_ERR0(it, ERR_FORMARG);
    Str *btn = html_escape(n > 2 ? args[2] : v_str(str_from(STR_BONAEGI)));
    GB b; gb_init(&b);
    gb_cz(&b, "<form action=\""); gb_str(&b, action); gb_cz(&b, "\" method=\"get\">");
    for (long i = 0; i < fields.as.l->n; i++) {
        Str *f = html_escape(fields.as.l->items[i]);
        gb_cz(&b, "<label>"); gb_str(&b, f); gb_cz(&b, " <input name=\""); gb_str(&b, f); gb_cz(&b, "\"></label><br>");
    }
    gb_cz(&b, "<button>"); gb_str(&b, btn); gb_cz(&b, "</button></form>");
    return v_str(html_flag(gb_done(&b)));
}

static Value b_notimpl(Interp *it, Value *args, int n, int id) {
    (void)args; (void)n;
    g_error(it, ERR_NOT_IMPLEMENTED, BUILTIN_NAMES[id]);
}

/* ---------------------------------------------------------------- dispatch */
Value builtin_dispatch(Interp *it, int id, Value *args, int n) {
    switch (id) {
    case B_CHULRYEOG: return b_print(it, args, n);
    case B_GILI: return b_len(it, args, n);
    case B_SUSJA: return b_number(it, args, n);
    case B_CHUGA: return b_append(it, args, n);
    case B_HAB: return b_sum(it, args, n);
    case B_PYEONGGYUN: return b_mean(it, args, n);
    case B_JUNGANGGABS: return b_median(it, args, n);
    case B_PYOJUNPYEONCHA: return b_stdev(it, args, n);
    case B_CHOEBINGABS: return b_mode(it, args, n);
    case B_JEONGGYUHWA: return b_normalize(it, args, n);
    case B_JEONGRYEOL: return b_sort(it, args, n);
    case B_SANGWI: return b_topn(it, args, n);
    case B_CHOEDAE: return b_maxmin(it, args, n, 1);
    case B_CHOESO: return b_maxmin(it, args, n, 0);
    case B_BEOMWI: return b_range(it, args, n);
    case B_GANGYEOG: return b_linspace(it, args, n);
    case B_NANUGI: return b_split(it, args, n);
    case B_JULNANUGI: return b_splitlines(it, args, n);
    case B_WICHI: return b_find(it, args, n);
    case B_HABCHIGI: return b_join(it, args, n);
    case B_GEOKKURO: return b_reverse(it, args, n);
    case B_MODEL: return b_model(it, args, n);
    case B_JILMUN: return b_ai_q(it, args, n);
    case B_CHEGYEJILMUN: return b_ai_sq(it, args, n);
    case B_BUNRYU: return b_ai_classify(it, args, n);
    case B_YOYAG: return b_ai_text1(it, args, n);
    case B_BEONYEOG: return b_ai_text1(it, args, n);
    case B_CHUCHUL: return b_ai_extract(it, args, n);
    case B_DAEMUNJA: return b_case(it, args, n, 1);
    case B_SOMUNJA: return b_case(it, args, n, 0);
    case B_DADEUMGI: return b_strip(it, args, n, 3);
    case B_OENDADEUMGI: return b_strip(it, args, n, 1);
    case B_OREUNDADEUMGI: return b_strip(it, args, n, 2);
    case B_MALJULIM: return b_truncate(it, args, n);
    case B_BAKKUGI: return b_replace(it, args, n);
    case B_POHAM: return b_contains(it, args, n);
    case B_SIJAG: return b_startswith(it, args, n);
    case B_KKEUT: return b_endswith(it, args, n);
    case B_SUSJAINGA: return b_isnumber(it, args, n);
    case B_GEULJAINGA: return b_isalpha(it, args, n);
    case B_GONGBAEGINGA: return b_isspace(it, args, n);
    case B_TAIB: return b_type(it, args, n);
    case B_JEOLDAESGABS: return b_abs(it, args, n);
    case B_BANOLRIM: return b_round(it, args, n);
    case B_CHEONDANWI: return b_commas(it, args, n);
    case B_SAIGABS: return b_clamp(it, args, n);
    case B_CHOEDAEGONGYAGSU: return b_gcd(it, args, n);
    case B_CHOESOGONGBAESU: return b_lcm(it, args, n);
    case B_OLRIM: return b_ceilfloor(it, args, n, 1);
    case B_NAERIM: return b_ceilfloor(it, args, n, 0);
    case B_JEGOBGEUN: return b_sqrt(it, args, n);
    case B_GEODEUBJEGOB: return b_pow(it, args, n);
    case B_IBRYEOG: return b_input(it, args, n);
    case B_PAILILGGI: return b_readfile(it, args, n);
    case B_PAILSSEUGI: return b_writefile(it, args, n);
    case B_IEOSSEUGI: return b_appendfile(it, args, n);
    case B_PAILJONJAE: return b_fileexists(it, args, n);
    case B_PAILMOGROG: return b_listdir(it, args, n);
    case B_HYEONJAEWICHI: return b_getcwd(it, args, n);
    case B_WICHIBYEONGYEONG: return b_chdir(it, args, n);
    case B_POLDEOSAENGSEONG: return b_makedirs(it, args, n);
    case B_PAILSAGJE: return b_delete(it, args, n);
    case B_PAILBOGSA: return b_copy(it, args, n);
    case B_IREUMBAKKUGI: return b_rename(it, args, n);
    case B_PAILJEONGBO: return b_fileinfo(it, args, n);
    case B_OEBUSILHAENG: return b_system(it, args, n);
    case B_NALJJASIGAN: return b_datetime(it, args, n);
    case B_SAEGCHIL: return b_color(it, args, n);
    case B_HWANGYEONGBYEONSU: return b_getenv(it, args, n);
    case B_MODYUL: return b_module(it, args, n);
    case B_JIGEUM: return b_now(it, args, n);
    case B_KIDEUL: return b_keys(it, args, n);
    case B_GABSDEUL: return b_values(it, args, n);
    case B_HANGMOGDEUL: return b_items(it, args, n);
    case B_GABSEODGI: return b_dictget(it, args, n);
    case B_KIISSNA: return b_haskey(it, args, n);
    case B_JEISEUNPASING: return b_json_loads(it, args, n);
    case B_JEISEUNMUNJAYEOL: return b_json_dumps(it, args, n);
    case B_BYEONHWAN: return b_map(it, args, n);
    case B_SAENGSEONG: return b_generate(it, args, n);
    case B_GEOREUGI: return b_filter(it, args, n);
    case B_BUNHAL: return b_partition(it, args, n);
    case B_GEURUBHWA: return b_groupby(it, args, n);
    case B_JEOBGI: return b_reduce(it, args, n);
    case B_SEOSIG: return b_format(it, args, n);
    case B_JEONGRYEOLGIJUN: return b_sortby(it, args, n);
    case B_SANGWIGIJUN: return b_topby(it, args, n);
    case B_CHOEDAEGIJUN: return b_bestby(it, args, n, 1);
    case B_CHOESOGIJUN: return b_bestby(it, args, n, 0);
    case B_CHAJGI: return b_findfn(it, args, n);
    case B_ISSNA: return b_any(it, args, n);
    case B_MODU: return b_all(it, args, n);
    case B_GOYU: return b_unique(it, args, n);
    case B_GAESU: return b_count(it, args, n);
    case B_BINDO: return b_freq(it, args, n);
    case B_MUKKEUM: return b_chunk(it, args, n);
    case B_PYEONGTANHWA: return b_flatten(it, args, n);
    case B_MUKKGI: return b_zip(it, args, n);
    case B_NUJEOGHAB: return b_cumsum(it, args, n, 0);
    case B_NUJEOGGOB: return b_cumsum(it, args, n, 1);
    case B_HOEJEON: return b_rotate(it, args, n);
    case B_JEONCHI: return b_transpose(it, args, n);
    case B_MUJAGWI: return b_randomf(it, args, n);
    case B_MUJAGWIJEONGSU: return b_randint(it, args, n);
    case B_MUJAGWISEONTAEG: return b_choice(it, args, n);
    case B_BYEONGHAB: return b_merge(it, args, n);
    case B_SAJEONMANDEULGI: return b_dictfrom(it, args, n);
    case B_BALSAENG: return b_raise(it, args, n);
    case B_OENJJOGCHAEUGI: return pad_common(it, args, n, 0, BUILTIN_NAMES[B_OENJJOGCHAEUGI]);
    case B_OREUNJJOGCHAEUGI: return pad_common(it, args, n, 1, BUILTIN_NAMES[B_OREUNJJOGCHAEUGI]);
    case B_GAUNDECHAEUGI: return pad_common(it, args, n, 2, BUILTIN_NAMES[B_GAUNDECHAEUGI]);
    case B_GYOJIBHAB: return b_setop(it, args, n, 0);
    case B_HABJIBHAB: return b_setop(it, args, n, 1);
    case B_CHAJIBHAB: return b_setop(it, args, n, 2);
    case B_MUNSEO: return b_document(it, args, n);
    case B_GEUL: return b_para(it, args, n);
    case B_MOGROG: return b_listtag(it, args, n);
    case B_YEONGYEOL: return b_link(it, args, n);
    case B_IBRYEOGPOM: return b_form(it, args, n);
    case B_SEOBEO: return b_notimpl(it, args, n, B_SEOBEO);
    case B_JARYOYEOLGI: return b_notimpl(it, args, n, B_JARYOYEOLGI);
    case B_SILHAENG: return b_notimpl(it, args, n, B_SILHAENG);
    case B_JILUI: return b_notimpl(it, args, n, B_JILUI);
    case B_JARYODADGI: return b_notimpl(it, args, n, B_JARYODADGI);
    case B_JEOJANGSO: return b_notimpl(it, args, n, B_JEOJANGSO);
    case B_NEOHGI: return b_notimpl(it, args, n, B_NEOHGI);
    case B_GOCHIGI: return b_notimpl(it, args, n, B_GOCHIGI);
    case B_PPAEGI: return b_notimpl(it, args, n, B_PPAEGI);
    case B_GEORAE: return b_notimpl(it, args, n, B_GEORAE);
    }
    g_error(it, FMT_CALL_ERR, "builtin", "bad id");
}
