/* na_rt.c — minimal C runtime for 나/가나다 native (LLVM) binaries.
 * See na_rt.h for ownership and scope notes.
 */
#include "na_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

void na_print_i64(int64_t v) {
    printf("%" PRId64 "\n", v);
}

void na_print_cstr(const char *s) {
    if (s) fputs(s, stdout);
    fputc('\n', stdout);
}

char *na_str_concat(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t na = strlen(a), nb = strlen(b);
    char *out = malloc(na + nb + 1);
    if (!out) {
        fputs("na_rt: out of memory (str_concat)\n", stderr);
        abort();
    }
    memcpy(out, a, na);
    memcpy(out + na, b, nb);
    out[na + nb] = '\0';
    return out;
}

char *na_i64_to_str(int64_t v) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%" PRId64, v);
    if (n < 0) n = 0;
    char *out = malloc((size_t)n + 1);
    if (!out) {
        fputs("na_rt: out of memory (i64_to_str)\n", stderr);
        abort();
    }
    memcpy(out, buf, (size_t)n + 1);
    return out;
}

void na_print_val(NaVal v) {
    switch (v.tag) {
    case NA_NIL:   puts("없음"); break;
    case NA_BOOL:  puts(v.as.b ? "참" : "거짓"); break;
    case NA_INT:   na_print_i64(v.as.i); break;
    case NA_FLOAT: printf("%g\n", v.as.f); break;
    case NA_STR:   na_print_cstr(v.as.s); break;
    default:       puts("?"); break;
    }
}
