/* gc.c — mark & sweep collector for heap values (Str/List/Dict/Func).
 *
 * Why a collector instead of reference counts: values travel by copy with no
 * ownership rule anywhere.  Builtins return elements borrowed from their
 * arguments (choedae -> l->items[i]), eval() results are copied into envs,
 * lists and dicts, and index_get() hands back an interior element.  Bolting
 * retain/release onto all of that means editing hundreds of sites where a
 * single mistake is a use-after-free, which is far worse than the leak we are
 * fixing.  Cycles settle it: every named function stores itself into the env
 * it captured (env -> Func -> env), and chuga(mogrog, mogrog) makes a list
 * that contains itself, so refcounts alone could free neither.
 *
 * Roots are precise for state the interpreter owns (env registry, Interp
 * fields) and conservative for the C stack plus callee-saved registers, which
 * is where every temporary lives.  Interior pointers count as references, so a
 * `char *` into a string's bytes or a `Value *` into a list's item array keeps
 * the owning object alive.  Consequence: an unreachable object may survive a
 * cycle or two (a stale stack word can name it), but a reachable one is never
 * freed.
 *
 * Objects carry a GCHdr in front of the payload.  AST string literals are
 * allocated `perm` and never swept, so marking them is harmless and the AST
 * needs no scanning.
 */
#include "ganada.h"

#if defined(__has_attribute)
#  if __has_attribute(no_sanitize)
#    define GC_NOSAN __attribute__((no_sanitize("address")))
#  endif
#endif
#ifndef GC_NOSAN
#  define GC_NOSAN
#endif

typedef struct GCHdr {
    struct GCHdr *next;
    size_t size;                /* payload bytes */
    uint32_t mark;              /* == g_epoch while reachable */
    uint16_t kind;              /* GC_STR / GC_LIST / GC_DICT / GC_FUNC */
    uint16_t perm;              /* 1: never swept (ast literals) */
} GCHdr;

#define HDR_OF(p)  ((GCHdr *)((char *)(p) - sizeof(GCHdr)))
#define BODY_OF(h) ((void *)((char *)(h) + sizeof(GCHdr)))
#define WORD sizeof(void *)

static GCHdr *g_objs;                   /* every live object, newest first */
static Interp *g_it;                    /* the one interpreter (main makes one) */
static const char *g_bottom;            /* high end of the C stack */
static uint32_t g_epoch = 1;
static size_t g_live = 0;               /* bytes retained by the last sweep */
static size_t g_since = 0;              /* bytes handed out since then */
static size_t g_limit = 1u << 20;
static int g_stress;                    /* GANADA_GC_STRESS=n: collect every n */
static int g_ticks;
static int g_busy;

/* gray stack (explicit, so nested structures cannot blow the C stack) */
static GCHdr **g_gray;
static size_t g_ngray, g_capgray;

/* address ranges of all objects, rebuilt per collection */
typedef struct { uintptr_t lo, hi; GCHdr *h; } Rng;
static Rng *g_rng;
static size_t g_nrng, g_caprng;

typedef struct { uintptr_t page; uint32_t rng, next; } PgEnt;
static PgEnt *g_pge;
static size_t g_npge, g_cappge;
static uint32_t *g_pgtab;               /* page slot -> entry index + 1 */
static size_t g_pgcap;

#define PAGE_SHIFT 12

static void *grow(void *p, size_t *cap, size_t need, size_t esz) {
    if (*cap >= need) return p;
    size_t c = *cap ? *cap : 64;
    while (c < need) c *= 2;
    *cap = c;
    return realloc(p, c * esz);
}

/* ---------------------------------------------------------------- ranges */
static void add_rng(const void *lo, size_t bytes, GCHdr *h) {
    if (!lo || !bytes) return;
    g_rng = grow(g_rng, &g_caprng, g_nrng + 1, sizeof(Rng));
    g_rng[g_nrng].lo = (uintptr_t)lo;
    g_rng[g_nrng].hi = (uintptr_t)lo + bytes;
    g_rng[g_nrng].h = h;
    g_nrng++;
}

static size_t page_slot(uintptr_t page) {
    return (size_t)((page * 0x9E3779B97F4A7C15ULL) >> 32) & (g_pgcap - 1);
}

static void build_ranges(void) {
    g_nrng = 0; g_npge = 0;
    for (GCHdr *h = g_objs; h; h = h->next) {
        add_rng(h, sizeof(GCHdr) + h->size, h);
        if (h->kind == GC_LIST) {
            List *l = BODY_OF(h);
            add_rng(l->items, (size_t)l->cap * sizeof(Value), h);
        } else if (h->kind == GC_DICT) {
            Dict *d = BODY_OF(h);
            add_rng(d->items, (size_t)d->cap * sizeof(DEntry), h);
            add_rng(d->idx, (size_t)d->icap * sizeof(uint32_t), h);
        }
    }
    size_t want = 64;
    while (want < g_nrng * 4) want *= 2;
    if (want != g_pgcap) {
        free(g_pgtab);
        g_pgcap = want;
        g_pgtab = malloc(g_pgcap * sizeof(uint32_t));
    }
    memset(g_pgtab, 0, g_pgcap * sizeof(uint32_t));
    for (size_t i = 0; i < g_nrng; i++) {
        uintptr_t p0 = g_rng[i].lo >> PAGE_SHIFT, p1 = (g_rng[i].hi - 1) >> PAGE_SHIFT;
        for (uintptr_t p = p0; p <= p1; p++) {
            g_pge = grow(g_pge, &g_cappge, g_npge + 1, sizeof(PgEnt));
            size_t slot = page_slot(p);
            g_pge[g_npge].page = p;
            g_pge[g_npge].rng = (uint32_t)i;
            g_pge[g_npge].next = g_pgtab[slot];
            g_npge++;
            g_pgtab[slot] = (uint32_t)g_npge;
        }
    }
}

/* object containing address p (base or interior), or NULL */
static GCHdr *find_obj(uintptr_t p) {
    uintptr_t page = p >> PAGE_SHIFT;
    for (uint32_t e = g_pgtab[page_slot(page)]; e; e = g_pge[e - 1].next) {
        if (g_pge[e - 1].page != page) continue;
        Rng *r = &g_rng[g_pge[e - 1].rng];
        if (p >= r->lo && p < r->hi) return r->h;
    }
    return NULL;
}

/* ---------------------------------------------------------------- marking */
static void mark_hdr(GCHdr *h) {
    if (h->mark == g_epoch) return;
    h->mark = g_epoch;
    if (h->kind == GC_STR) return;
    g_gray = grow(g_gray, &g_capgray, g_ngray + 1, sizeof(GCHdr *));
    g_gray[g_ngray++] = h;
}

static void mark_val(Value v) {
    switch (v.tag) {
    case VT_STR: mark_hdr(HDR_OF(v.as.s)); break;
    case VT_LIST: mark_hdr(HDR_OF(v.as.l)); break;
    case VT_DICT: mark_hdr(HDR_OF(v.as.d)); break;
    case VT_FUNC: mark_hdr(HDR_OF(v.as.fn)); break;
    default: break;
    }
}

static void mark_env(Env *e) {
    for (; e; e = e->parent) {
        if (e->gcmark == g_epoch) return;
        e->gcmark = g_epoch;
        if (e->shared) mark_hdr(HDR_OF(e->shared));
        if (e->cap == ENV_INLINE) {
            for (int i = 0; i < e->n; i++) mark_val(e->vals[i]);
        } else {
            for (int i = 0; i < e->cap; i++)
                if (e->keys[i]) mark_val(e->vals[i]);
        }
    }
}

static void drain(void) {
    while (g_ngray) {
        GCHdr *h = g_gray[--g_ngray];
        switch (h->kind) {
        case GC_LIST: {
            List *l = BODY_OF(h);
            for (long i = 0; i < l->n; i++) mark_val(l->items[i]);
            break;
        }
        case GC_DICT: {
            Dict *d = BODY_OF(h);
            for (long i = 0; i < d->n; i++) {
                mark_val(d->items[i].key);
                mark_val(d->items[i].val);
            }
            break;
        }
        /* a closure holds a reference on its env, so rc > 0 here */
        case GC_FUNC: mark_env(((Func *)BODY_OF(h))->env); break;
        /* found by a stack word; rc == 0 means pooled, its slots are stale */
        case GC_ENV: {
            Env *e = BODY_OF(h);
            if (e->rc) mark_env(e);
            break;
        }
        default: break;
        }
    }
}

/* words of memory that may hold pointers we do not track precisely */
GC_NOSAN
static void scan_words(const char *lo, const char *hi) {
    uintptr_t a = ((uintptr_t)lo + WORD - 1) & ~(uintptr_t)(WORD - 1);
    for (; a + WORD <= (uintptr_t)hi; a += WORD) {
        uintptr_t w;
        memcpy(&w, (const void *)a, sizeof w);
        if (w < 4096) continue;
        GCHdr *h = find_obj(w);
        if (h) mark_hdr(h);
    }
}

/* ---------------------------------------------------------------- sweep */
static void sweep(void) {
    GCHdr **pp = &g_objs;
    size_t live = 0;
    while (*pp) {
        GCHdr *h = *pp;
        if (h->perm) { pp = &h->next; continue; }   /* kept, and off the budget */
        if (h->mark == g_epoch) {
            live += sizeof(GCHdr) + h->size;
            if (h->kind == GC_LIST) {
                live += (size_t)((List *)BODY_OF(h))->cap * sizeof(Value);
            } else if (h->kind == GC_DICT) {
                Dict *d = BODY_OF(h);
                live += (size_t)d->cap * sizeof(DEntry) + (size_t)d->icap * sizeof(uint32_t);
            }
            pp = &h->next;
            continue;
        }
        *pp = h->next;
        switch (h->kind) {
        case GC_LIST: free(((List *)BODY_OF(h))->items); break;
        case GC_DICT: {
            Dict *d = BODY_OF(h);
            free(d->items); free(d->idx);
            break;
        }
        /* the captured env was retained when the closure was built */
        case GC_FUNC: env_release(g_it, ((Func *)BODY_OF(h))->env); break;
        default: break;
        }
        free(h);
    }
    g_live = live;
}

/* ---------------------------------------------------------------- collect */
static void gc_collect(void) {
    if (!g_it || !g_bottom || g_busy) return;
    g_busy = 1;
    if (++g_epoch == 0) g_epoch = 1;
    build_ranges();

    mark_val(g_it->retval);
    if (g_it->ai_model) mark_hdr(HDR_OF(g_it->ai_model));
    for (int i = 0; i < g_it->nmods; i++) mark_hdr(HDR_OF(g_it->mod_dicts[i]));
    if (g_it->g) mark_env(g_it->g);
    /* every other live env is held by a C frame (found by the stack scan
     * below), by a child env's parent link, or by a closure.  Rooting envs
     * merely because rc > 0 would keep every env <-> Func cycle forever. */

    jmp_buf regs;                       /* spills the callee-saved registers */
    _setjmp(regs);
    const char *sp = (const char *)&regs;
    const char *fa = (const char *)__builtin_frame_address(0);
    if (fa < sp) sp = fa;
    scan_words((const char *)&regs, (const char *)&regs + sizeof regs);
    if (sp < g_bottom) scan_words(sp, g_bottom);

    drain();
    sweep();
    g_since = 0;
    g_limit = g_live > (1u << 20) ? g_live : (size_t)(1u << 20);
    g_busy = 0;
}

/* ---------------------------------------------------------------- api */
void gc_init(void *stack_bottom) {
    g_bottom = (const char *)stack_bottom;
    const char *s = getenv("GANADA_GC_STRESS");
    if (s) { int v = atoi(s); g_stress = v > 0 ? v : 0; }
}

void gc_set_interp(Interp *it) { g_it = it; }
Interp *gc_interp(void) { return g_it; }

void *gc_alloc(size_t size, int kind) {
    if (g_stress) {
        if (++g_ticks >= g_stress) { g_ticks = 0; gc_collect(); }
    } else if (g_since >= g_limit) gc_collect();
    GCHdr *h = malloc(sizeof(GCHdr) + size);
    h->next = g_objs; g_objs = h;
    h->size = size; h->mark = 0; h->kind = (uint16_t)kind; h->perm = 0;
    g_since += sizeof(GCHdr) + size;
    return BODY_OF(h);
}

/* never collected: ast literals live as long as the program text */
void *gc_alloc_perm(size_t size, int kind) {
    GCHdr *h = malloc(sizeof(GCHdr) + size);
    h->next = g_objs; g_objs = h;
    h->size = size; h->mark = 0; h->kind = (uint16_t)kind; h->perm = 1;
    return BODY_OF(h);
}

/* forces a pointer to stay live in its frame across a call */
void gc_keep_alive(void *p) {
    static void *volatile sink;
    sink = p;
    (void)sink;                 /* volatile access: the store cannot be dropped */
}
