/* ganada.h — common declarations for the ganada C interpreter.
 * All Korean text lives in tables.h as escaped constants; ASCII only here. */
#ifndef GANADA_H
#define GANADA_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "tables.h"

/* ---------------------------------------------------------------- values */
enum { VT_NIL, VT_BOOL, VT_INT, VT_FLOAT, VT_STR, VT_LIST, VT_DICT, VT_FUNC };

typedef struct Value Value;
typedef struct Str Str;
typedef struct List List;
typedef struct Dict Dict;
typedef struct Func Func;
typedef struct Env Env;
typedef struct Node Node;
typedef struct Interp Interp;

struct Value {
    uint8_t tag;
    uint8_t _pad[7];
    union {
        int64_t i;
        double f;
        int b;
        Str *s;
        List *l;
        Dict *d;
        Func *fn;
    } as;
};

struct Str {
    uint32_t len;               /* byte length (excluding NUL) */
    uint32_t cplen;             /* codepoint count; UINT32_MAX = unknown */
    uint32_t flags;             /* bit0: HTML fragment (skip escaping) */
    char data[];                /* NUL-terminated UTF-8 */
};

struct List {
    long n, cap;
    Value *items;
};

typedef struct { Value key, val; } DEntry;

struct Dict {
    long n, cap;                /* entries in insertion order */
    DEntry *items;
    uint32_t *idx;              /* hash -> position+1 (0 = empty) */
    uint32_t icap;              /* power of two */
};

struct Func {
    const char *name;           /* interned display name */
    const char **pnames;        /* interned param names */
    Node **pdefs;               /* default exprs (NULL = required) */
    int nparams;
    Node *body;                 /* N_BLOCK */
    Env *env;                   /* captured definition env */
};

/* ---------------------------------------------------------------- AST */
enum {
    N_BLOCK, N_STMT, N_IMPORT, N_BREAK, N_CONT, N_TRY, N_FUNC, N_IF,
    N_WHILE, N_FOR, N_FOREACH, N_RETURN, N_ASSIGN, N_DESTRUCT, N_SETINDEX,
    N_EXPRSTMT, N_LIT, N_VAR, N_UN, N_BIN, N_LIST, N_DICT, N_LAMBDA,
    N_INDEX, N_SLICE, N_CALL
};

/* binary/unary op ids */
enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR, OP_NOT, OP_NEG
};

struct Node {
    int kind;
    int line;
    Node *a, *b, *c;            /* generic children */
    Node **items;               /* stmts / args / elems / dict pairs (2n) */
    int nitems;
    Value lit;                  /* N_LIT */
    const char *name;           /* interned: var/assign/func/try-err name */
    const char *str;            /* N_IMPORT path (malloc'd, raw) */
    int op;                     /* N_BIN / N_UN */
    const char **pnames;        /* N_FUNC / N_LAMBDA */
    Node **pdefs;
    int nparams;
};

/* ---------------------------------------------------------------- tokens */
enum { T_KW, T_ID, T_NUM, T_STR, T_OP, T_EOF };

typedef struct {
    int kind;
    const char *s;              /* interned for KW/ID/OP; malloc'd for STR */
    Value num;                  /* T_NUM */
    int line, col;
} Tok;

/* ---------------------------------------------------------------- errors */
enum { GS_NONE = 0, GS_ERR = 1, GS_RET = 2, GS_BRK = 3, GS_CONT = 4 };

typedef struct Handler {
    jmp_buf jb;
    struct Handler *prev;
} Handler;

/* ---------------------------------------------------------------- env */
#define ENV_INLINE 8
struct Env {
    Env *parent;
    unsigned rc;
    int n, cap;                 /* cap <= ENV_INLINE uses inline arrays */
    const char **keys;
    Value *vals;
    const char *ikeys[ENV_INLINE];
    Value ivals[ENV_INLINE];
    Dict *shared;               /* module env: storage shared with a dict */
    Env *pool_next;
    uint32_t gcmark;            /* gc: visited stamp */
};

/* ---------------------------------------------------------------- interp */
typedef struct { const char *ptr; int id; } PtrEnt;

struct Interp {
    Env *g;
    Handler *top;
    char *err;                  /* GS_ERR payload (malloc'd) */
    Value retval;               /* GS_RET payload */
    int cur_line;
    int depth;
    char *base_dir;
    FILE *out;
    Str *ai_model;
    /* intern table */
    char **itab; uint32_t icap, in;
    /* interned pointers */
    const char *kw_ptr[NUM_KEYWORDS];
    const char *builtin_ptr[B_COUNT];
    const char *goreugi_ptr;
    /* builtin dispatch: pointer -> id */
    PtrEnt *bmap; uint32_t bmap_cap;
    /* env pool */
    Env *env_pool;
    /* imports + module cache */
    char **imported; int nimported, capimported;
    char **mod_paths; Dict **mod_dicts; int nmods, capmods;
    int rng_seeded;
};

/* ---------------------------------------------------------------- gc.c */
enum { GC_STR, GC_LIST, GC_DICT, GC_FUNC, GC_ENV };

void gc_init(void *stack_bottom);           /* called once from main */
void gc_set_interp(Interp *it);
Interp *gc_interp(void);                    /* current interp (NULL before set) */
void *gc_alloc(size_t size, int kind);      /* collectable heap value */
void *gc_alloc_perm(size_t size, int kind); /* never collected */
void gc_keep_alive(void *p);                /* pin a local across a call */

/* ---------------------------------------------------------------- value.c */
Value v_nil(void);
Value v_bool(int b);
Value v_int(int64_t i);
Value v_float(double f);
Value v_str(Str *s);
Value v_list(List *l);
Value v_dict(Dict *d);

Str *str_new(const char *data, uint32_t len);
Str *str_from(const char *cstr);
Str *str_perm(const char *cstr);             /* uncollectable: ast literals */
Str *str_own(char *data, uint32_t len);      /* takes ownership of malloc'd buf */
Str *str_concat(Str *a, Str *b);
Str *str_concat3(Str *a, Str *b, Str *c);
uint32_t str_cplen(Str *s);
long str_cp_byteoff(Str *s, long cpidx);     /* byte offset of cp index (clamped) */
Str *str_sub_cp(Str *s, long start_cp, long end_cp);
Str *str_char_at(Str *s, long cpidx);        /* 1-codepoint string */
uint32_t utf8_dec(const char *p, uint32_t *adv);

List *list_new(long cap);
void list_push(List *l, Value v);

Dict *dict_new(void);
void dict_set(Dict *d, Value key, Value val);
bool dict_get(Dict *d, Value key, Value *out);

bool v_truthy(Value v);
bool v_eq(Value a, Value b);
/* compare: -1/0/1; sets *err on incomparable */
int v_cmp(Value a, Value b, bool *err);
const char *v_typename(Value v);            /* tables.h strings */
Str *v_stringify(Value v);
void fmt_float(double x, char *buf);        /* shortest round-trip %g */
Str *str_float_repr(double x);              /* python repr(float) */

/* interning (per-interp) */
const char *intern(Interp *it, const char *s, uint32_t len);
const char *intern_cz(Interp *it, const char *s);

/* ---------------------------------------------------------------- errors */
void g_throw(Interp *it, int code) __attribute__((noreturn));
void g_error(Interp *it, const char *fmt, ...) __attribute__((noreturn));
#define G_ERR0(it, msg) g_error((it), "%s", (msg))

/* ---------------------------------------------------------------- lexer */
Tok *lex_all(Interp *it, const char *src, int *ntoks);   /* throws HanError */

/* ---------------------------------------------------------------- parser */
Node *parse_all(Interp *it, Tok *toks);                  /* returns N_BLOCK */

/* ---------------------------------------------------------------- interp */
Interp *interp_new(void);
Env *env_new(Interp *it, Env *parent);
void env_release(Interp *it, Env *e);
Value env_get(Interp *it, Env *e, const char *name);    /* throws */
void env_define(Env *e, const char *name, Value v);
void env_set_existing_or_define(Env *e, const char *name, Value v);
void exec_block(Interp *it, Node *blk, Env *env);
Value eval_node(Interp *it, Node *node, Env *env);
Value apply_func(Interp *it, Value fnv, Value *args, int nargs);
Value call_builtin(Interp *it, int id, Value *args, int n);
int builtin_id_of(Interp *it, const char *interned_name);
/* run a whole program; returns malloc'd error message or NULL */
char *interp_run_source(Interp *it, const char *src);
/* REPL: single expr → *echo (malloc'd display, free after print); else run.
 * returns malloc'd error or NULL. *echo is NULL when nothing to echo. */
char *interp_repl_eval(Interp *it, const char *src, char **echo);
/* clear user bindings in env (global scope) — returns how many were removed */
int env_clear(Env *e);
/* iterate env bindings: cb(name, value, ud). returns count. */
int env_each(Env *e, void (*cb)(const char *name, Value v, void *ud), void *ud);

/* difflib-style ratio on utf8 strings */
double seq_ratio(const char *a, const char *b);
const char *suggest_name(Interp *it, Env *env, const char *name);

/* ---------------------------------------------------------------- builtins */
Value builtin_dispatch(Interp *it, int id, Value *args, int n);

/* ---------------------------------------------------------------- main util */
char *attach_source_line(const char *msg, const char *src);
char *read_file_utf8(const char *path);                 /* NULL on failure */

#endif
