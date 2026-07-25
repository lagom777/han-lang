/* builtins.c — builtin functions + json + html helpers */
#include "ganada.h"
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdarg.h>
#include <signal.h>
#include <limits.h>
#include <sys/socket.h>
#include <netinet/in.h>

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
        Str *r = gc_alloc(sizeof(Str) + s->len + 1, GC_STR);
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
    List *keys = list_new(l->n);         /* keys are only in `a`: keep them alive */
    for (long i = 0; i < l->n; i++) {
        a[i].val = l->items[i];
        a[i].key = apply_func(it, f, &l->items[i], 1);
        list_push(keys, a[i].key);
        a[i].idx = i;
    }
    ksort(it, a, tmp, 0, l->n, fn);
    gc_keep_alive(keys);
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
    List *keys = list_new(l->n);         /* keys are only in `a`: keep them alive */
    for (long i = 0; i < l->n; i++) {
        a[i].val = l->items[i];
        a[i].key = apply_func(it, f, &l->items[i], 1);
        list_push(keys, a[i].key);
        a[i].idx = i;
    }
    ksort(it, a, tmp, 0, l->n, fn);
    gc_keep_alive(keys);
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
    char *out = malloc(need + 1);   /* strtok_r cut `full` up: use its old length */
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
        free(toks);                     /* the ast keeps no Tok pointers */
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
static void json_bytes(GB *b, const char *data, uint32_t len) {
    gb_ch(b, '"');
    for (uint32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
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

static void json_str(GB *b, Str *s) { json_bytes(b, s->data, s->len); }

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

/* ================================================================ db, store, server
 *
 * The python reference builds 자료열기 / 저장소 / 서버 on sqlite3 and http.server.
 * Linking libsqlite3 would add an external dependency (the rule here is the C
 * standard library plus what is already used), so this section carries a small
 * table engine and a POSIX socket HTTP server instead.  Two consequences, both
 * deliberate:
 *   - files on disk use our own text format, not the SQLite format.  A file the
 *     python side wrote is refused with a clear message, never misread.
 *   - 실행/질의 take the SQL the language's own examples and tests use: CREATE
 *     TABLE / INSERT / SELECT / UPDATE / DELETE / DROP with WHERE, ORDER BY,
 *     LIMIT and ? bindings.  Anything outside that fails loudly as unsupported
 *     instead of guessing.
 *
 * Table cells are plain malloc memory, never Value: the collector traces the C
 * stack and the env chain only, so a Value parked in a malloc'd struct would be
 * swept while the table still pointed at it.
 *
 * The korean strings below sit here instead of tables.h because tables.h is
 * generated by tools/gen_tables.py, which is not ours to change.  Every one was
 * taken byte for byte from the python interpreter source with the generator's
 * own escaping rule.
 */
#define SRV_BANNER            "\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4 \xEC\x84\x9C\xEB\xB2\x84: http://localhost:" /* 가나다 서버: http://localhost: */
#define SRV_404               "404 \xEC\x97\x86\xEB\x8A\x94 \xEA\xB2\xBD\xEB\xA1\x9C: " /* 404 없는 경로:  */
#define SRV_500               "500 \xEC\x84\x9C\xEB\xB2\x84 \xEC\x98\xA4\xEB\xA5\x98: " /* 500 서버 오류:  */
#define ERR_SRV_ROUTES        "\xEC\x84\x9C\xEB\xB2\x84: \xEB\x9D\xBC\xEC\x9A\xB0\xED\x8A\xB8\xEB\x8A\x94 {\xEA\xB2\xBD\xEB\xA1\x9C:\xED\x95\xA8\xEC\x88\x98} \xEC\x82\xAC\xEC\xA0\x84\xEC\x9D\xB4\xEC\x96\xB4\xEC\x95\xBC \xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4" /* 서버: 라우트는 {경로:함수} 사전이어야 합니다 */
#define ERR_DB_HANDLE         "%s: \xEC\xB2\xAB \xEC\x9D\xB8\xEC\x9E\x90\xEB\x8A\x94 \xEC\x9E\x90\xEB\xA3\x8C\xEC\x97\xB4\xEA\xB8\xB0() \xEA\xB0\x80 \xEB\x8F\x8C\xEB\xA0\xA4\xEC\xA4\x80 \xED\x95\xB8\xEB\x93\xA4\xEC\x9D\xB4\xEC\x96\xB4\xEC\x95\xBC \xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4" /* %s: 첫 인자는 자료열기() 가 돌려준 핸들이어야 합니다 */
#define ERR_DB_BINDLIST       "%s: \xEC\x9D\xB8\xEC\x9E\x90\xEB\xAA\xA9\xEB\xA1\x9D\xEC\x9D\x80 \xEB\xAA\xA9\xEB\xA1\x9D\xEC\x9D\xB4\xEC\x96\xB4\xEC\x95\xBC \xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4 \xE2\x80\x94 SQL \xEC\x9D\x98 ? \xEC\x9E\x90\xEB\xA6\xAC\xEC\x97\x90 \xEC\x88\x9C\xEC\x84\x9C\xEB\x8C\x80\xEB\xA1\x9C \xEB\x93\xA4\xEC\x96\xB4\xEA\xB0\x91\xEB\x8B\x88\xEB\x8B\xA4" /* %s: 인자목록은 목록이어야 합니다 — SQL 의 ? 자리에 순서대로 들어갑니다 */
#define ERR_DB_OPEN           "\xEC\x9E\x90\xEB\xA3\x8C\xEC\x97\xB4\xEA\xB8\xB0 \xEC\x98\xA4\xEB\xA5\x98: '%s' \xE2\x80\x94 %s" /* 자료열기 오류: '%s' — %s */
#define ERR_DB_EXEC           "\xEC\x8B\xA4\xED\x96\x89 \xEC\x98\xA4\xEB\xA5\x98: %s (\xEA\xB0\x92 \xEB\x81\xBC\xEC\x9B\x8C\xEB\x84\xA3\xEA\xB8\xB0\xEB\x8A\x94 \xEB\xAC\xB8\xEC\x9E\x90\xEC\x97\xB4 \xEC\x9E\x87\xEA\xB8\xB0 \xEB\x8C\x80\xEC\x8B\xA0 ? \xEC\x99\x80 \xEC\x9D\xB8\xEC\x9E\x90\xEB\xAA\xA9\xEB\xA1\x9D\xEC\x9D\x84 \xEC\x93\xB0\xEC\x84\xB8\xEC\x9A\x94)" /* 실행 오류: %s (값 끼워넣기는 문자열 잇기 대신 ? 와 인자목록을 쓰세요) */
#define ERR_DB_QUERY          "\xEC\xA7\x88\xEC\x9D\x98 \xEC\x98\xA4\xEB\xA5\x98: %s (\xEA\xB0\x92 \xEB\x81\xBC\xEC\x9B\x8C\xEB\x84\xA3\xEA\xB8\xB0\xEB\x8A\x94 \xEB\xAC\xB8\xEC\x9E\x90\xEC\x97\xB4 \xEC\x9E\x87\xEA\xB8\xB0 \xEB\x8C\x80\xEC\x8B\xA0 ? \xEC\x99\x80 \xEC\x9D\xB8\xEC\x9E\x90\xEB\xAA\xA9\xEB\xA1\x9D\xEC\x9D\x84 \xEC\x93\xB0\xEC\x84\xB8\xEC\x9A\x94)" /* 질의 오류: %s (값 끼워넣기는 문자열 잇기 대신 ? 와 인자목록을 쓰세요) */
#define ERR_STORE_HANDLE      "%s: \xEC\xB2\xAB \xEC\x9D\xB8\xEC\x9E\x90\xEB\x8A\x94 \xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C() \xEA\xB0\x80 \xEB\x8F\x8C\xEB\xA0\xA4\xEC\xA4\x80 \xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C\xEC\x97\xAC\xEC\x95\xBC \xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4" /* %s: 첫 인자는 저장소() 가 돌려준 저장소여야 합니다 */
#define ERR_STORE_ARGS        "\xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C: \xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C(\xEA\xB2\xBD\xEB\xA1\x9C, \xEC\x9D\xB4\xEB\xA6\x84) \xE2\x80\x94 \xED\x8C\x8C\xEC\x9D\xBC \xEA\xB2\xBD\xEB\xA1\x9C\xEC\x99\x80 \xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C \xEC\x9D\xB4\xEB\xA6\x84 \xEB\x91\x98 \xEB\x8B\xA4 \xED\x95\x84\xEC\x9A\x94\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4" /* 저장소: 저장소(경로, 이름) — 파일 경로와 저장소 이름 둘 다 필요합니다 */
#define ERR_PUT_DICT          "\xEB\x84\xA3\xEA\xB8\xB0: \xEB\x91\x90 \xEB\xB2\x88\xEC\xA7\xB8 \xEC\x9D\xB8\xEC\x9E\x90\xEB\x8A\x94 \xEC\x82\xAC\xEC\xA0\x84\xEC\x9D\xB4\xEC\x96\xB4\xEC\x95\xBC \xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4 \xE2\x80\x94 \xEB\x84\xA3\xEA\xB8\xB0(\xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C, {\x22\xED\x82\xA4\x22: \xEA\xB0\x92})" /* 넣기: 두 번째 인자는 사전이어야 합니다 — 넣기(저장소, {"키": 값}) */
#define ERR_FIND_DICT         "\xEC\xB0\xBE\xEA\xB8\xB0: \xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C \xEA\xB2\x80\xEC\x83\x89 \xEC\xA1\xB0\xEA\xB1\xB4\xEC\x9D\x80 \xEC\x82\xAC\xEC\xA0\x84\xEC\x9D\xB4\xEC\x96\xB4\xEC\x95\xBC \xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4 \xE2\x80\x94 \xEC\xB0\xBE\xEA\xB8\xB0(\xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C, {\x22\xED\x82\xA4\x22: \xEA\xB0\x92})" /* 찾기: 저장소 검색 조건은 사전이어야 합니다 — 찾기(저장소, {"키": 값}) */
#define ERR_FIX_ARGS          "\xEA\xB3\xA0\xEC\xB9\x98\xEA\xB8\xB0: \xEA\xB3\xA0\xEC\xB9\x98\xEA\xB8\xB0(\xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C, \xEB\xB2\x88\xED\x98\xB8, \xEC\x82\xAC\xEC\xA0\x84) \xE2\x80\x94 \xEB\xB2\x88\xED\x98\xB8\xEC\x99\x80 \xEA\xB3\xA0\xEC\xB9\xA0 \xEB\x82\xB4\xEC\x9A\xA9 \xEC\x82\xAC\xEC\xA0\x84\xEC\x9D\xB4 \xED\x95\x84\xEC\x9A\x94\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4" /* 고치기: 고치기(저장소, 번호, 사전) — 번호와 고칠 내용 사전이 필요합니다 */
#define ERR_DEL_ARGS          "\xEB\xB9\xBC\xEA\xB8\xB0: \xEB\xB9\xBC\xEA\xB8\xB0(\xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C, \xEB\xB2\x88\xED\x98\xB8) \xE2\x80\x94 \xEC\xA7\x80\xEC\x9A\xB8 \xEA\xB8\xB0\xEB\xA1\x9D\xEC\x9D\x98 \xEB\xB2\x88\xED\x98\xB8\xEA\xB0\x80 \xED\x95\x84\xEC\x9A\x94\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4" /* 빼기: 빼기(저장소, 번호) — 지울 기록의 번호가 필요합니다 */
#define ERR_TX_HANDLE         "\xEA\xB1\xB0\xEB\x9E\x98: \xEC\xB2\xAB \xEC\x9D\xB8\xEC\x9E\x90\xEB\x8A\x94 \xEC\x9E\x90\xEB\xA3\x8C\xEC\x97\xB4\xEA\xB8\xB0() \xED\x95\xB8\xEB\x93\xA4\xEC\x9D\xB4\xEB\x82\x98 \xEC\xA0\x80\xEC\x9E\xA5\xEC\x86\x8C() \xEC\x97\xAC\xEC\x95\xBC \xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4" /* 거래: 첫 인자는 자료열기() 핸들이나 저장소() 여야 합니다 */
#define STR_BEONHO            "\xEB\xB2\x88\xED\x98\xB8" /* 번호 */
#define STR_JARYO_COL         "\xEC\x9E\x90\xEB\xA3\x8C" /* 자료 */
#define STR_REQ_METHOD        "\xEB\xA9\x94\xEC\x84\x9C\xEB\x93\x9C" /* 메서드 */
#define STR_REQ_PATH          "\xEA\xB2\xBD\xEB\xA1\x9C" /* 경로 */
#define STR_REQ_QUERY         "\xEC\xA7\x88\xEC\x9D\x98" /* 질의 */
#define STR_REQ_BODY          "\xEB\xB3\xB8\xEB\xAC\xB8" /* 본문 */
#define STR_REQ_COOKIE        "\xEC\xBF\xA0\xED\x82\xA4" /* 쿠키 */
#define STR_RES_STATUS        "\xEC\x83\x81\xED\x83\x9C" /* 상태 */
#define STR_RES_HEADERS       "\xED\x97\xA4\xEB\x8D\x94" /* 헤더 */
#define STR_RES_SETCOOKIE     "\xEC\xBF\xA0\xED\x82\xA4\xEC\x84\xA4\xEC\xA0\x95" /* 쿠키설정 */
#define STR_COOKIE_VAL        "\xEA\xB0\x92" /* 값 */
#define STR_COOKIE_MAXAGE     "\xEB\xA7\x8C\xEB\xA3\x8C" /* 만료 */

/* handle marker keys: a source string cannot hold a \x01, so a program can
 * neither forge a handle nor collide with one */
#define HK_DB    "\x01" "db"
#define HK_STORE "\x01" "store"
#define HK_TAB   "\x01" "tab"

/* ---------------------------------------------------------------- cells */
enum { CELL_NULL, CELL_INT, CELL_REAL, CELL_TEXT };
enum { AFF_NONE, AFF_TEXT, AFF_NUM, AFF_INT, AFF_REAL };

typedef struct {
    uint8_t kind;
    int64_t i;
    double f;
    char *s;                        /* CELL_TEXT: malloc'd, NUL terminated */
    uint32_t slen;
} Cell;

static Cell cell_null(void) { Cell c; memset(&c, 0, sizeof c); c.kind = CELL_NULL; return c; }
static Cell cell_int(int64_t v) { Cell c = cell_null(); c.kind = CELL_INT; c.i = v; return c; }
static Cell cell_real(double v) { Cell c = cell_null(); c.kind = CELL_REAL; c.f = v; return c; }

static Cell cell_text(const char *s, uint32_t n) {
    Cell c = cell_null();
    c.kind = CELL_TEXT; c.slen = n;
    c.s = malloc((size_t)n + 1);
    memcpy(c.s, s, n); c.s[n] = 0;
    return c;
}

static void cell_clear(Cell *c) {
    if (c->kind == CELL_TEXT) free(c->s);
    *c = cell_null();
}

static Cell cell_dup(const Cell *o) {
    return o->kind == CELL_TEXT ? cell_text(o->s, o->slen) : *o;
}

static Cell cell_from_value(Value v) {
    switch (v.tag) {
    case VT_BOOL: return cell_int(v.as.b);
    case VT_INT: return cell_int(v.as.i);
    case VT_FLOAT: return cell_real(v.as.f);
    case VT_STR: return cell_text(v.as.s->data, v.as.s->len);
    default: return cell_null();
    }
}

static Value cell_to_value(const Cell *c) {
    switch (c->kind) {
    case CELL_INT: return v_int(c->i);
    case CELL_REAL: return v_float(c->f);
    case CELL_TEXT: return v_str(str_new(c->s, c->slen));
    default: return v_nil();
    }
}

/* text -> number the sqlite way: the whole string must be a numeric literal */
static int text_as_num(const char *s, uint32_t n, int64_t *ip, double *fp, int *isint) {
    char buf[64];
    if (n == 0 || n >= sizeof buf) return 0;
    memcpy(buf, s, n); buf[n] = 0;
    char *end;
    errno = 0;
    long long iv = strtoll(buf, &end, 10);
    if (end != buf && !*end && errno != ERANGE) { *ip = (int64_t)iv; *isint = 1; return 1; }
    errno = 0;
    double dv = strtod(buf, &end);
    if (end != buf && !*end) { *fp = dv; *isint = 0; return 1; }
    return 0;
}

/* column affinity, applied on store and on comparison */
static void cell_coerce(Cell *c, int aff) {
    if (aff == AFF_TEXT) {
        if (c->kind == CELL_INT) {
            char b[32];
            snprintf(b, sizeof b, "%lld", (long long)c->i);
            *c = cell_text(b, (uint32_t)strlen(b));
        } else if (c->kind == CELL_REAL) {
            Str *r = str_float_repr(c->f);
            *c = cell_text(r->data, r->len);
        }
        return;
    }
    if (aff == AFF_INT || aff == AFF_REAL || aff == AFF_NUM) {
        if (c->kind == CELL_TEXT) {
            int64_t iv = 0; double dv = 0; int isint = 0;
            if (text_as_num(c->s, c->slen, &iv, &dv, &isint)) {
                free(c->s);
                *c = isint ? cell_int(iv) : cell_real(dv);
            }
        }
        if (aff == AFF_REAL && c->kind == CELL_INT) *c = cell_real((double)c->i);
        if ((aff == AFF_INT || aff == AFF_NUM) && c->kind == CELL_REAL
            && c->f == floor(c->f) && fabs(c->f) < 9.0e18)
            *c = cell_int((int64_t)c->f);
    }
}

static int cell_class(const Cell *c) {          /* sqlite orders null < number < text */
    return c->kind == CELL_NULL ? 0 : c->kind == CELL_TEXT ? 2 : 1;
}

static int cell_cmp(const Cell *a, const Cell *b) {
    int ca = cell_class(a), cb = cell_class(b);
    if (ca != cb) return ca < cb ? -1 : 1;
    if (ca == 0) return 0;
    if (ca == 2) {
        uint32_t n = a->slen < b->slen ? a->slen : b->slen;
        int r = n ? memcmp(a->s, b->s, n) : 0;
        if (r) return r < 0 ? -1 : 1;
        return a->slen == b->slen ? 0 : (a->slen < b->slen ? -1 : 1);
    }
    if (a->kind == CELL_INT && b->kind == CELL_INT)
        return a->i == b->i ? 0 : (a->i < b->i ? -1 : 1);
    double x = a->kind == CELL_INT ? (double)a->i : a->f;
    double y = b->kind == CELL_INT ? (double)b->i : b->f;
    return x == y ? 0 : (x < y ? -1 : 1);
}

static int cell_truth(const Cell *c) {
    switch (c->kind) {
    case CELL_INT: return c->i != 0;
    case CELL_REAL: return c->f != 0;
    case CELL_TEXT: {
        int64_t iv = 0; double dv = 0; int isint = 0;
        if (!text_as_num(c->s, c->slen, &iv, &dv, &isint)) return 0;
        return isint ? iv != 0 : dv != 0;
    }
    default: return 0;
    }
}

/* ---------------------------------------------------------------- tables */
typedef struct { char *name; uint8_t aff; uint8_t rowid_alias; } DbCol;
typedef struct { int64_t rowid; Cell *cells; } DbRow;

typedef struct {
    char *name;
    DbCol *cols; int ncols;
    DbRow *rows; long nrows, caprows;
    int64_t seq;                    /* largest rowid ever handed out */
    int autoinc;
} DbTable;

typedef struct {
    char *path;                     /* NULL: ":memory:" */
    int id;                         /* handle id, never reused while the process runs */
    DbTable **tabs; int ntabs, captabs;
    int refs;                       /* open handles pointing here */
    int tx;                         /* 거래 nesting depth */
    DbTable **snap; int nsnap;      /* rollback copy, taken when tx goes 0 -> 1 */
} Db;

static Db **g_dbs;                  /* open databases, compacted on close */
static int g_ndbs, g_capdbs, g_dbseq;

static int ascii_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* case insensitive over ascii, exact over the rest (utf-8 names have no case) */
static int name_eq(const char *a, uint32_t an, const char *b, uint32_t bn) {
    if (an != bn) return 0;
    for (uint32_t i = 0; i < an; i++)
        if (ascii_lower((unsigned char)a[i]) != ascii_lower((unsigned char)b[i])) return 0;
    return 1;
}

static int name_eqz(const char *a, uint32_t an, const char *b) {
    return name_eq(a, an, b, (uint32_t)strlen(b));
}

static DbTable *tab_find(Db *db, const char *name, uint32_t n) {
    for (int i = 0; i < db->ntabs; i++)
        if (name_eqz(name, n, db->tabs[i]->name)) return db->tabs[i];
    return NULL;
}

static DbTable *tab_alloc(const char *name, uint32_t n) {
    DbTable *t = calloc(1, sizeof *t);
    t->name = malloc((size_t)n + 1);
    memcpy(t->name, name, n); t->name[n] = 0;
    return t;
}

static void db_attach(Db *db, DbTable *t) {
    if (db->ntabs == db->captabs) {
        db->captabs = db->captabs ? db->captabs * 2 : 4;
        db->tabs = realloc(db->tabs, sizeof(DbTable *) * (size_t)db->captabs);
    }
    db->tabs[db->ntabs++] = t;
}

static DbTable *tab_new(Db *db, const char *name, uint32_t n) {
    DbTable *t = tab_alloc(name, n);
    db_attach(db, t);
    return t;
}

static void tab_add_col(DbTable *t, const char *name, uint32_t n, int aff, int rowid_alias) {
    t->cols = realloc(t->cols, sizeof(DbCol) * (size_t)(t->ncols + 1));
    DbCol *c = &t->cols[t->ncols++];
    c->name = malloc((size_t)n + 1);
    memcpy(c->name, name, n); c->name[n] = 0;
    c->aff = (uint8_t)aff;
    c->rowid_alias = (uint8_t)rowid_alias;
}

static int tab_col_index(DbTable *t, const char *name, uint32_t n) {
    for (int i = 0; i < t->ncols; i++)
        if (name_eqz(name, n, t->cols[i].name)) return i;
    return -1;
}

static int tab_rowid_col(DbTable *t) {
    for (int i = 0; i < t->ncols; i++) if (t->cols[i].rowid_alias) return i;
    return -1;
}

/* column index for a name: -1 is the implicit rowid, -2 no such column.
 * "rowid" resolves to an INTEGER PRIMARY KEY column when the table has one, so
 * that SELECT rowid reports that column's name, as sqlite does. */
static int tab_resolve_col(DbTable *t, const char *name, uint32_t n) {
    int c = tab_col_index(t, name, n);
    if (c >= 0) return c;
    if (name_eqz(name, n, "rowid") || name_eqz(name, n, "oid") || name_eqz(name, n, "_rowid_")) {
        int alias = tab_rowid_col(t);
        return alias >= 0 ? alias : -1;
    }
    return -2;
}

static void tab_free(DbTable *t) {
    for (long r = 0; r < t->nrows; r++) {
        for (int c = 0; c < t->ncols; c++) cell_clear(&t->rows[r].cells[c]);
        free(t->rows[r].cells);
    }
    free(t->rows);
    for (int c = 0; c < t->ncols; c++) free(t->cols[c].name);
    free(t->cols);
    free(t->name);
    free(t);
}

static DbTable *tab_clone(const DbTable *o) {
    DbTable *t = tab_alloc(o->name, (uint32_t)strlen(o->name));
    t->seq = o->seq; t->autoinc = o->autoinc; t->ncols = o->ncols;
    t->cols = malloc(sizeof(DbCol) * (size_t)(o->ncols ? o->ncols : 1));
    for (int c = 0; c < o->ncols; c++) {
        t->cols[c] = o->cols[c];
        t->cols[c].name = strdup(o->cols[c].name);
    }
    t->nrows = t->caprows = o->nrows;
    if (o->nrows) {
        t->rows = malloc(sizeof(DbRow) * (size_t)o->nrows);
        for (long r = 0; r < o->nrows; r++) {
            t->rows[r].rowid = o->rows[r].rowid;
            t->rows[r].cells = malloc(sizeof(Cell) * (size_t)(o->ncols ? o->ncols : 1));
            for (int c = 0; c < o->ncols; c++)
                t->rows[r].cells[c] = cell_dup(&o->rows[r].cells[c]);
        }
    }
    return t;
}

/* rows stay in rowid order, so a plain scan matches sqlite's scan order */
static DbRow *tab_insert(DbTable *t, int64_t rowid) {
    if (t->nrows == t->caprows) {
        t->caprows = t->caprows ? t->caprows * 2 : 8;
        t->rows = realloc(t->rows, sizeof(DbRow) * (size_t)t->caprows);
    }
    long at = t->nrows;
    while (at > 0 && t->rows[at - 1].rowid > rowid) at--;
    memmove(&t->rows[at + 1], &t->rows[at], sizeof(DbRow) * (size_t)(t->nrows - at));
    t->nrows++;
    t->rows[at].rowid = rowid;
    t->rows[at].cells = malloc(sizeof(Cell) * (size_t)(t->ncols ? t->ncols : 1));
    for (int c = 0; c < t->ncols; c++) t->rows[at].cells[c] = cell_null();
    if (rowid > t->seq) t->seq = rowid;
    return &t->rows[at];
}

static long tab_find_rowid(DbTable *t, int64_t rowid) {
    for (long r = 0; r < t->nrows; r++) if (t->rows[r].rowid == rowid) return r;
    return -1;
}

static void tab_remove(DbTable *t, long at) {
    for (int c = 0; c < t->ncols; c++) cell_clear(&t->rows[at].cells[c]);
    free(t->rows[at].cells);
    memmove(&t->rows[at], &t->rows[at + 1], sizeof(DbRow) * (size_t)(t->nrows - at - 1));
    t->nrows--;
}

static int64_t tab_next_rowid(DbTable *t) {
    if (t->autoinc) return t->seq + 1;
    int64_t mx = 0;
    for (long r = 0; r < t->nrows; r++) if (t->rows[r].rowid > mx) mx = t->rows[r].rowid;
    return mx + 1;
}

static void db_drop_tables(DbTable ***tabs, int *n) {
    for (int i = 0; i < *n; i++) tab_free((*tabs)[i]);
    free(*tabs);
    *tabs = NULL; *n = 0;
}

/* ---------------------------------------------------------------- on disk */
#define DB_MAGIC "GANADA-DB 1\n"

static int64_t scan_int(const char **p) {
    char *e;
    long long v = strtoll(*p, &e, 10);
    *p = e;
    return (int64_t)v;
}

static double scan_real(const char **p) {
    char *e;
    double v = strtod(*p, &e);
    *p = e;
    return v;
}

static void db_put_cell(GB *b, const Cell *c) {
    char t[64];
    switch (c->kind) {
    case CELL_INT: snprintf(t, sizeof t, " I%lld", (long long)c->i); gb_cz(b, t); break;
    case CELL_REAL: gb_cz(b, " F"); gb_str(b, str_float_repr(c->f)); break;
    case CELL_TEXT: gb_cz(b, " S"); json_bytes(b, c->s, c->slen); break;
    default: gb_cz(b, " N");
    }
}

static void db_save(Interp *it, Db *db) {
    if (!db->path || db->tx) return;
    GB b; gb_init(&b);
    gb_cz(&b, DB_MAGIC);
    char t[80];
    for (int i = 0; i < db->ntabs; i++) {
        DbTable *tb = db->tabs[i];
        gb_cz(&b, "T ");
        json_bytes(&b, tb->name, (uint32_t)strlen(tb->name));
        snprintf(t, sizeof t, " %lld %d\n", (long long)tb->seq, tb->autoinc);
        gb_cz(&b, t);
        for (int c = 0; c < tb->ncols; c++) {
            gb_cz(&b, "C ");
            json_bytes(&b, tb->cols[c].name, (uint32_t)strlen(tb->cols[c].name));
            snprintf(t, sizeof t, " %d %d\n", tb->cols[c].aff, tb->cols[c].rowid_alias);
            gb_cz(&b, t);
        }
        for (long r = 0; r < tb->nrows; r++) {
            snprintf(t, sizeof t, "R %lld", (long long)tb->rows[r].rowid);
            gb_cz(&b, t);
            for (int c = 0; c < tb->ncols; c++) db_put_cell(&b, &tb->rows[r].cells[c]);
            gb_ch(&b, '\n');
        }
    }
    FILE *f = fopen(db->path, "wb");
    if (!f) { free(b.p); g_error(it, ERR_FILE_WRITE, db->path); }
    fwrite(b.p, 1, b.n, f);
    fclose(f);
    free(b.p);
}

/* a json escaped string at *p, appended to out; advances *p */
static int db_unesc(const char **p, GB *out) {
    const char *s = *p;
    if (*s != '"') return 0;
    s++;
    while (*s && *s != '"') {
        if (*s != '\\') { gb_ch(out, *s++); continue; }
        s++;
        switch (*s) {
        case 'n': gb_ch(out, '\n'); s++; break;
        case 't': gb_ch(out, '\t'); s++; break;
        case 'r': gb_ch(out, '\r'); s++; break;
        case 'b': gb_ch(out, '\b'); s++; break;
        case 'f': gb_ch(out, '\f'); s++; break;
        case '"': gb_ch(out, '"'); s++; break;
        case '\\': gb_ch(out, '\\'); s++; break;
        case 'u': {                                  /* the writer escapes only < 0x20 */
            char hx[5] = { s[1], s[2], s[3], s[4], 0 };
            gb_ch(out, (char)strtoul(hx, NULL, 16));
            s += 5;
            break;
        }
        default: return 0;
        }
    }
    if (*s != '"') return 0;
    *p = s + 1;
    return 1;
}

static void db_skip_sp(const char **p) { while (**p == ' ' || **p == '\t') (*p)++; }

static int db_load(Db *db, const char *text, const char **why) {
    const char *p = text;
    if (strncmp(p, DB_MAGIC, strlen(DB_MAGIC))) {
        *why = !strncmp(p, "SQLite format 3", 15)
             ? "a SQLite file from the python implementation cannot be read here"
             : "file is not a database";
        return 0;
    }
    p += strlen(DB_MAGIC);
    DbTable *cur = NULL;
    while (*p) {
        char kind = *p;
        if (kind == '\n') { p++; continue; }
        p++;
        db_skip_sp(&p);
        if (kind == 'T') {
            GB nb; gb_init(&nb);
            if (!db_unesc(&p, &nb)) { free(nb.p); *why = "file is not a database"; return 0; }
            cur = tab_new(db, nb.p, (uint32_t)nb.n);
            free(nb.p);
            db_skip_sp(&p);
            cur->seq = scan_int(&p);
            db_skip_sp(&p);
            cur->autoinc = (int)scan_int(&p);
        } else if (kind == 'C' && cur) {
            GB nb; gb_init(&nb);
            if (!db_unesc(&p, &nb)) { free(nb.p); *why = "file is not a database"; return 0; }
            db_skip_sp(&p);
            int aff = (int)scan_int(&p);
            db_skip_sp(&p);
            int ra = (int)scan_int(&p);
            tab_add_col(cur, nb.p, (uint32_t)nb.n, aff, ra);
            free(nb.p);
        } else if (kind == 'R' && cur) {
            DbRow *row = tab_insert(cur, scan_int(&p));
            for (int c = 0; c < cur->ncols; c++) {
                db_skip_sp(&p);
                char k = *p ? *p++ : 'N';
                if (k == 'I') row->cells[c] = cell_int(scan_int(&p));
                else if (k == 'F') row->cells[c] = cell_real(scan_real(&p));
                else if (k == 'S') {
                    GB vb; gb_init(&vb);
                    if (!db_unesc(&p, &vb)) { free(vb.p); *why = "file is not a database"; return 0; }
                    row->cells[c] = cell_text(vb.p, (uint32_t)vb.n);
                    free(vb.p);
                }
            }
        } else {
            *why = "file is not a database";
            return 0;
        }
        while (*p && *p != '\n') p++;
    }
    return 1;
}

/* ---------------------------------------------------------------- handles */
static Db *db_open(Interp *it, const char *path) {
    int memory = !strcmp(path, ":memory:");
    if (!memory)
        for (int i = 0; i < g_ndbs; i++)                 /* one file, one image */
            if (g_dbs[i] && g_dbs[i]->path && !strcmp(g_dbs[i]->path, path)) {
                g_dbs[i]->refs++;
                return g_dbs[i];
            }
    Db *db = calloc(1, sizeof *db);
    db->refs = 1;
    if (!memory) {
        db->path = strdup(path);
        FILE *f = fopen(path, "rb");
        if (!f) {                                        /* sqlite creates it too */
            f = fopen(path, "ab");
            if (!f) {
                free(db->path); free(db);
                g_error(it, ERR_DB_OPEN, path, "unable to open database file");
            }
            fclose(f);
            f = fopen(path, "rb");
        }
        if (f) {
            GB b; gb_init(&b);
            char tmp[8192];
            size_t got;
            while ((got = fread(tmp, 1, sizeof tmp, f)) > 0) gb_put(&b, tmp, got);
            fclose(f);
            const char *why = "file is not a database";
            if (b.n && !db_load(db, b.p, &why)) {
                free(b.p);
                db_drop_tables(&db->tabs, &db->ntabs);
                free(db->path); free(db);
                g_error(it, ERR_DB_OPEN, path, why);
            }
            free(b.p);
        }
    }
    if (g_ndbs == g_capdbs) {
        g_capdbs = g_capdbs ? g_capdbs * 2 : 4;
        g_dbs = realloc(g_dbs, sizeof(Db *) * (size_t)g_capdbs);
    }
    db->id = ++g_dbseq;
    g_dbs[g_ndbs++] = db;
    return db;
}

static void db_close(Db *db) {
    if (--db->refs > 0) return;
    for (int i = 0; i < g_ndbs; i++)
        if (g_dbs[i] == db) {
            memmove(&g_dbs[i], &g_dbs[i + 1], sizeof(Db *) * (size_t)(g_ndbs - i - 1));
            g_ndbs--;
            break;
        }
    db_drop_tables(&db->tabs, &db->ntabs);
    db_drop_tables(&db->snap, &db->nsnap);
    free(db->path);
    free(db);
}

/* a handle only ever names a still open database: ids are not recycled, so a
 * handle left over from 자료닫기 cannot land on a later database */
static Db *db_by_id(int id) {
    for (int i = 0; i < g_ndbs; i++) if (g_dbs[i]->id == id) return g_dbs[i];
    return NULL;
}

static int handle_id(Value v, const char *key) {
    if (v.tag != VT_DICT) return 0;
    Value slot;
    if (!dict_get(v.as.d, v_str(str_from(key)), &slot)) return 0;
    return slot.tag == VT_INT ? (int)slot.as.i : 0;
}

static Db *db_of_handle(Value v) {
    int id = handle_id(v, HK_DB);
    return id ? db_by_id(id) : NULL;
}

static Db *need_db(Interp *it, Value v, const char *fn, const char *errfmt) {
    Db *db = db_of_handle(v);
    if (!db) {
        if (handle_id(v, HK_DB)) g_error(it, errfmt, "Cannot operate on a closed database.");
        g_error(it, ERR_DB_HANDLE, fn);
    }
    return db;
}

static int store_of_handle(Value v, Db **dbp, DbTable **tp) {
    Db *db = handle_id(v, HK_STORE) ? db_by_id(handle_id(v, HK_STORE)) : NULL;
    Value name;
    if (!db || !dict_get(v.as.d, v_str(str_from(HK_TAB)), &name) || name.tag != VT_STR) return 0;
    *dbp = db;
    *tp = tab_find(db, name.as.s->data, name.as.s->len);
    return *tp != NULL;
}

static int is_store(Value v) { return handle_id(v, HK_STORE) != 0; }

static void need_store(Interp *it, Value v, const char *fn, Db **dbp, DbTable **tp) {
    if (!store_of_handle(v, dbp, tp)) g_error(it, ERR_STORE_HANDLE, fn);
}

/* ---------------------------------------------------------------- sql subset
 * One statement per call, like sqlite3's Connection.execute.  Supported:
 *   CREATE TABLE [IF NOT EXISTS] t (col type [PRIMARY KEY [AUTOINCREMENT]], ...)
 *   DROP TABLE [IF EXISTS] t
 *   INSERT INTO t [(cols)] VALUES (...), (...)
 *   SELECT * | col [AS name], ... FROM t [WHERE e] [ORDER BY col [ASC|DESC], ...]
 *          [LIMIT n [OFFSET m]]
 *   UPDATE t SET col = e, ... [WHERE e]
 *   DELETE FROM t [WHERE e]
 * Expressions: literals, ?, column refs, + - * / %, comparisons, IS [NOT] NULL,
 * AND / OR / NOT, parentheses.  Table level constraints are parsed and ignored
 * (no UNIQUE / CHECK / FOREIGN KEY enforcement); anything else is refused.
 */
enum { TK_EOF, TK_ID, TK_STR, TK_INT, TK_REAL, TK_PARAM, TK_PUNCT };
enum { SX_LIT, SX_COL, SX_BIN, SX_UN };
enum { SO_ADD, SO_SUB, SO_MUL, SO_DIV, SO_MOD, SO_EQ, SO_NE, SO_LT, SO_GT, SO_LE, SO_GE,
       SO_AND, SO_OR, SO_NOT, SO_NEG, SO_ISNULL, SO_NOTNULL };

typedef struct SqlEx SqlEx;
struct SqlEx {
    int kind, op;
    Cell lit;                       /* SX_LIT */
    int col, aff;                   /* SX_COL: index (-1 rowid) + affinity */
    SqlEx *a, *b;
    SqlEx *chain;                   /* teardown list */
};

typedef struct {
    Db *db;
    const char *p;
    int tk;
    char *tok; uint32_t tokn;       /* TK_ID / TK_STR text */
    int64_t ival;
    double rval;
    char punct[4];
    Value *binds; int nbinds, usedbinds;
    SqlEx *chain;
    char **owned; int nowned, capowned;
    int mutated;
    char err[200];
} Sq;

static void sq_err(Sq *q, const char *fmt, ...) {
    if (q->err[0]) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(q->err, sizeof q->err, fmt, ap);
    va_end(ap);
}

static char *sq_own(Sq *q, char *s) {
    if (q->nowned == q->capowned) {
        q->capowned = q->capowned ? q->capowned * 2 : 8;
        q->owned = realloc(q->owned, sizeof(char *) * (size_t)q->capowned);
    }
    q->owned[q->nowned++] = s;
    return s;
}

static void sq_teardown(Sq *q) {
    for (SqlEx *e = q->chain, *nx; e; e = nx) { nx = e->chain; cell_clear(&e->lit); free(e); }
    for (int i = 0; i < q->nowned; i++) free(q->owned[i]);
    free(q->owned);
    free(q->tok);
}

static SqlEx *ex_new(Sq *q, int kind) {
    SqlEx *e = calloc(1, sizeof *e);
    e->kind = kind;
    e->chain = q->chain; q->chain = e;
    return e;
}

static int sq_id_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

static int sq_id_cont(unsigned char c) {
    return sq_id_start(c) || (c >= '0' && c <= '9') || c == '$';
}

static int sq_next(Sq *q) {
    free(q->tok); q->tok = NULL; q->tokn = 0;
    const char *p = q->p;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') p++;
        if (p[0] == '-' && p[1] == '-') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        break;
    }
    if (!*p) { q->p = p; q->tk = TK_EOF; return 1; }
    unsigned char c = (unsigned char)*p;
    if (sq_id_start(c)) {
        const char *s = p;
        while (sq_id_cont((unsigned char)*p)) p++;
        q->tokn = (uint32_t)(p - s);
        q->tok = malloc(q->tokn + 1);
        memcpy(q->tok, s, q->tokn); q->tok[q->tokn] = 0;
        q->p = p; q->tk = TK_ID;
        return 1;
    }
    if (c == '"' || c == '`' || c == '[') {              /* quoted identifier */
        char close = c == '[' ? ']' : (char)c;
        p++;
        GB b; gb_init(&b);
        while (*p && *p != close) gb_ch(&b, *p++);
        if (*p != close) { free(b.p); sq_err(q, "unrecognized token"); return 0; }
        p++;
        q->tok = b.p; q->tokn = (uint32_t)b.n;
        q->p = p; q->tk = TK_ID;
        return 1;
    }
    if (c == '\'') {
        p++;
        GB b; gb_init(&b);
        for (;;) {
            if (!*p) { free(b.p); sq_err(q, "unrecognized token"); return 0; }
            if (*p == '\'') {
                if (p[1] == '\'') { gb_ch(&b, '\''); p += 2; continue; }
                p++;
                break;
            }
            gb_ch(&b, *p++);
        }
        q->tok = b.p; q->tokn = (uint32_t)b.n;
        q->p = p; q->tk = TK_STR;
        return 1;
    }
    if ((c >= '0' && c <= '9') || (c == '.' && p[1] >= '0' && p[1] <= '9')) {
        const char *s = p;
        int isreal = 0;
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') { isreal = 1; p++; while (*p >= '0' && *p <= '9') p++; }
        if (*p == 'e' || *p == 'E') {
            isreal = 1; p++;
            if (*p == '+' || *p == '-') p++;
            while (*p >= '0' && *p <= '9') p++;
        }
        char buf[64];
        size_t n = (size_t)(p - s);
        if (n >= sizeof buf) { sq_err(q, "unrecognized token"); return 0; }
        memcpy(buf, s, n); buf[n] = 0;
        if (isreal) { q->rval = strtod(buf, NULL); q->tk = TK_REAL; }
        else {
            errno = 0;
            q->ival = (int64_t)strtoll(buf, NULL, 10);
            if (errno == ERANGE) { q->rval = strtod(buf, NULL); q->tk = TK_REAL; }
            else q->tk = TK_INT;
        }
        q->p = p;
        return 1;
    }
    if (c == '?') { q->p = p + 1; q->tk = TK_PARAM; return 1; }
    static const char *const OPS[] = { "<=", ">=", "<>", "!=", "==", NULL };
    for (int i = 0; OPS[i]; i++)
        if (!strncmp(p, OPS[i], 2)) {
            memcpy(q->punct, OPS[i], 3);
            q->p = p + 2; q->tk = TK_PUNCT;
            return 1;
        }
    if (strchr("()*,;=<>+-/%.", (int)c)) {
        q->punct[0] = (char)c; q->punct[1] = 0;
        q->p = p + 1; q->tk = TK_PUNCT;
        return 1;
    }
    sq_err(q, "unrecognized token: \"%c\"", (int)c);
    return 0;
}

static int sq_is_kw(Sq *q, const char *kw) {
    return q->tk == TK_ID && name_eqz(q->tok, q->tokn, kw);
}

static int sq_eat_kw(Sq *q, const char *kw) {
    return sq_is_kw(q, kw) ? sq_next(q) : 0;
}

static int sq_is_punct(Sq *q, const char *s) {
    return q->tk == TK_PUNCT && !strcmp(q->punct, s);
}

static int sq_eat_punct(Sq *q, const char *s) {
    return sq_is_punct(q, s) ? sq_next(q) : 0;
}

static const char *sq_tokdesc(Sq *q) {
    static char buf[64];
    if (q->tk == TK_EOF) return "end of input";
    if (q->tk == TK_PUNCT) { snprintf(buf, sizeof buf, "\"%s\"", q->punct); return buf; }
    if (q->tk == TK_PARAM) return "\"?\"";
    if (q->tok) { snprintf(buf, sizeof buf, "\"%.40s\"", q->tok); return buf; }
    return "a number";
}

static int sq_syntax(Sq *q) {
    sq_err(q, "near %s: syntax error", sq_tokdesc(q));
    return 0;
}

static int sq_need_punct(Sq *q, const char *s) {
    return sq_eat_punct(q, s) ? 1 : sq_syntax(q);
}

/* identifier: the name is owned by the context and outlives the statement */
static char *sq_name(Sq *q, uint32_t *n) {
    if (q->tk != TK_ID) { sq_syntax(q); return NULL; }
    char *s = sq_own(q, q->tok);
    *n = q->tokn;
    q->tok = NULL; q->tokn = 0;
    return sq_next(q) ? s : NULL;
}

/* ---- expressions ---- */
static SqlEx *sq_or_ex(Sq *q, DbTable *t);

static SqlEx *sq_primary(Sq *q, DbTable *t) {
    if (q->tk == TK_INT) {
        SqlEx *e = ex_new(q, SX_LIT);
        e->lit = cell_int(q->ival);
        return sq_next(q) ? e : NULL;
    }
    if (q->tk == TK_REAL) {
        SqlEx *e = ex_new(q, SX_LIT);
        e->lit = cell_real(q->rval);
        return sq_next(q) ? e : NULL;
    }
    if (q->tk == TK_STR) {
        SqlEx *e = ex_new(q, SX_LIT);
        e->lit = cell_text(q->tok, q->tokn);
        return sq_next(q) ? e : NULL;
    }
    if (q->tk == TK_PARAM) {
        SqlEx *e = ex_new(q, SX_LIT);
        int i = q->usedbinds++;
        if (i < q->nbinds) {
            Value v = q->binds[i];
            if (v.tag == VT_LIST || v.tag == VT_DICT || v.tag == VT_FUNC) {
                sq_err(q, "Error binding parameter %d - probably unsupported type.", i);
                return NULL;
            }
            e->lit = cell_from_value(v);
        }
        return sq_next(q) ? e : NULL;
    }
    if (sq_is_punct(q, "(")) {
        if (!sq_next(q)) return NULL;
        SqlEx *e = sq_or_ex(q, t);
        if (!e || !sq_need_punct(q, ")")) return NULL;
        return e;
    }
    if (q->tk == TK_ID) {
        if (sq_is_kw(q, "null")) {
            SqlEx *e = ex_new(q, SX_LIT);
            e->lit = cell_null();
            return sq_next(q) ? e : NULL;
        }
        uint32_t n;
        char *name = sq_name(q, &n);
        if (!name) return NULL;
        if (sq_is_punct(q, "(")) {
            sq_err(q, "unsupported SQL in the C implementation: no SQL functions (%s)", name);
            return NULL;
        }
        if (sq_is_punct(q, ".")) {                      /* table.column */
            if (!t || !name_eqz(name, n, t->name)) { sq_err(q, "no such column: %s", name); return NULL; }
            if (!sq_next(q)) return NULL;
            name = sq_name(q, &n);
            if (!name) return NULL;
        }
        if (!t) { sq_err(q, "no such column: %s", name); return NULL; }
        SqlEx *e = ex_new(q, SX_COL);
        e->col = tab_resolve_col(t, name, n);
        if (e->col == -2) { sq_err(q, "no such column: %s", name); return NULL; }
        e->aff = e->col < 0 ? AFF_INT : t->cols[e->col].aff;
        return e;
    }
    return sq_syntax(q), NULL;
}

static SqlEx *sq_unary(Sq *q, DbTable *t) {
    if (sq_is_punct(q, "-") || sq_is_punct(q, "+")) {
        int neg = sq_is_punct(q, "-");
        if (!sq_next(q)) return NULL;
        SqlEx *a = sq_unary(q, t);
        if (!a || !neg) return a;
        SqlEx *e = ex_new(q, SX_UN);
        e->op = SO_NEG; e->a = a;
        return e;
    }
    return sq_primary(q, t);
}

static SqlEx *sq_mul(Sq *q, DbTable *t) {
    SqlEx *a = sq_unary(q, t);
    while (a && (sq_is_punct(q, "*") || sq_is_punct(q, "/") || sq_is_punct(q, "%"))) {
        int op = sq_is_punct(q, "*") ? SO_MUL : sq_is_punct(q, "/") ? SO_DIV : SO_MOD;
        if (!sq_next(q)) return NULL;
        SqlEx *b = sq_unary(q, t);
        if (!b) return NULL;
        SqlEx *e = ex_new(q, SX_BIN);
        e->op = op; e->a = a; e->b = b;
        a = e;
    }
    return a;
}

static SqlEx *sq_add_ex(Sq *q, DbTable *t) {
    SqlEx *a = sq_mul(q, t);
    while (a && (sq_is_punct(q, "+") || sq_is_punct(q, "-"))) {
        int op = sq_is_punct(q, "+") ? SO_ADD : SO_SUB;
        if (!sq_next(q)) return NULL;
        SqlEx *b = sq_mul(q, t);
        if (!b) return NULL;
        SqlEx *e = ex_new(q, SX_BIN);
        e->op = op; e->a = a; e->b = b;
        a = e;
    }
    return a;
}

static SqlEx *sq_cmp(Sq *q, DbTable *t) {
    SqlEx *a = sq_add_ex(q, t);
    for (;;) {
        if (!a) return NULL;
        int op;
        if (sq_is_punct(q, "=") || sq_is_punct(q, "==")) op = SO_EQ;
        else if (sq_is_punct(q, "!=") || sq_is_punct(q, "<>")) op = SO_NE;
        else if (sq_is_punct(q, "<")) op = SO_LT;
        else if (sq_is_punct(q, ">")) op = SO_GT;
        else if (sq_is_punct(q, "<=")) op = SO_LE;
        else if (sq_is_punct(q, ">=")) op = SO_GE;
        else if (sq_is_kw(q, "is")) {
            if (!sq_next(q)) return NULL;
            int notnull = sq_is_kw(q, "not");
            if (notnull && !sq_next(q)) return NULL;
            if (!sq_is_kw(q, "null")) { sq_syntax(q); return NULL; }
            if (!sq_next(q)) return NULL;
            SqlEx *e = ex_new(q, SX_UN);
            e->op = notnull ? SO_NOTNULL : SO_ISNULL;
            e->a = a;
            a = e;
            continue;
        } else break;
        if (!sq_next(q)) return NULL;
        SqlEx *b = sq_add_ex(q, t);
        if (!b) return NULL;
        SqlEx *e = ex_new(q, SX_BIN);
        e->op = op; e->a = a; e->b = b;
        a = e;
    }
    return a;
}

static SqlEx *sq_not_ex(Sq *q, DbTable *t) {
    if (sq_is_kw(q, "not")) {
        if (!sq_next(q)) return NULL;
        SqlEx *a = sq_not_ex(q, t);
        if (!a) return NULL;
        SqlEx *e = ex_new(q, SX_UN);
        e->op = SO_NOT; e->a = a;
        return e;
    }
    return sq_cmp(q, t);
}

static SqlEx *sq_and_ex(Sq *q, DbTable *t) {
    SqlEx *a = sq_not_ex(q, t);
    while (a && sq_is_kw(q, "and")) {
        if (!sq_next(q)) return NULL;
        SqlEx *b = sq_not_ex(q, t);
        if (!b) return NULL;
        SqlEx *e = ex_new(q, SX_BIN);
        e->op = SO_AND; e->a = a; e->b = b;
        a = e;
    }
    return a;
}

static SqlEx *sq_or_ex(Sq *q, DbTable *t) {
    SqlEx *a = sq_and_ex(q, t);
    while (a && sq_is_kw(q, "or")) {
        if (!sq_next(q)) return NULL;
        SqlEx *b = sq_and_ex(q, t);
        if (!b) return NULL;
        SqlEx *e = ex_new(q, SX_BIN);
        e->op = SO_OR; e->a = a; e->b = b;
        a = e;
    }
    return a;
}

/* ---- evaluation ---- */
static Cell row_cell(DbRow *r, int col) {
    return col < 0 ? cell_int(r->rowid) : cell_dup(&r->cells[col]);
}

static void cell_num(const Cell *c, int64_t *ip, double *fp, int *isint) {
    if (c->kind == CELL_INT) { *ip = c->i; *isint = 1; return; }
    if (c->kind == CELL_REAL) { *fp = c->f; *isint = 0; return; }
    if (c->kind == CELL_TEXT && text_as_num(c->s, c->slen, ip, fp, isint)) return;
    *ip = 0; *isint = 1;                                /* sqlite: other text is 0 */
}

static Cell ex_eval(SqlEx *e, DbRow *r) {
    if (e->kind == SX_LIT) return cell_dup(&e->lit);
    if (e->kind == SX_COL) return row_cell(r, e->col);
    if (e->kind == SX_UN) {
        Cell a = ex_eval(e->a, r);
        Cell out = cell_null();
        switch (e->op) {
        case SO_NEG:
            if (a.kind != CELL_NULL) {
                int64_t i = 0; double f = 0; int isint = 0;
                cell_num(&a, &i, &f, &isint);
                out = isint ? cell_int(-i) : cell_real(-f);
            }
            break;
        case SO_NOT: if (a.kind != CELL_NULL) out = cell_int(!cell_truth(&a)); break;
        case SO_ISNULL: out = cell_int(a.kind == CELL_NULL); break;
        default: out = cell_int(a.kind != CELL_NULL);
        }
        cell_clear(&a);
        return out;
    }
    if (e->op == SO_AND || e->op == SO_OR) {             /* three valued logic */
        Cell a = ex_eval(e->a, r);
        int at = a.kind == CELL_NULL ? -1 : cell_truth(&a);
        cell_clear(&a);
        if (e->op == SO_AND && at == 0) return cell_int(0);
        if (e->op == SO_OR && at == 1) return cell_int(1);
        Cell b = ex_eval(e->b, r);
        int bt = b.kind == CELL_NULL ? -1 : cell_truth(&b);
        cell_clear(&b);
        if (e->op == SO_AND) {
            if (bt == 0) return cell_int(0);
            return (at < 0 || bt < 0) ? cell_null() : cell_int(1);
        }
        if (bt == 1) return cell_int(1);
        return (at < 0 || bt < 0) ? cell_null() : cell_int(0);
    }
    Cell a = ex_eval(e->a, r), b = ex_eval(e->b, r);
    Cell out = cell_null();
    if (e->op >= SO_EQ && e->op <= SO_GE) {
        if (a.kind != CELL_NULL && b.kind != CELL_NULL) {
            int aff = e->a->kind == SX_COL ? e->a->aff
                    : (e->b->kind == SX_COL ? e->b->aff : AFF_NONE);
            if (aff != AFF_NONE) { cell_coerce(&a, aff); cell_coerce(&b, aff); }
            int c = cell_cmp(&a, &b);
            out = cell_int(e->op == SO_EQ ? c == 0 : e->op == SO_NE ? c != 0
                         : e->op == SO_LT ? c < 0 : e->op == SO_GT ? c > 0
                         : e->op == SO_LE ? c <= 0 : c >= 0);
        }
    } else if (a.kind != CELL_NULL && b.kind != CELL_NULL) {
        int64_t ai = 0, bi = 0; double af = 0, bf = 0; int aint = 0, bint = 0;
        cell_num(&a, &ai, &af, &aint);
        cell_num(&b, &bi, &bf, &bint);
        int divzero = (e->op == SO_DIV || e->op == SO_MOD) && (bint ? bi == 0 : bf == 0);
        if (!divzero) {
            if (aint && bint) {
                switch (e->op) {
                case SO_ADD: out = cell_int(ai + bi); break;
                case SO_SUB: out = cell_int(ai - bi); break;
                case SO_MUL: out = cell_int(ai * bi); break;
                case SO_DIV: out = cell_int(ai / bi); break;   /* integer division */
                default: out = cell_int(ai % bi);
                }
            } else {
                double x = aint ? (double)ai : af, y = bint ? (double)bi : bf;
                switch (e->op) {
                case SO_ADD: out = cell_real(x + y); break;
                case SO_SUB: out = cell_real(x - y); break;
                case SO_MUL: out = cell_real(x * y); break;
                case SO_DIV: out = cell_real(x / y); break;
                default: out = cell_int((int64_t)x % (int64_t)y);
                }
            }
        }
    }
    cell_clear(&a); cell_clear(&b);
    return out;
}

static int row_matches(SqlEx *where, DbRow *r) {
    if (!where) return 1;
    Cell c = ex_eval(where, r);
    int ok = cell_truth(&c);
    cell_clear(&c);
    return ok;
}

/* ---- ORDER BY ---- */
typedef struct { int col, desc; } SortKey;
static struct { SortKey *keys; int nkeys; } g_sortctx;

static int row_order_cmp(const void *pa, const void *pb) {
    DbRow *ra = *(DbRow *const *)pa, *rb = *(DbRow *const *)pb;
    for (int k = 0; k < g_sortctx.nkeys; k++) {
        Cell a = row_cell(ra, g_sortctx.keys[k].col);
        Cell b = row_cell(rb, g_sortctx.keys[k].col);
        int c = cell_cmp(&a, &b);
        cell_clear(&a); cell_clear(&b);
        if (c) return g_sortctx.keys[k].desc ? -c : c;
    }
    return ra->rowid == rb->rowid ? 0 : (ra->rowid < rb->rowid ? -1 : 1);
}

/* ---- statements ---- */
static int sq_type_affinity(const char *type) {
    char low[64];
    size_t n = strlen(type);
    if (n >= sizeof low) n = sizeof low - 1;
    for (size_t i = 0; i < n; i++) low[i] = (char)ascii_lower((unsigned char)type[i]);
    low[n] = 0;
    if (!n) return AFF_NONE;
    if (strstr(low, "int")) return AFF_INT;
    if (strstr(low, "char") || strstr(low, "clob") || strstr(low, "text")) return AFF_TEXT;
    if (strstr(low, "blob")) return AFF_NONE;
    if (strstr(low, "real") || strstr(low, "floa") || strstr(low, "doub")) return AFF_REAL;
    return AFF_NUM;
}

static int sq_col_constraint_kw(Sq *q) {
    static const char *const K[] = { "primary", "not", "null", "unique", "check", "default",
                                     "collate", "references", "generated", "as",
                                     "constraint", "autoincrement", NULL };
    for (int i = 0; K[i]; i++) if (sq_is_kw(q, K[i])) return 1;
    return 0;
}

/* skip to the ',' or ')' that ends the current item */
static int sq_skip_item(Sq *q, int *pk, int *autoinc) {
    int depth = 0;
    while (q->tk != TK_EOF) {
        if (sq_is_punct(q, "(")) depth++;
        else if (sq_is_punct(q, ")")) {
            if (!depth) return 1;
            depth--;
        } else if (sq_is_punct(q, ",") && !depth) return 1;
        else if (!depth && q->tk == TK_ID) {
            if (pk && sq_is_kw(q, "primary")) *pk = 1;
            if (autoinc && sq_is_kw(q, "autoincrement")) *autoinc = 1;
        }
        if (!sq_next(q)) return 0;
    }
    return 1;
}

static int sq_create(Sq *q) {
    if (!sq_eat_kw(q, "table")) return sq_syntax(q);
    int ifnot = 0;
    if (sq_is_kw(q, "if")) {
        if (!sq_next(q) || !sq_eat_kw(q, "not") || !sq_eat_kw(q, "exists")) return sq_syntax(q);
        ifnot = 1;
    }
    uint32_t nn;
    char *name = sq_name(q, &nn);
    if (!name) return 0;
    if (tab_find(q->db, name, nn)) {
        if (ifnot) return 1;
        sq_err(q, "table %s already exists", name);
        return 0;
    }
    if (!sq_need_punct(q, "(")) return 0;
    DbTable *t = tab_alloc(name, nn);
    int ok = 0;
    for (;;) {
        if (sq_is_kw(q, "constraint") || sq_is_kw(q, "primary") || sq_is_kw(q, "unique")
            || sq_is_kw(q, "check") || sq_is_kw(q, "foreign")) {
            if (!sq_skip_item(q, NULL, NULL)) goto done;        /* table constraint: ignored */
        } else {
            uint32_t cn;
            char *cname = sq_name(q, &cn);
            if (!cname) goto done;
            GB type; gb_init(&type);
            while (q->tk == TK_ID && !sq_col_constraint_kw(q)) {
                gb_put(&type, q->tok, q->tokn);
                if (!sq_next(q)) { free(type.p); goto done; }
            }
            if (sq_is_punct(q, "(")) {                          /* VARCHAR(20) */
                int depth = 0;
                do {
                    if (sq_is_punct(q, "(")) depth++;
                    else if (sq_is_punct(q, ")")) depth--;
                    if (!sq_next(q)) { free(type.p); goto done; }
                } while (depth && q->tk != TK_EOF);
            }
            int pk = 0, autoinc = 0;
            if (!sq_skip_item(q, &pk, &autoinc)) { free(type.p); goto done; }
            int alias = pk && name_eqz(type.p, (uint32_t)type.n, "integer");
            tab_add_col(t, cname, cn, sq_type_affinity(type.p), alias);
            free(type.p);
            if (alias && autoinc) t->autoinc = 1;
        }
        if (sq_eat_punct(q, ",")) continue;
        break;
    }
    if (!sq_need_punct(q, ")")) goto done;
    db_attach(q->db, t);
    t = NULL;
    q->mutated = 1;
    ok = 1;
done:
    if (t) tab_free(t);
    return ok;
}

static int sq_drop(Sq *q) {
    if (!sq_eat_kw(q, "table")) return sq_syntax(q);
    int ifex = 0;
    if (sq_is_kw(q, "if")) {
        if (!sq_next(q) || !sq_eat_kw(q, "exists")) return sq_syntax(q);
        ifex = 1;
    }
    uint32_t nn;
    char *name = sq_name(q, &nn);
    if (!name) return 0;
    DbTable *t = tab_find(q->db, name, nn);
    if (!t) {
        if (ifex) return 1;
        sq_err(q, "no such table: %s", name);
        return 0;
    }
    for (int i = 0; i < q->db->ntabs; i++)
        if (q->db->tabs[i] == t) {
            memmove(&q->db->tabs[i], &q->db->tabs[i + 1],
                    sizeof(DbTable *) * (size_t)(q->db->ntabs - i - 1));
            q->db->ntabs--;
            break;
        }
    tab_free(t);
    q->mutated = 1;
    return 1;
}

static int sq_insert(Sq *q, int *changes) {
    if (!sq_eat_kw(q, "into")) return sq_syntax(q);
    uint32_t nn;
    char *name = sq_name(q, &nn);
    if (!name) return 0;
    DbTable *t = tab_find(q->db, name, nn);
    if (!t) { sq_err(q, "no such table: %s", name); return 0; }
    int *cols = NULL, ncols = 0, ok = 0;
    if (sq_is_punct(q, "(")) {
        if (!sq_next(q)) goto done;
        for (;;) {
            uint32_t cn;
            char *cname = sq_name(q, &cn);
            if (!cname) goto done;
            int ci = tab_col_index(t, cname, cn);
            if (ci < 0) { sq_err(q, "table %s has no column named %s", t->name, cname); goto done; }
            cols = realloc(cols, sizeof(int) * (size_t)(ncols + 1));
            cols[ncols++] = ci;
            if (sq_eat_punct(q, ",")) continue;
            break;
        }
        if (!sq_need_punct(q, ")")) goto done;
    } else {
        ncols = t->ncols;
        cols = malloc(sizeof(int) * (size_t)(ncols ? ncols : 1));
        for (int i = 0; i < ncols; i++) cols[i] = i;
    }
    if (!sq_eat_kw(q, "values")) {
        sq_err(q, "unsupported SQL in the C implementation: INSERT needs VALUES");
        goto done;
    }
    int rid_col = tab_rowid_col(t);
    for (;;) {
        if (!sq_need_punct(q, "(")) goto done;
        Cell *vals = calloc((size_t)(t->ncols ? t->ncols : 1), sizeof(Cell));
        int nv = 0, bad = 0;
        for (;;) {
            SqlEx *e = sq_or_ex(q, NULL);
            if (!e) { bad = 1; break; }
            if (nv >= ncols) {
                sq_err(q, "table %s has %d columns but %d values were supplied",
                       t->name, ncols, nv + 1);
                bad = 1;
                break;
            }
            vals[cols[nv]] = ex_eval(e, NULL);
            nv++;
            if (sq_eat_punct(q, ",")) continue;
            break;
        }
        if (!bad && nv != ncols) {
            sq_err(q, "table %s has %d columns but %d values were supplied", t->name, ncols, nv);
            bad = 1;
        }
        if (!bad && !sq_need_punct(q, ")")) bad = 1;
        int64_t rowid = -1;
        if (!bad && rid_col >= 0 && vals[rid_col].kind != CELL_NULL) {
            Cell c = cell_dup(&vals[rid_col]);
            cell_coerce(&c, AFF_INT);
            if (c.kind != CELL_INT) { sq_err(q, "datatype mismatch"); bad = 1; }
            else if (tab_find_rowid(t, c.i) >= 0) {
                sq_err(q, "UNIQUE constraint failed: %s.%s", t->name, t->cols[rid_col].name);
                bad = 1;
            } else rowid = c.i;
            cell_clear(&c);
        }
        if (bad) {
            for (int i = 0; i < t->ncols; i++) cell_clear(&vals[i]);
            free(vals);
            goto done;
        }
        if (rowid < 0) rowid = tab_next_rowid(t);
        DbRow *row = tab_insert(t, rowid);
        for (int i = 0; i < t->ncols; i++) {
            row->cells[i] = vals[i];
            cell_coerce(&row->cells[i], t->cols[i].aff);
        }
        if (rid_col >= 0) {
            cell_clear(&row->cells[rid_col]);
            row->cells[rid_col] = cell_int(rowid);
        }
        free(vals);
        (*changes)++;
        q->mutated = 1;
        if (sq_eat_punct(q, ",")) continue;
        break;
    }
    ok = 1;
done:
    free(cols);
    return ok;
}

static int sq_select(Sq *q, List *out) {
    char **raw = NULL, **alias = NULL;
    uint32_t *rawn = NULL;
    int nraw = 0, star = 0, ok = 0;
    int *cols = NULL;
    const char **names = NULL;
    DbRow **order = NULL;
    if (sq_is_kw(q, "distinct") || sq_is_kw(q, "all")) {
        sq_err(q, "unsupported SQL in the C implementation: near %s", sq_tokdesc(q));
        return 0;
    }
    if (sq_eat_punct(q, "*")) star = 1;
    else
        for (;;) {
            if (q->tk != TK_ID) {
                sq_err(q, "unsupported SQL in the C implementation: only column names in SELECT");
                goto done;
            }
            uint32_t n1;
            char *nm = sq_name(q, &n1);
            if (!nm) goto done;
            if (sq_is_punct(q, "(")) {
                sq_err(q, "unsupported SQL in the C implementation: no SQL functions (%s)", nm);
                goto done;
            }
            if (sq_is_punct(q, ".")) {
                if (!sq_next(q)) goto done;
                nm = sq_name(q, &n1);
                if (!nm) goto done;
            }
            char *al = NULL;
            if (sq_is_kw(q, "as")) {
                uint32_t n2;
                if (!sq_next(q)) goto done;
                al = sq_name(q, &n2);
                if (!al) goto done;
            }
            raw = realloc(raw, sizeof(char *) * (size_t)(nraw + 1));
            rawn = realloc(rawn, sizeof(uint32_t) * (size_t)(nraw + 1));
            alias = realloc(alias, sizeof(char *) * (size_t)(nraw + 1));
            raw[nraw] = nm; rawn[nraw] = n1; alias[nraw] = al;
            nraw++;
            if (sq_eat_punct(q, ",")) continue;
            break;
        }
    if (!sq_eat_kw(q, "from")) { sq_syntax(q); goto done; }
    uint32_t tn;
    char *tname = sq_name(q, &tn);
    if (!tname) goto done;
    DbTable *t = tab_find(q->db, tname, tn);
    if (!t) { sq_err(q, "no such table: %s", tname); goto done; }
    int ncols = star ? t->ncols : nraw;
    cols = malloc(sizeof(int) * (size_t)(ncols ? ncols : 1));
    names = malloc(sizeof(char *) * (size_t)(ncols ? ncols : 1));
    if (star)
        for (int i = 0; i < ncols; i++) { cols[i] = i; names[i] = t->cols[i].name; }
    else
        for (int i = 0; i < ncols; i++) {
            cols[i] = tab_resolve_col(t, raw[i], rawn[i]);
            if (cols[i] == -2) { sq_err(q, "no such column: %s", raw[i]); goto done; }
            names[i] = alias[i] ? alias[i] : (cols[i] < 0 ? "rowid" : t->cols[cols[i]].name);
        }
    SqlEx *where = NULL;
    if (sq_is_kw(q, "where")) {
        if (!sq_next(q)) goto done;
        where = sq_or_ex(q, t);
        if (!where) goto done;
    }
    SortKey keys[8];
    int nkeys = 0;
    if (sq_is_kw(q, "order")) {
        if (!sq_next(q) || !sq_eat_kw(q, "by")) { sq_syntax(q); goto done; }
        for (;;) {
            SqlEx *e = sq_primary(q, t);
            if (!e) goto done;
            if (e->kind != SX_COL) {
                sq_err(q, "unsupported SQL in the C implementation: only column names in ORDER BY");
                goto done;
            }
            if (nkeys == 8) { sq_err(q, "too many ORDER BY terms"); goto done; }
            keys[nkeys].col = e->col;
            keys[nkeys].desc = 0;
            if (sq_is_kw(q, "desc")) { keys[nkeys].desc = 1; if (!sq_next(q)) goto done; }
            else if (sq_is_kw(q, "asc") && !sq_next(q)) goto done;
            nkeys++;
            if (sq_eat_punct(q, ",")) continue;
            break;
        }
    }
    int64_t limit = -1, offset = 0;
    if (sq_is_kw(q, "limit")) {
        if (!sq_next(q)) goto done;
        SqlEx *e = sq_or_ex(q, NULL);
        if (!e) goto done;
        Cell c = ex_eval(e, NULL);
        int64_t iv = 0; double fv = 0; int isint = 0;
        cell_num(&c, &iv, &fv, &isint);
        limit = isint ? iv : (int64_t)fv;
        cell_clear(&c);
        if (sq_is_kw(q, "offset")) {
            if (!sq_next(q)) goto done;
            SqlEx *o = sq_or_ex(q, NULL);
            if (!o) goto done;
            Cell oc = ex_eval(o, NULL);
            cell_num(&oc, &iv, &fv, &isint);
            offset = isint ? iv : (int64_t)fv;
            cell_clear(&oc);
        }
    }
    long nmatch = 0;
    order = malloc(sizeof(DbRow *) * (size_t)(t->nrows ? t->nrows : 1));
    for (long r = 0; r < t->nrows; r++)
        if (row_matches(where, &t->rows[r])) order[nmatch++] = &t->rows[r];
    if (nkeys) {
        g_sortctx.keys = keys; g_sortctx.nkeys = nkeys;
        qsort(order, (size_t)nmatch, sizeof(DbRow *), row_order_cmp);
        g_sortctx.keys = NULL; g_sortctx.nkeys = 0;
    }
    for (long i = offset > 0 ? offset : 0; i < nmatch; i++) {
        if (limit >= 0 && i - (offset > 0 ? offset : 0) >= limit) break;
        Dict *d = dict_new();
        for (int c = 0; c < ncols; c++) {
            Cell v = row_cell(order[i], cols[c]);
            dict_set(d, v_str(str_from(names[c])), cell_to_value(&v));
            cell_clear(&v);
        }
        list_push(out, v_dict(d));
    }
    ok = 1;
done:
    free(order);
    free(cols);
    free((void *)names);
    free(raw); free(rawn); free(alias);
    return ok;
}

static int sq_update(Sq *q, int *changes) {
    uint32_t nn;
    char *name = sq_name(q, &nn);
    if (!name) return 0;
    DbTable *t = tab_find(q->db, name, nn);
    if (!t) { sq_err(q, "no such table: %s", name); return 0; }
    if (!sq_eat_kw(q, "set")) return sq_syntax(q);
    int *cols = NULL, nset = 0, ok = 0;
    SqlEx **vals = NULL;
    for (;;) {
        uint32_t cn;
        char *cname = sq_name(q, &cn);
        if (!cname) goto done;
        int ci = tab_col_index(t, cname, cn);
        if (ci < 0) { sq_err(q, "no such column: %s", cname); goto done; }
        if (t->cols[ci].rowid_alias) {
            sq_err(q, "unsupported SQL in the C implementation: cannot UPDATE the key column %s",
                   cname);
            goto done;
        }
        if (!sq_need_punct(q, "=")) goto done;
        SqlEx *e = sq_or_ex(q, t);
        if (!e) goto done;
        cols = realloc(cols, sizeof(int) * (size_t)(nset + 1));
        vals = realloc(vals, sizeof(SqlEx *) * (size_t)(nset + 1));
        cols[nset] = ci; vals[nset] = e;
        nset++;
        if (sq_eat_punct(q, ",")) continue;
        break;
    }
    SqlEx *where = NULL;
    if (sq_is_kw(q, "where")) {
        if (!sq_next(q)) goto done;
        where = sq_or_ex(q, t);
        if (!where) goto done;
    }
    for (long r = 0; r < t->nrows; r++) {
        if (!row_matches(where, &t->rows[r])) continue;
        Cell *fresh = malloc(sizeof(Cell) * (size_t)nset);
        for (int i = 0; i < nset; i++) fresh[i] = ex_eval(vals[i], &t->rows[r]);
        for (int i = 0; i < nset; i++) {
            cell_clear(&t->rows[r].cells[cols[i]]);
            t->rows[r].cells[cols[i]] = fresh[i];
            cell_coerce(&t->rows[r].cells[cols[i]], t->cols[cols[i]].aff);
        }
        free(fresh);
        (*changes)++;
        q->mutated = 1;
    }
    ok = 1;
done:
    free(cols);
    free(vals);
    return ok;
}

static int sq_delete(Sq *q, int *changes) {
    if (!sq_eat_kw(q, "from")) return sq_syntax(q);
    uint32_t nn;
    char *name = sq_name(q, &nn);
    if (!name) return 0;
    DbTable *t = tab_find(q->db, name, nn);
    if (!t) { sq_err(q, "no such table: %s", name); return 0; }
    SqlEx *where = NULL;
    if (sq_is_kw(q, "where")) {
        if (!sq_next(q)) return 0;
        where = sq_or_ex(q, t);
        if (!where) return 0;
    }
    for (long r = t->nrows - 1; r >= 0; r--)
        if (row_matches(where, &t->rows[r])) {
            tab_remove(t, r);
            (*changes)++;
            q->mutated = 1;
        }
    return 1;
}

/* one statement; 0 with q->err set on failure.  never throws. */
static int sql_run(Sq *q, const char *sql, int *changes, List *out) {
    q->p = sql;
    if (!sq_next(q)) return 0;
    if (q->tk == TK_EOF) return 1;
    int ok;
    if (sq_is_kw(q, "select")) {
        if (!sq_next(q)) return 0;
        ok = sq_select(q, out);
        *changes = -1;
    } else if (sq_is_kw(q, "insert")) {
        if (!sq_next(q)) return 0;
        ok = sq_insert(q, changes);
    } else if (sq_is_kw(q, "update")) {
        if (!sq_next(q)) return 0;
        ok = sq_update(q, changes);
    } else if (sq_is_kw(q, "delete")) {
        if (!sq_next(q)) return 0;
        ok = sq_delete(q, changes);
    } else if (sq_is_kw(q, "create")) {
        if (!sq_next(q)) return 0;
        ok = sq_create(q);
        *changes = -1;
    } else if (sq_is_kw(q, "drop")) {
        if (!sq_next(q)) return 0;
        ok = sq_drop(q);
        *changes = -1;
    } else if (sq_is_kw(q, "begin") || sq_is_kw(q, "commit") || sq_is_kw(q, "rollback")) {
        sq_err(q, "unsupported SQL in the C implementation: use %s() for transactions",
               BUILTIN_NAMES[B_GEORAE]);
        return 0;
    } else {
        return sq_syntax(q);
    }
    if (!ok) return 0;
    sq_eat_punct(q, ";");
    if (q->tk != TK_EOF) return sq_syntax(q);
    return 1;
}

/* count ? and spot a second statement before anything runs, as sqlite3 does */
static void sql_prescan(const char *sql, int *nparams, int *multi) {
    Sq c;
    memset(&c, 0, sizeof c);
    c.p = sql;
    *nparams = 0; *multi = 0;
    int semi = 0;
    while (sq_next(&c) && c.tk != TK_EOF) {
        if (c.tk == TK_PARAM) (*nparams)++;
        if (semi) { *multi = 1; break; }
        if (c.tk == TK_PUNCT && !strcmp(c.punct, ";")) semi = 1;
    }
    free(c.tok);
}

/* shared by 실행 and 질의 */
static int db_statement(Interp *it, Value *args, int n, const char *fn, const char *errfmt,
                        List *out) {
    Db *db = need_db(it, need_arg(it, args, n, 0, fn), fn, errfmt);
    Str *sql = v_stringify(need_arg(it, args, n, 1, fn));
    Value bindv = n > 2 ? args[2] : v_nil();
    if (n > 2 && bindv.tag != VT_LIST) g_error(it, ERR_DB_BINDLIST, fn);
    int nbinds = bindv.tag == VT_LIST ? (int)bindv.as.l->n : 0;
    int nparams = 0, multi = 0;
    sql_prescan(sql->data, &nparams, &multi);
    if (multi) g_error(it, errfmt, "You can only execute one statement at a time.");
    if (nparams != nbinds) {
        char m[160];
        snprintf(m, sizeof m, "Incorrect number of bindings supplied. The current statement uses "
                              "%d, and there are %d supplied.", nparams, nbinds);
        g_error(it, errfmt, m);
    }
    Sq q;
    memset(&q, 0, sizeof q);
    q.db = db;
    if (bindv.tag == VT_LIST) q.binds = bindv.as.l->items;
    q.nbinds = nbinds;
    int changes = 0;
    int ok = sql_run(&q, sql->data, &changes, out);
    int mutated = q.mutated;
    char msg[sizeof q.err];
    memcpy(msg, q.err, sizeof msg);
    sq_teardown(&q);
    if (!ok) g_error(it, errfmt, msg);
    if (mutated) db_save(it, db);
    return changes < 0 ? 0 : changes;
}

static Value b_db_open(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JARYOYEOLGI];
    Str *path = v_stringify(need_arg(it, args, n, 0, fn));
    Db *db = db_open(it, path->data);
    Dict *d = dict_new();
    dict_set(d, v_str(str_from(HK_DB)), v_int(db->id));
    return v_dict(d);
}

static Value b_db_exec(Interp *it, Value *args, int n) {
    return v_int(db_statement(it, args, n, BUILTIN_NAMES[B_SILHAENG], ERR_DB_EXEC, NULL));
}

static Value b_db_query(Interp *it, Value *args, int n) {
    List *out = list_new(8);
    db_statement(it, args, n, BUILTIN_NAMES[B_JILUI], ERR_DB_QUERY, out);
    return v_list(out);
}

static Value b_db_close(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JARYODADGI];
    Value h = need_arg(it, args, n, 0, fn);
    Db *db = db_of_handle(h);
    if (!db) {
        if (handle_id(h, HK_DB)) return v_nil();         /* closing twice is fine */
        g_error(it, ERR_DB_HANDLE, fn);
    }
    db_close(db);
    return v_nil();
}

/* ---------------------------------------------------------------- store (SQL hidden)
 * One row per record: rowid plus the record as json text, the same shape the
 * python version keeps in sqlite.  넣기/모두/찾기/고치기/빼기 use the table
 * directly instead of going through SQL.
 */
static int store_name_ok(Str *s) {                      /* python: 이름.isidentifier() */
    if (!s->len) return 0;
    uint32_t i = 0;
    for (int first = 1; i < s->len; first = 0) {
        uint32_t adv;
        uint32_t cp = utf8_dec(s->data + i, &adv);
        if (cp < 0x80) {
            int c = (int)cp, digit = c >= '0' && c <= '9';
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || digit || c == '_')) return 0;
            if (first && digit) return 0;
        } else if (!is_alpha_cp(cp)) return 0;
        i += adv;
    }
    return 1;
}

static int is_beonho(Value k) {
    return k.tag == VT_STR && k.as.s->len == strlen(STR_BEONHO)
        && !memcmp(k.as.s->data, STR_BEONHO, k.as.s->len);
}

static Value b_store(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_JEOJANGSO];
    if (n < 2) G_ERR0(it, ERR_STORE_ARGS);
    Str *path = v_stringify(args[0]);
    Str *name = v_stringify(args[1]);
    if (!store_name_ok(name)) g_error(it, ERR_STORE_NAME, name->data);
    Db *db = db_open(it, path->data);
    DbTable *t = tab_find(db, name->data, name->len);
    if (!t) {
        t = tab_new(db, name->data, name->len);
        tab_add_col(t, "id", 2, AFF_INT, 1);
        tab_add_col(t, STR_JARYO_COL, (uint32_t)strlen(STR_JARYO_COL), AFF_TEXT, 0);
        t->autoinc = 1;
        db_save(it, db);
    } else if (t->ncols != 2 || !t->cols[0].rowid_alias)
        g_error(it, FMT_CALL_ERR, fn, "that table exists with a different shape");
    Dict *d = dict_new();
    dict_set(d, v_str(str_from(HK_STORE)), v_int(db->id));
    dict_set(d, v_str(str_from(HK_TAB)), v_str(str_new(name->data, name->len)));
    return v_dict(d);
}

/* one row as a record: the stored json plus "번호" last, as in python */
static Value store_record(Interp *it, DbTable *t, long r) {
    Cell *c = &t->rows[r].cells[1];
    Value rec = v_nil();
    if (c->kind == CELL_TEXT) {
        JP j = { c->s, it };
        rec = jp_value(&j);
    }
    if (rec.tag != VT_DICT) rec = v_dict(dict_new());
    dict_set(rec.as.d, v_str(str_from(STR_BEONHO)), v_int(t->rows[r].rowid));
    return rec;
}

static Value b_put(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_NEOHGI];
    Db *db; DbTable *t;
    need_store(it, need_arg(it, args, n, 0, fn), fn, &db, &t);
    Value rec = n > 1 ? args[1] : v_nil();
    if (rec.tag != VT_DICT) G_ERR0(it, ERR_PUT_DICT);
    Dict *keep = dict_new();                            /* 번호 is the store's to hand out */
    for (long i = 0; i < rec.as.d->n; i++)
        if (!is_beonho(rec.as.d->items[i].key))
            dict_set(keep, rec.as.d->items[i].key, rec.as.d->items[i].val);
    GB b; gb_init(&b);
    json_val(&b, v_dict(keep), 0, 0);
    int64_t rowid = tab_next_rowid(t);
    DbRow *row = tab_insert(t, rowid);
    row->cells[0] = cell_int(rowid);
    row->cells[1] = cell_text(b.p, (uint32_t)b.n);
    free(b.p);
    db_save(it, db);
    return v_int(rowid);
}

static Value b_store_all(Interp *it, Value store) {
    Db *db; DbTable *t;
    need_store(it, store, BUILTIN_NAMES[B_MODU], &db, &t);
    List *out = list_new(t->nrows ? t->nrows : 1);
    for (long r = 0; r < t->nrows; r++) list_push(out, store_record(it, t, r));
    return v_list(out);
}

static Value b_store_find(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_CHAJGI];
    Db *db; DbTable *t;
    need_store(it, args[0], fn, &db, &t);
    Value cond = n > 1 ? args[1] : v_dict(dict_new());   /* no condition: every record */
    if (cond.tag != VT_DICT) G_ERR0(it, ERR_FIND_DICT);
    List *out = list_new(4);
    for (long r = 0; r < t->nrows; r++) {
        Value rec = store_record(it, t, r);
        int all = 1;
        for (long i = 0; i < cond.as.d->n && all; i++) {
            Value got;                                   /* a missing key never matches */
            all = dict_get(rec.as.d, cond.as.d->items[i].key, &got)
                  && v_eq(got, cond.as.d->items[i].val);
        }
        if (all) list_push(out, rec);
    }
    return v_list(out);
}

static Value b_store_fix(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_GOCHIGI];
    Db *db; DbTable *t;
    need_store(it, need_arg(it, args, n, 0, fn), fn, &db, &t);
    if (n < 3 || args[2].tag != VT_DICT) G_ERR0(it, ERR_FIX_ARGS);
    long at = tab_find_rowid(t, to_int64(it, args[1], fn));
    if (at < 0) return v_bool(0);
    Value rec = store_record(it, t, at);
    Dict *upd = args[2].as.d;
    Dict *merged = dict_new();                          /* python: d.update() key order */
    for (long i = 0; i < rec.as.d->n; i++) {
        Value k = rec.as.d->items[i].key, nv;
        if (is_beonho(k)) continue;
        dict_set(merged, k, dict_get(upd, k, &nv) ? nv : rec.as.d->items[i].val);
    }
    for (long i = 0; i < upd->n; i++)
        if (!is_beonho(upd->items[i].key))
            dict_set(merged, upd->items[i].key, upd->items[i].val);
    GB b; gb_init(&b);
    json_val(&b, v_dict(merged), 0, 0);
    cell_clear(&t->rows[at].cells[1]);
    t->rows[at].cells[1] = cell_text(b.p, (uint32_t)b.n);
    free(b.p);
    db_save(it, db);
    return v_bool(1);
}

static Value b_store_remove(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_PPAEGI];
    Db *db; DbTable *t;
    need_store(it, need_arg(it, args, n, 0, fn), fn, &db, &t);
    if (n < 2) G_ERR0(it, ERR_DEL_ARGS);
    long at = tab_find_rowid(t, to_int64(it, args[1], fn));
    if (at < 0) return v_bool(0);
    tab_remove(t, at);
    db_save(it, db);
    return v_bool(1);
}

/* ---------------------------------------------------------------- 거래
 * Writes inside the function stay in memory; success flushes once, failure puts
 * back the copy taken on entry and rethrows.
 */
static Value b_transaction(Interp *it, Value *args, int n) {
    Value h = n > 0 ? args[0] : v_nil();
    Db *db = NULL;
    DbTable *t;
    if (is_store(h)) store_of_handle(h, &db, &t);
    else db = db_of_handle(h);
    if (!db) G_ERR0(it, ERR_TX_HANDLE);
    Value fnv = n > 1 ? args[1] : v_nil();
    if (db->tx == 0) {
        db_drop_tables(&db->snap, &db->nsnap);
        if (db->ntabs) {
            db->snap = malloc(sizeof(DbTable *) * (size_t)db->ntabs);
            for (int i = 0; i < db->ntabs; i++) db->snap[i] = tab_clone(db->tabs[i]);
        }
        db->nsnap = db->ntabs;
    }
    db->tx++;
    Handler hd;
    hd.prev = it->top;
    it->top = &hd;
    int code = _setjmp(hd.jb);
    if (code == 0) {
        apply_func(it, fnv, NULL, 0);
        it->top = hd.prev;
        if (--db->tx == 0) {
            db_drop_tables(&db->snap, &db->nsnap);
            db_save(it, db);
        }
        return v_nil();
    }
    it->top = hd.prev;
    db_drop_tables(&db->tabs, &db->ntabs);
    db->tabs = db->snap;
    db->ntabs = db->captabs = db->nsnap;
    db->snap = NULL; db->nsnap = 0;
    db->tx = 0;
    g_throw(it, code);
}

/* ---------------------------------------------------------------- http server
 * One connection at a time, HTTP/1.0 with Content-Length: what python's
 * HTTPServer + BaseHTTPRequestHandler default to.  A route gets the request
 * dict {메서드,경로,질의,본문,쿠키} and answers with a string (200 html) or
 * {상태,헤더,본문,쿠키설정}.  Unknown paths go to the static folder if one was
 * given, otherwise 404.
 */
static const char *const SRV_HTML = "text/html; charset=utf-8";

static const struct { const char *ext, *type; } MIME_TABLE[] = {
    { ".html", "text/html; charset=utf-8" }, { ".css", "text/css; charset=utf-8" },
    { ".js", "text/javascript; charset=utf-8" }, { ".png", "image/png" },
    { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" }, { ".svg", "image/svg+xml" },
    { ".ico", "image/x-icon" }, { ".json", "application/json; charset=utf-8" },
    { ".txt", "text/plain; charset=utf-8" },
};

static const char *mime_of(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot) {
        char ext[16];
        size_t n = strlen(dot);
        if (n < sizeof ext) {
            for (size_t i = 0; i <= n; i++) ext[i] = (char)ascii_lower((unsigned char)dot[i]);
            for (size_t i = 0; i < sizeof MIME_TABLE / sizeof MIME_TABLE[0]; i++)
                if (!strcmp(ext, MIME_TABLE[i].ext)) return MIME_TABLE[i].type;
        }
    }
    return "application/octet-stream";
}

static const char *reason_of(int code) {
    switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default: return "???";
    }
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* urllib.parse.quote(s, safe='') */
static void pct_enc(GB *b, const char *s, uint32_t n) {
    static const char *const HEX = "0123456789ABCDEF";
    for (uint32_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '_' || c == '.' || c == '-' || c == '~')
            gb_ch(b, (char)c);
        else { gb_ch(b, '%'); gb_ch(b, HEX[c >> 4]); gb_ch(b, HEX[c & 15]); }
    }
}

/* urllib.parse.unquote / unquote_plus; a broken escape stays as written */
static Str *pct_dec(const char *s, uint32_t n, int plus) {
    GB b; gb_init(&b);
    for (uint32_t i = 0; i < n; i++) {
        if (plus && s[i] == '+') { gb_ch(&b, ' '); continue; }
        if (s[i] == '%' && i + 2 < n) {
            int hi = hexval((unsigned char)s[i + 1]), lo = hexval((unsigned char)s[i + 2]);
            if (hi >= 0 && lo >= 0) { gb_ch(&b, (char)(hi * 16 + lo)); i += 2; continue; }
        }
        gb_ch(&b, s[i]);
    }
    return gb_done(&b);
}

/* urllib.parse.parse_qs: '&' separated; a pair with no '=' or an empty value is
 * dropped, an empty name is kept, and the first value of a name wins */
static Dict *parse_qs(const char *s, uint32_t n) {
    Dict *d = dict_new();
    uint32_t i = 0;
    while (i < n) {
        uint32_t e = i;
        while (e < n && s[e] != '&') e++;
        uint32_t eq = i;
        while (eq < e && s[eq] != '=') eq++;
        if (eq < e && e > eq + 1) {
            Value k = v_str(pct_dec(s + i, eq - i, 1)), seen;
            if (!dict_get(d, k, &seen)) dict_set(d, k, v_str(pct_dec(s + eq + 1, e - eq - 1, 1)));
        }
        i = e + 1;
    }
    return d;
}

static Dict *parse_cookies(const char *s) {
    Dict *d = dict_new();
    if (!s) return d;
    uint32_t n = (uint32_t)strlen(s), i = 0;
    for (;;) {
        uint32_t e = i;
        while (e < n && s[e] != ';') e++;
        uint32_t a = i, z = e;
        while (a < z && (s[a] == ' ' || s[a] == '\t')) a++;
        while (z > a && (s[z - 1] == ' ' || s[z - 1] == '\t')) z--;
        uint32_t eq = a;
        while (eq < z && s[eq] != '=') eq++;
        if (eq > a)
            dict_set(d, v_str(pct_dec(s + a, eq - a, 0)),
                     v_str(eq < z ? pct_dec(s + eq + 1, z - eq - 1, 0) : str_from("")));
        if (e >= n) break;
        i = e + 1;
    }
    return d;
}

static void write_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w <= 0) return;
        p += (size_t)w;
        n -= (size_t)w;
    }
}

static void srv_send(int fd, int code, Dict *headers, Dict *setcookie,
                     const char *body, size_t blen) {
    GB b; gb_init(&b);
    char line[80];
    snprintf(line, sizeof line, "HTTP/1.0 %d %s\r\n", code, reason_of(code));
    gb_cz(&b, line);
    if (headers)
        for (long i = 0; i < headers->n; i++) {
            gb_str(&b, v_stringify(headers->items[i].key));
            gb_cz(&b, ": ");
            gb_str(&b, v_stringify(headers->items[i].val));
            gb_cz(&b, "\r\n");
        }
    if (setcookie)
        for (long i = 0; i < setcookie->n; i++) {
            Value val = setcookie->items[i].val;
            int has_age = 0;
            int64_t age = 0;
            if (val.tag == VT_DICT) {                    /* {"값": ..., "만료": 초} */
                Value tmp;
                if (dict_get(val.as.d, v_str(str_from(STR_COOKIE_MAXAGE)), &tmp)) {
                    has_age = 1;
                    age = tmp.tag == VT_FLOAT ? (int64_t)tmp.as.f
                        : tmp.tag == VT_INT ? tmp.as.i : tmp.tag == VT_BOOL ? tmp.as.b : 0;
                }
                val = dict_get(val.as.d, v_str(str_from(STR_COOKIE_VAL)), &tmp)
                    ? tmp : v_str(str_from(""));
            }
            Str *k = v_stringify(setcookie->items[i].key);
            Str *v = v_stringify(val);
            gb_cz(&b, "Set-Cookie: ");
            pct_enc(&b, k->data, k->len);
            gb_ch(&b, '=');
            pct_enc(&b, v->data, v->len);
            gb_cz(&b, "; Path=/; HttpOnly");
            if (has_age) {
                snprintf(line, sizeof line, "; Max-Age=%lld", (long long)age);
                gb_cz(&b, line);
            }
            gb_cz(&b, "\r\n");
        }
    snprintf(line, sizeof line, "Content-Length: %llu\r\n\r\n", (unsigned long long)blen);
    gb_cz(&b, line);
    gb_put(&b, body, blen);
    write_all(fd, b.p, b.n);
    free(b.p);
}

static void srv_send_html(int fd, int code, const char *prefix, Str *tail) {
    Dict *h = dict_new();
    dict_set(h, v_str(str_from("Content-Type")), v_str(str_from(SRV_HTML)));
    GB b; gb_init(&b);
    gb_cz(&b, prefix);
    if (tail) gb_str(&b, tail);
    srv_send(fd, code, h, NULL, b.p, b.n);
    free(b.p);
}

/* headers + body, or NULL when the peer went away */
static char *srv_read_request(int fd, size_t *hdrend_out, size_t *total_out) {
    GB b; gb_init(&b);
    size_t hdrend = 0;
    for (;;) {
        for (size_t i = 0; i + 1 < b.n && !hdrend; i++) {
            if (b.p[i] == '\n' && b.p[i + 1] == '\n') hdrend = i + 2;
            else if (i + 3 < b.n && !memcmp(b.p + i, "\r\n\r\n", 4)) hdrend = i + 4;
        }
        if (hdrend) break;
        char tmp[4096];
        ssize_t got = read(fd, tmp, sizeof tmp);
        if (got <= 0 || b.n > (16u << 20)) { free(b.p); return NULL; }
        gb_put(&b, tmp, (size_t)got);
    }
    size_t clen = 0;
    for (size_t i = 0; i + 15 < hdrend; i++)
        if ((i == 0 || b.p[i - 1] == '\n') && name_eq(b.p + i, 15, "content-length:", 15)) {
            clen = (size_t)strtoull(b.p + i + 15, NULL, 10);
            break;
        }
    while (b.n < hdrend + clen) {
        char tmp[4096];
        ssize_t got = read(fd, tmp, sizeof tmp);
        if (got <= 0) break;
        gb_put(&b, tmp, (size_t)got);
    }
    *hdrend_out = hdrend;
    *total_out = b.n;
    return b.p;
}

static Str *srv_header(char *req, size_t hdrend, const char *name) {
    size_t nl = strlen(name);
    for (size_t i = 0; i + nl < hdrend; i++)
        if ((i == 0 || req[i - 1] == '\n') && name_eq(req + i, (uint32_t)nl, name, (uint32_t)nl)) {
            const char *p = req + i + nl;
            while (*p == ' ' || *p == '\t') p++;
            const char *e = p;
            while (e < req + hdrend && *e != '\r' && *e != '\n') e++;
            return str_new(p, (uint32_t)(e - p));
        }
    return NULL;
}

static void srv_static(int fd, Str *dir, Str *path) {
    char root[PATH_MAX], full[PATH_MAX * 2], real[PATH_MAX];
    GB p; gb_init(&p);
    gb_str(&p, path);
    if (!p.n || p.p[p.n - 1] == '/') gb_cz(&p, "index.html");
    const char *rel = p.p;
    while (*rel == '/') rel++;
    int ok = realpath(dir->data, root) && strlen(root) + strlen(rel) + 2 < sizeof full;
    if (ok) snprintf(full, sizeof full, "%s/%s", root, rel);
    free(p.p);
    struct stat st;
    size_t rootlen = ok ? strlen(root) : 0;
    if (!ok || !realpath(full, real)
        || !(!strcmp(real, root) || (!strncmp(real, root, rootlen) && real[rootlen] == '/'))
        || stat(real, &st) || !S_ISREG(st.st_mode)) {
        srv_send_html(fd, 404, SRV_404, path);
        return;
    }
    FILE *f = fopen(real, "rb");
    if (!f) { srv_send_html(fd, 404, SRV_404, path); return; }
    GB body; gb_init(&body);
    char tmp[8192];
    size_t got;
    while ((got = fread(tmp, 1, sizeof tmp, f)) > 0) gb_put(&body, tmp, got);
    fclose(f);
    Dict *h = dict_new();
    dict_set(h, v_str(str_from("Content-Type")), v_str(str_from(mime_of(real))));
    srv_send(fd, 200, h, NULL, body.p, body.n);
    free(body.p);
}

/* 0 when the request was answered, else the interpreter signal to rethrow */
static int srv_one(Interp *it, int fd, Dict *routes, Str *staticdir) {
    size_t hdrend = 0, total = 0;
    char *req = srv_read_request(fd, &hdrend, &total);
    if (!req) return 0;
    char *sp1 = strchr(req, ' ');
    char *eol = strpbrk(req, "\r\n");
    if (!sp1 || !eol || sp1 > eol) { free(req); return 0; }
    *sp1 = 0;
    char *method = req;
    char *target = sp1 + 1;
    char *sp2 = strchr(target, ' ');
    if (sp2 && sp2 < eol) *sp2 = 0;
    else *eol = 0;
    char *hash = strchr(target, '#');                    /* urlsplit drops the fragment */
    if (hash) *hash = 0;
    char *qmark = strchr(target, '?');
    uint32_t plen = (uint32_t)(qmark ? (size_t)(qmark - target) : strlen(target));
    Str *path = pct_dec(target, plen, 0);
    Dict *query = qmark ? parse_qs(qmark + 1, (uint32_t)strlen(qmark + 1)) : dict_new();
    Str *ctype = srv_header(req, hdrend, "content-type:");
    Str *cookie = srv_header(req, hdrend, "cookie:");
    Str *body = str_new(req + hdrend, (uint32_t)(total - hdrend));
    if (!strcmp(method, "POST") && ctype
        && !strncmp(ctype->data, "application/x-www-form-urlencoded", 33)) {
        Dict *form = parse_qs(body->data, body->len);    /* form fields join 질의, body wins */
        for (long i = 0; i < form->n; i++) dict_set(query, form->items[i].key, form->items[i].val);
    }
    int get_or_post = !strcmp(method, "GET") || !strcmp(method, "POST");
    Value route;
    if (get_or_post && !dict_get(routes, v_str(path), &route)) {
        if (staticdir) srv_static(fd, staticdir, path);
        else srv_send_html(fd, 404, SRV_404, path);
        free(req);
        return 0;
    }
    if (!get_or_post) {
        Str *m = str_from(method);
        free(req);
        srv_send_html(fd, 501, "501 Unsupported method: ", m);
        return 0;
    }
    Dict *reqd = dict_new();
    dict_set(reqd, v_str(str_from(STR_REQ_METHOD)), v_str(str_from(method)));
    dict_set(reqd, v_str(str_from(STR_REQ_PATH)), v_str(path));
    dict_set(reqd, v_str(str_from(STR_REQ_QUERY)), v_dict(query));
    dict_set(reqd, v_str(str_from(STR_REQ_BODY)), v_str(body));
    dict_set(reqd, v_str(str_from(STR_REQ_COOKIE)),
             v_dict(parse_cookies(cookie ? cookie->data : NULL)));
    free(req);

    Value arg = v_dict(reqd);
    Value result;
    Handler h;
    h.prev = it->top;
    it->top = &h;
    int code = _setjmp(h.jb);
    if (code) {
        it->top = h.prev;
        if (code != GS_ERR) return code;
        srv_send_html(fd, 500, SRV_500, str_from(it->err ? it->err : ""));
        return 0;
    }
    result = apply_func(it, route, &arg, 1);
    it->top = h.prev;
    if (result.tag == VT_DICT) {
        Value st, hd, bd, ck;
        int status = dict_get(result.as.d, v_str(str_from(STR_RES_STATUS)), &st)
                   ? (int)to_int64(it, st, BUILTIN_NAMES[B_SEOBEO]) : 200;
        Dict *headers = NULL;
        if (dict_get(result.as.d, v_str(str_from(STR_RES_HEADERS)), &hd)
            && hd.tag == VT_DICT && hd.as.d->n)
            headers = hd.as.d;
        else {
            headers = dict_new();
            dict_set(headers, v_str(str_from("Content-Type")), v_str(str_from(SRV_HTML)));
        }
        Str *out = dict_get(result.as.d, v_str(str_from(STR_REQ_BODY)), &bd)
                 ? v_stringify(bd) : str_from("");
        Dict *cookies = dict_get(result.as.d, v_str(str_from(STR_RES_SETCOOKIE)), &ck)
                        && ck.tag == VT_DICT ? ck.as.d : NULL;
        srv_send(fd, status, headers, cookies, out->data, out->len);
    } else {
        Str *out = v_stringify(result);
        Dict *headers = dict_new();
        dict_set(headers, v_str(str_from("Content-Type")), v_str(str_from(SRV_HTML)));
        srv_send(fd, 200, headers, NULL, out->data, out->len);
    }
    return 0;
}

static volatile sig_atomic_t g_srv_stop;
static void srv_on_sigint(int sig) { (void)sig; g_srv_stop = 1; }

static Value b_server(Interp *it, Value *args, int n) {
    const char *fn = BUILTIN_NAMES[B_SEOBEO];
    int port = n > 0 ? (int)to_int64(it, args[0], fn) : 8000;
    if (n > 1 && args[1].tag != VT_DICT) G_ERR0(it, ERR_SRV_ROUTES);
    Dict *routes = n > 1 ? args[1].as.d : dict_new();
    Str *staticdir = n > 2 ? v_stringify(args[2]) : NULL;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) g_error(it, FMT_CALL_ERR, fn, strerror(errno));
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(srv, (struct sockaddr *)&a, sizeof a) || listen(srv, 8)) {
        int e = errno;
        close(srv);
        g_error(it, FMT_CALL_ERR, fn, strerror(e));
    }
    socklen_t alen = sizeof a;
    if (!getsockname(srv, (struct sockaddr *)&a, &alen)) port = ntohs(a.sin_port);
    fprintf(it->out, "%s%d\n", SRV_BANNER, port);
    fflush(it->out);
    struct sigaction sa, oldint;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = srv_on_sigint;
    g_srv_stop = 0;
    sigaction(SIGINT, &sa, &oldint);                     /* Ctrl-C ends the loop, like python */
    void (*oldpipe)(int) = signal(SIGPIPE, SIG_IGN);     /* a client hangup is not fatal */
    int rethrow = 0;
    while (!g_srv_stop && !rethrow) {
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        struct timeval tv = { 30, 0 };                   /* a stalled peer must not wedge us */
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        rethrow = srv_one(it, fd, routes, staticdir);
        close(fd);
    }
    sigaction(SIGINT, &oldint, NULL);
    signal(SIGPIPE, oldpipe);
    close(srv);
    if (rethrow) g_throw(it, rethrow);
    return v_nil();
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
    case B_CHAJGI:                                  /* 찾기(저장소, 조건) or 찾기(목록, 함수) */
        return n > 0 && is_store(args[0]) ? b_store_find(it, args, n) : b_findfn(it, args, n);
    case B_ISSNA: return b_any(it, args, n);
    case B_MODU:                                    /* 모두(저장소) or 모두(목록, 함수) */
        return n > 0 && is_store(args[0]) ? b_store_all(it, args[0]) : b_all(it, args, n);
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
    case B_SEOBEO: return b_server(it, args, n);
    case B_JARYOYEOLGI: return b_db_open(it, args, n);
    case B_SILHAENG: return b_db_exec(it, args, n);
    case B_JILUI: return b_db_query(it, args, n);
    case B_JARYODADGI: return b_db_close(it, args, n);
    case B_JEOJANGSO: return b_store(it, args, n);
    case B_NEOHGI: return b_put(it, args, n);
    case B_GOCHIGI: return b_store_fix(it, args, n);
    case B_PPAEGI: return b_store_remove(it, args, n);
    case B_GEORAE: return b_transaction(it, args, n);
    }
    g_error(it, FMT_CALL_ERR, "builtin", "bad id");
}
