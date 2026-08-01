/* na_rt.h — minimal C runtime for 나/가나다 native (LLVM) binaries
 *
 * Linked into user programs produced by `가나다 llvm` / `가나다 컴파일`+clang.
 * Not part of the interpreter binary.
 *
 * Ownership:
 *   - na_str_concat / na_i64_to_str return malloc'd NUL-terminated strings.
 *     Caller owns them; no free is done by the runtime (no GC yet — short-lived
 *     CLI programs leak until process exit).
 *   - Print helpers write to stdout and do not take ownership of arguments.
 *
 * Full language (lists, dicts, web, db, AI, …) still requires the interpreter.
 */
#ifndef NA_RT_H
#define NA_RT_H

#include <stdint.h>

void na_print_i64(int64_t v);
void na_print_cstr(const char *s);          /* prints s + newline; NULL → empty line */
char *na_str_concat(const char *a, const char *b); /* malloc'd; NULL args treated as "" */
char *na_i64_to_str(int64_t v);             /* malloc'd decimal */

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
