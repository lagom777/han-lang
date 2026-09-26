/* na_rt.h — minimal C runtime for 나/가나다 native (LLVM) binaries
 *
 * Linked into user programs produced by `가나다 llvm` / `가나다 컴파일`+clang.
 * Not part of the interpreter binary.
 *
 * Ownership:
 *   - na_str_concat / na_i64_to_str / na_str_dup return malloc'd NUL-terminated
 *     strings. The caller owns them and releases them with na_str_free
 *     (codegen_llvm.c frees temporaries after use and a variable's old value on
 *     overwrite, so loops do not grow memory).
 *   - na_bool_to_str returns a static string — never free it.
 *   - Print helpers write to stdout and do not take ownership of arguments.
 *
 * Interpreter parity (codegen_llvm.c relies on these):
 *   - na_line mirrors the interpreter's cur_line (the generated code stores the
 *     line at every statement); na_depth mirrors its call depth.
 *   - na_fail prints "오류: [na_line행] msg" plus the source line exactly like
 *     main.c run_file, then exits 1. na_init hands it the program source.
 *
 * Full language (lists, dicts, web, db, AI, …) still requires the interpreter.
 */
#ifndef NA_RT_H
#define NA_RT_H

#include <stdint.h>

extern int64_t na_line;
extern int64_t na_depth;
void na_init(const char *src);
void na_fail(const char *msg) __attribute__((noreturn));

void na_write_i64(int64_t v);               /* no newline */
void na_write_cstr(const char *s);
void na_write_bool(int64_t b);              /* 참 / 거짓 */
void na_write_byte(int64_t c);
void na_print_i64(int64_t v);
void na_print_cstr(const char *s);          /* prints s + newline; NULL → empty line */
char *na_str_concat(const char *a, const char *b); /* malloc'd; NULL args treated as "" */
char *na_i64_to_str(int64_t v);             /* malloc'd decimal */
const char *na_bool_to_str(int64_t b);      /* static 참 / 거짓 */
char *na_str_dup(const char *s);            /* malloc'd copy */
void na_str_free(char *s);
int64_t na_str_eq(const char *a, const char *b);   /* 1 / 0 */
int64_t na_str_cmp(const char *a, const char *b);  /* -1 / 0 / 1 */

/* --- Value-tag foundation (next: lists/dicts lower to these, not pure IR) ---
 * Interpreter Value tags: NIL BOOL INT FLOAT STR LIST DICT FUNC.
 * Native path currently uses dual SSA types (i64 | i8*); NaVal is the stable
 * ABI target when codegen grows beyond dual slots. */
enum {
    NA_NIL = 0,
    NA_BOOL,
    NA_INT,
    NA_FLOAT,
    NA_STR
    /* NA_LIST / NA_DICT / NA_FUNC later — share layout with host when linked */
};

typedef struct NaVal {
    uint8_t tag;
    uint8_t _pad[7];
    union {
        int64_t i;
        double f;
        int b;
        char *s;            /* cstr; ownership TBD per call */
    } as;
} NaVal;

static inline NaVal na_v_nil(void) {
    NaVal v; v.tag = NA_NIL; v.as.i = 0; return v;
}
static inline NaVal na_v_int(int64_t i) {
    NaVal v; v.tag = NA_INT; v.as.i = i; return v;
}
static inline NaVal na_v_str(char *s) {
    NaVal v; v.tag = NA_STR; v.as.s = s; return v;
}

void na_print_val(NaVal v);     /* dispatch print by tag */

#endif /* NA_RT_H */
