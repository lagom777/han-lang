/* na_rt.c — minimal C runtime for 나/가나다 native (LLVM) binaries.
 * See na_rt.h for ownership and scope notes.
 */
#include "na_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define NA_OORYU "\xEC\x98\xA4\xEB\xA5\x98"   /* 오류 */
#define NA_HAENG "\xED\x96\x89"               /* 행 */
#define NA_CHAM  "\xEC\xB0\xB8"               /* 참 */
#define NA_GEOJIS "\xEA\xB1\xB0\xEC\xA7\x93"  /* 거짓 */

int64_t na_line;
int64_t na_depth;
static const char *na_src;

static void na_oom(const char *where) {
    fprintf(stderr, "na_rt: out of memory (%s)\n", where);
    abort();
}

void na_init(const char *src) {
    na_src = src;
}

/* 인터프리터 run_file 과 같은 모양으로 멈춘다:
 *   finish_err 가 "[cur_line행] " 을 붙이고, attach_source_line 이 그 줄을 붙이고, "오류: …" 로 찍는다. */
void na_fail(const char *msg) {
    fflush(stdout);
    long line_no = (long)na_line;
    fprintf(stderr, "%s: [%ld%s] %s", NA_OORYU, line_no, NA_HAENG, msg);
    const char *src = na_src;
    if (src) {
        /* main.c attach_source_line 의 줄 찾기를 그대로 옮겼다 */
        const char *p = src;
        long cur = 1;
        const char *linestart = src;
        while (*p) {
            if (cur == line_no) { linestart = p; break; }
            if (*p == '\n') cur++;
            p++;
        }
        if (cur == line_no) {
            const char *lineend = strchr(linestart, '\n');
            size_t llen = lineend ? (size_t)(lineend - linestart) : strlen(linestart);
            while (llen && (linestart[llen - 1] == ' ' || linestart[llen - 1] == '\t' || linestart[llen - 1] == '\r'))
                llen--;
            size_t k = 0;
            while (k < llen && (linestart[k] == ' ' || linestart[k] == '\t')) k++;
            if (k != llen) fprintf(stderr, "\n  %ld | %.*s", line_no, (int)llen, linestart);
        }
    }
    fputc('\n', stderr);
    exit(1);
}

void na_write_i64(int64_t v) {
    printf("%" PRId64, v);
}

void na_write_cstr(const char *s) {
    fputs(s, stdout);
}

void na_write_bool(int64_t b) {
    fputs(b ? NA_CHAM : NA_GEOJIS, stdout);
}

void na_write_byte(int64_t c) {
    putchar((int)c);
}

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
    if (!out) na_oom("str_concat");
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
    if (!out) na_oom("i64_to_str");
    memcpy(out, buf, (size_t)n + 1);
    return out;
}

const char *na_bool_to_str(int64_t b) {
    return b ? NA_CHAM : NA_GEOJIS;
}

char *na_str_dup(const char *s) {
    size_t n = strlen(s);
    char *out = malloc(n + 1);
    if (!out) na_oom("str_dup");
    memcpy(out, s, n + 1);
    return out;
}

void na_str_free(char *s) {
    free(s);
}

int64_t na_str_eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

int64_t na_str_cmp(const char *a, const char *b) {
    int c = strcmp(a, b);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

void na_print_val(NaVal v) {
    switch (v.tag) {
    case NA_NIL:   puts("\xEC\x97\x86\xEC\x9D\x8C"); break;   /* 없음 */
    case NA_BOOL:  puts(v.as.b ? NA_CHAM : NA_GEOJIS); break;
    case NA_INT:   na_print_i64(v.as.i); break;
    case NA_FLOAT: printf("%g\n", v.as.f); break;
    case NA_STR:   na_print_cstr(v.as.s); break;
    default:       puts("?"); break;
    }
}
