/* main.c — CLI entry: ganada run <file> / ganada repl */
#include "ganada.h"
#include <unistd.h>
#include <libgen.h>

/* display width: East Asian W/F count as 2 */
static int ea_wide(uint32_t cp) {
    if (cp >= 0x1100 && cp <= 0x115F) return 1;
    if (cp >= 0x2E80 && cp <= 0x303E) return 1;
    if (cp >= 0x3041 && cp <= 0x33FF) return 1;
    if (cp >= 0x3400 && cp <= 0x4DBF) return 1;
    if (cp >= 0x4E00 && cp <= 0x9FFF) return 1;
    if (cp >= 0xA000 && cp <= 0xA4CF) return 1;
    if (cp >= 0xAC00 && cp <= 0xD7A3) return 1;
    if (cp >= 0xF900 && cp <= 0xFAFF) return 1;
    if (cp >= 0xFE10 && cp <= 0xFE19) return 1;
    if (cp >= 0xFE30 && cp <= 0xFE52) return 1;
    if (cp >= 0xFE54 && cp <= 0xFE66) return 1;
    if (cp >= 0xFE68 && cp <= 0xFE6B) return 1;
    if (cp >= 0xFF00 && cp <= 0xFF60) return 1;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return 1;
    if (cp >= 0x1F300 && cp <= 0x1F64F) return 1;
    if (cp >= 0x20000 && cp <= 0x3FFFD) return 1;
    return 0;
}

static int disp_width_cp(const char *s, size_t n) {
    int w = 0;
    size_t i = 0;
    while (i < n) {
        uint32_t adv;
        uint32_t cp = utf8_dec(s + i, &adv);
        w += ea_wide(cp) ? 2 : 1;
        i += adv;
    }
    return w;
}

/* is `s` ASCII digits, s[0]=='[' ... parse "[N" form; returns digit count or 0 */
static int parse_digits(const char *s, long *out) {
    int n = 0;
    long v = 0;
    while (s[n] >= '0' && s[n] <= '9') { v = v * 10 + (s[n] - '0'); n++; }
    if (n) *out = v;
    return n;
}

/* the 2 hangul bytes for haeng/yeol come from tables via ERR strings; we
 * match on literal UTF-8 constants reconstructed from tables.h macros.
 * HANG = haeng, YEOL = yeol — build them from STR_SSIK? no: use explicit
 * escaped constants (same bytes as in tables.h, verified by generator). */
#define HANGUL_HAENG "\xED\x96\x89"
#define HANGUL_YEOL "\xEC\x97\xB4"

/* attach failing source line (and optional caret) to error message */
char *attach_source_line(const char *msg, const char *src) {
    if (msg[0] != '[' || strchr(msg, '\n')) return strdup(msg);
    int j = 1;
    long line_no;
    int nd = parse_digits(msg + j, &line_no);
    if (!nd) return strdup(msg);
    j += nd;
    if (strncmp(msg + j, HANGUL_HAENG, strlen(HANGUL_HAENG))) return strdup(msg);
    j += (int)strlen(HANGUL_HAENG);
    /* optional column: spaces, digits, yeol */
    long col = -1;
    {
        int k = j;
        while (msg[k] == ' ') k++;
        long c;
        int nd2 = parse_digits(msg + k, &c);
        if (nd2 && !strncmp(msg + k + nd2, HANGUL_YEOL, strlen(HANGUL_YEOL)))
            col = c;
    }
    /* find line line_no (1-based) */
    const char *p = src;
    long cur = 1;
    const char *linestart = src;
    const char *lineend = NULL;
    while (*p) {
        if (cur == line_no) { linestart = p; break; }
        if (*p == '\n') cur++;
        p++;
    }
    if (cur != line_no) return strdup(msg);
    lineend = strchr(linestart, '\n');
    size_t llen = lineend ? (size_t)(lineend - linestart) : strlen(linestart);
    while (llen && (linestart[llen - 1] == ' ' || linestart[llen - 1] == '\t' || linestart[llen - 1] == '\r')) llen--;
    /* blank line -> no attachment */
    size_t k2 = 0;
    while (k2 < llen && (linestart[k2] == ' ' || linestart[k2] == '\t')) k2++;
    if (k2 == llen) return strdup(msg);

    size_t need = strlen(msg) + llen + 64;
    char *out = malloc(need);
    char *o = out;
    o += sprintf(o, "%s\n  %ld | ", msg, line_no);
    memcpy(o, linestart, llen); o += llen; *o = 0;
    if (col >= 1) {
        /* caret: only if col <= cplen(line)+1; compute cp byte offset */
        long cplen = 0;
        size_t i = 0;
        long target_byte = -1;
        while (i < llen) {
            uint32_t adv;
            utf8_dec(linestart + i, &adv);
            cplen++;
            if (cplen == col) { target_byte = (long)i; break; }
            i += adv;
        }
        if (col == cplen + 1) target_byte = (long)llen;
        if (target_byte >= 0) {
            int pad = 6 + disp_width_cp(linestart, (size_t)target_byte);   /* "  N | " varies with N digits */
            /* recompute prefix width exactly: 2 + digits + 3 */
            int digits = 1;
            for (long t = line_no; t >= 10; t /= 10) digits++;
            pad = 2 + digits + 3 + disp_width_cp(linestart, (size_t)target_byte);
            strcat(out, "\n");
            o = out + strlen(out);
            for (int s = 0; s < pad; s++) *o++ = ' ';
            *o++ = '^'; *o = 0;
        }
    }
    return out;
}

static char *abs_path(const char *path) {
    if (path[0] == '/') return strdup(path);
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) return strdup(path);
    size_t need = strlen(cwd) + strlen(path) + 2;
    char *out = malloc(need);
    snprintf(out, need, "%s/%s", cwd, path);
    return out;
}

static int run_file(const char *path) {
    char *src = read_file_utf8(path);
    if (!src) {
        fprintf(stderr, "%s: %s\n", STR_OORYU, path);
        return 1;
    }
    Interp *it = interp_new();
    char *ap = abs_path(path);
    char *dir = dirname(ap);
    it->base_dir = strdup(dir);
    char *err = interp_run_source(it, src);
    fflush(stdout);
    if (err) {
        char *shown = attach_source_line(err, src);
        fprintf(stderr, "%s: %s\n", STR_OORYU, shown);
        return 1;
    }
    return 0;
}

/* brace balance check for the repl (approximation of needs_more) */
static int needs_more(const char *src) {
    int depth = 0;
    int in_str = 0;
    for (const char *p = src; *p; p++) {
        if (in_str) {
            if (*p == '\\' && p[1]) { p++; continue; }
            if (*p == '"') in_str = 0;
            continue;
        }
        if (*p == '"') { in_str = 1; continue; }
        if (*p == '{') depth++;
        if (*p == '}') depth--;
    }
    return in_str || depth > 0;
}

/* ---- REPL meta commands (:도움 · :변수 · :기록 · :비우기) + expr echo ---- */
typedef struct { char **items; int n, cap; } Hist;

static void hist_push(Hist *h, const char *s) {
    if (h->n == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 16;
        h->items = realloc(h->items, sizeof(char *) * (size_t)h->cap);
    }
    h->items[h->n++] = strdup(s);
}

typedef struct { char *buf; size_t n, cap; } GBuf;
static void gb_init(GBuf *b) { b->buf = malloc(256); b->n = 0; b->cap = 256; b->buf[0] = 0; }
static void gb_put(GBuf *b, const char *s) {
    size_t L = strlen(s);
    if (b->n + L + 1 > b->cap) {
        while (b->n + L + 1 > b->cap) b->cap *= 2;
        b->buf = realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->n, s, L + 1);
    b->n += L;
}

static int qsort_cstr(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void cmd_help(void) {
    printf("%s\n%s", STR_HELP_LINE, STR_BUILTINS_LABEL);
    const char **names = malloc(sizeof(char *) * B_COUNT);
    for (int i = 0; i < B_COUNT; i++) names[i] = BUILTIN_NAMES[i];
    qsort(names, B_COUNT, sizeof(char *), qsort_cstr);
    for (int i = 0; i < B_COUNT; i++) {
        if (i) printf(", ");
        printf("%s", names[i]);
    }
    printf("\n");
    free(names);
}

typedef struct { GBuf *out; int first; } VarDump;
static void var_cb(const char *name, Value v, void *ud) {
    VarDump *d = ud;
    if (!d->first) gb_put(d->out, "\n");
    d->first = 0;
    gb_put(d->out, name);
    gb_put(d->out, " = ");
    Str *s = v_stringify(v);
    if (s->len <= 50) {
        gb_put(d->out, s->data);
    } else {
        char tmp[64];
        memcpy(tmp, s->data, 50);
        tmp[50] = 0;
        gb_put(d->out, tmp);
        gb_put(d->out, "\xE2\x80\xA6"); /* … */
    }
}

static void cmd_vars(Interp *it) {
    /* stringify may throw on cycle — catch so :변수 never aborts the REPL */
    Handler h;
    h.prev = it->top; it->top = &h;
    int code = _setjmp(h.jb);
    if (code == 0) {
        GBuf b; gb_init(&b);
        VarDump d = { &b, 1 };
        int n = env_each(it->g, var_cb, &d);
        if (n == 0) printf("%s\n", STR_NO_VARS);
        else printf("%s\n", b.buf);
        free(b.buf);
        it->top = h.prev;
    } else {
        it->top = h.prev;
        if (code == GS_ERR && it->err) {
            printf("%s: %s\n", STR_OORYU, it->err);
            free(it->err); it->err = NULL;
        }
    }
}

static void cmd_hist(Hist *h) {
    if (!h->n) { printf("%s\n", STR_NO_HIST); return; }
    for (int i = 0; i < h->n; i++)
        printf("%d. %s\n", i + 1, h->items[i]);
}

/* 1 = handled (including quit via *quit=1), 0 = not a meta command */
static int repl_command(const char *line, Interp *it, Hist *hist, int *quit) {
    if (line[0] != ':') return 0;
    if (!strcmp(line, ":quit") || !strcmp(line, STR_CMD_QUIT) || !strcmp(line, STR_CMD_END)) {
        *quit = 1;
        return 1;
    }
    if (!strcmp(line, STR_CMD_HELP)) { cmd_help(); return 1; }
    if (!strcmp(line, STR_CMD_VARS) || !strcmp(line, STR_CMD_ENV)) { cmd_vars(it); return 1; }
    if (!strcmp(line, STR_CMD_HIST)) { cmd_hist(hist); return 1; }
    if (!strcmp(line, STR_CMD_CLEAR) || !strcmp(line, STR_CMD_RESET)) {
        int n = env_clear(it->g);
        printf(STR_CLEARED_FMT "\n", n);
        return 1;
    }
    printf(STR_UNKNOWN_CMD_FMT "\n", line);
    return 1;
}

static void repl(void) {
#ifdef __has_include
#  if __has_include(<readline/readline.h>)
#    define GANADA_HAS_READLINE 1
#  endif
#endif
#ifdef GANADA_HAS_READLINE
    /* optional — not linked by default; fgets path is the portable baseline */
#endif
    printf("%s\n", STR_BANNER);
    Interp *it = interp_new();
    Hist hist = {0};
    char line[4096];
    char buf[65536];
    buf[0] = 0;
    for (;;) {
        printf("%s", buf[0] ? "... " : STR_PROMPT);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) { printf("\n"); break; }
        size_t ll = strlen(line);
        while (ll && (line[ll - 1] == '\n' || line[ll - 1] == '\r')) line[--ll] = 0;
        if (!buf[0]) {
            int quit = 0;
            if (repl_command(line, it, &hist, &quit)) {
                if (quit) break;
                continue;
            }
            if (!ll) continue;
        }
        if (buf[0]) strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
        if (needs_more(buf)) continue;
        hist_push(&hist, buf);
        char *echo = NULL;
        char *err = interp_repl_eval(it, buf, &echo);
        if (err) {
            char *shown = attach_source_line(err, buf);
            printf("%s: %s\n", STR_OORYU, shown);
            free(err);
        } else if (echo) {
            printf("%s\n", echo);
            free(echo);
        }
        buf[0] = 0;
    }
    for (int i = 0; i < hist.n; i++) free(hist.items[i]);
    free(hist.items);
}

/* CLI 한글 명령 (UTF-8 바이트) */
#define CMD_COMPILE_KO "\xEC\xBB\xB4\xED\x8C\x8C\xEC\x9D\xBC"   /* 컴파일 */
#define CMD_BUILD_KO   "\xEB\xB9\x8C\xEB\x93\x9C"               /* 빌드 */
#define CMD_NA_KO      "\xEB\x82\x98"                           /* 나 */
#define CMD_DOLRIGI_KO "\xEB\x8F\x8C\xEB\xA6\xAC\xEA\xB8\xB0"   /* 돌리기 */
#define CMD_RUN_KO     "\xEC\x8B\xA4\xED\x96\x89"               /* 실행 */
#define CMD_REPL_KO    "\xEB\x8C\x80\xED\x99\x94"               /* 대화 */

/* Path to na_rt.c for linking native user programs (not into ganada itself). */
static char g_argv0[4096];

static int find_na_rt(char *out, size_t outsz) {
    /* 1) <dir(argv0)>/src/na_rt.c  (dev: ganada-c/ganada → ganada-c/src/na_rt.c) */
    if (g_argv0[0]) {
        char resolved[4096];
        if (realpath(g_argv0, resolved)) {
            char *slash = strrchr(resolved, '/');
            if (slash) {
                *slash = 0;
                snprintf(out, outsz, "%s/src/na_rt.c", resolved);
                if (access(out, R_OK) == 0) return 0;
            }
        }
        char tmp[4096];
        snprintf(tmp, sizeof tmp, "%s", g_argv0);
        char *slash = strrchr(tmp, '/');
        if (slash) {
            *slash = 0;
            snprintf(out, outsz, "%s/src/na_rt.c", tmp);
            if (access(out, R_OK) == 0) return 0;
        }
    }
    /* 2) cwd-relative fallbacks */
    static const char *cands[] = { "src/na_rt.c", "ganada-c/src/na_rt.c", NULL };
    for (int i = 0; cands[i]; i++) {
        if (access(cands[i], R_OK) == 0) {
            snprintf(out, outsz, "%s", cands[i]);
            return 0;
        }
    }
    return -1;
}

/* LLVM 경로 모드:
 *   IR=IR만 / BUILD=바이너리 / RUN=바이너리+실행(실패 시 하드 에러)
 *   TRY_RUN=실행 경로용: emit/clang 실패 시 2 반환 → 호출측이 인터프리터로 폴백
 * 반환: 0 성공, 1 하드 실패, 2 soft(TRY_RUN only · 폴백 가능) */
enum { LLVM_IR = 0, LLVM_BUILD = 1, LLVM_RUN = 2, LLVM_TRY_RUN = 3 };

static int compile_llvm(const char *path, int mode) {
    int try_mode = (mode == LLVM_TRY_RUN);
    char *src = read_file_utf8(path);
    if (!src) {
        fprintf(stderr, "파일을 열 수 없습니다: %s\n", path);
        return 1;
    }
    Interp *it = interp_new();
    char *err = NULL;
    Node *root = NULL;
    /* parse via run path internals: lex + parse with error capture */
    Handler h;
    h.prev = it->top; it->top = &h;
    int code = _setjmp(h.jb);
    if (code == 0) {
        int ntoks = 0;
        Tok *toks = lex_all(it, src, &ntoks);
        root = parse_all(it, toks);
        it->top = h.prev;
    } else {
        it->top = h.prev;
        if (it->err) {
            char *shown = attach_source_line(it->err, src);
            fprintf(stderr, "오류: %s\n", shown);
            free(it->err); it->err = NULL;
        }
        free(src);
        return 1; /* 구문 오류 — 인터프리터도 동일 실패 */
    }
    char *ir = llvm_emit_module(it, root, &err);
    if (!ir) {
        if (try_mode) {
            free(err);
            free(src);
            return 2; /* 미지원 기능 → 폴백 */
        }
        fprintf(stderr, "LLVM: %s\n", err ? err : "코드 생성 실패");
        free(err);
        free(src);
        return 1;
    }

    char outll[4096];
    char outbin[4096];
    if (try_mode) {
        /* 실행 폴백 경로: 소스 옆 파일을 오염시키지 않도록 임시 경로 */
        snprintf(outll, sizeof outll, "/tmp/ganada_run_%d.ll", (int)getpid());
        snprintf(outbin, sizeof outbin, "/tmp/ganada_run_%d.native", (int)getpid());
    } else {
        snprintf(outll, sizeof outll, "%s.ll", path);
        snprintf(outbin, sizeof outbin, "%s.native", path);
    }
    FILE *f = fopen(outll, "w");
    if (!f) {
        if (try_mode) {
            free(ir); free(src);
            return 2;
        }
        fprintf(stderr, "쓸 수 없음: %s\n", outll);
        free(ir); free(src);
        return 1;
    }
    fputs(ir, f);
    fclose(f);
    if (!try_mode)
        fprintf(stderr, "LLVM IR → %s\n", outll);

    if (mode == LLVM_IR) {
        free(ir);
        free(src);
        return 0;
    }

    /* 빌드/나/TRY: clang links IR + na_rt.c (prints, string concat). */
    char nart[4096];
    if (find_na_rt(nart, sizeof nart) != 0) {
        if (try_mode) {
            unlink(outll);
            free(ir); free(src);
            return 2;
        }
        fprintf(stderr, "LLVM: na_rt.c 를 찾을 수 없습니다 (ganada-c/src/na_rt.c)\n");
        free(ir); free(src);
        return 1;
    }
    char cmd[12288];
    /* TRY: clang 진단은 삼키고, 실패 시 인터프리터로 조용히 폴백 */
    if (try_mode)
        snprintf(cmd, sizeof cmd, "clang -O2 -o %s %s %s >/dev/null 2>&1", outbin, outll, nart);
    else {
        snprintf(cmd, sizeof cmd, "clang -O2 -o %s %s %s 2>&1", outbin, outll, nart);
        fprintf(stderr, "clang → %s (+ na_rt)\n", outbin);
    }
    int rc = system(cmd);
    if (rc != 0) {
        if (try_mode) {
            unlink(outll);
            unlink(outbin);
            free(ir); free(src);
            return 2;
        }
        fprintf(stderr, "clang 링크 실패 (rc=%d)\n", rc);
        free(ir); free(src);
        return 1;
    }
    if (mode == LLVM_BUILD) {
        fprintf(stderr, "네이티브 바이너리 → %s\n", outbin);
        free(ir); free(src);
        return 0;
    }
    /* LLVM_RUN / LLVM_TRY_RUN: 바로 실행 (프로그램 종료코드는 폴백 사유 아님) */
    snprintf(cmd, sizeof cmd, "%s", outbin);
    rc = system(cmd);
    if (try_mode) {
        unlink(outll);
        unlink(outbin);
    }
    free(ir); free(src);
    return rc ? 1 : 0;
}

/* 실행/run: 네이티브 시도 → 실패 시 인터프리터. env 탈출구 지원. */
static int run_smart(const char *path) {
    const char *fi = getenv("GANADA_FORCE_INTERP");
    const char *fl = getenv("GANADA_FORCE_LLVM");
    if (fi && fi[0] == '1' && fi[1] == '\0')
        return run_file(path);
    if (fl && fl[0] == '1' && fl[1] == '\0')
        return compile_llvm(path, LLVM_RUN); /* 네이티브만, 실패 시 하드 에러 */
    int rc = compile_llvm(path, LLVM_TRY_RUN);
    if (rc == 2) {
        fprintf(stderr, "LLVM 미지원 → 인터프리터\n");
        return run_file(path);
    }
    return rc;
}

int main(int argc, char **argv) {
    gc_init(__builtin_frame_address(0));    /* high end of the stack to scan */
    if (argv[0]) {
        strncpy(g_argv0, argv[0], sizeof g_argv0 - 1);
        g_argv0[sizeof g_argv0 - 1] = 0;
    }
    /* 실행/run — 네이티브 우선, 미지원 시 인터프리터 폴백 */
    if (argc >= 3 && (!strcmp(argv[1], "run") || !strcmp(argv[1], CMD_RUN_KO)))
        return run_smart(argv[2]);
    /* 나(LLVM) 경로 — 명시적: 빌드=바이너리, 나/llvm/돌리기=빌드+실행, 컴파일=IR만 (폴백 없음) */
    if (argc >= 3 && (!strcmp(argv[1], "compile") || !strcmp(argv[1], CMD_COMPILE_KO)))
        return compile_llvm(argv[2], LLVM_IR);
    if (argc >= 3 && (!strcmp(argv[1], "build") || !strcmp(argv[1], CMD_BUILD_KO)))
        return compile_llvm(argv[2], LLVM_BUILD);
    if (argc >= 3 && (!strcmp(argv[1], "llvm") || !strcmp(argv[1], CMD_NA_KO)
                      || !strcmp(argv[1], CMD_DOLRIGI_KO)))
        return compile_llvm(argv[2], LLVM_RUN);
    if (argc == 1 || (argc == 2 && (!strcmp(argv[1], "repl") || !strcmp(argv[1], CMD_REPL_KO)))) {
        repl();
        return 0;
    }
    fprintf(stderr, "%s\n", STR_USAGE);
    fprintf(stderr, "\n권장(네이티브 · 나 경로, clang 필요):\n");
    fprintf(stderr, "  가나다 빌드  파일.ㄱㄴㄷ   → LLVM IR + 네이티브 바이너리 (.native)\n");
    fprintf(stderr, "  가나다 나    파일.ㄱㄴㄷ   → 빌드 후 바로 실행 (별칭: llvm, 돌리기)\n");
    fprintf(stderr, "  가나다 컴파일 파일.ㄱㄴㄷ  → LLVM IR (.ll) 만\n");
    fprintf(stderr, "\n실행(네이티브 우선 · 폴백):\n");
    fprintf(stderr, "  가나다 실행  파일.ㄱㄴㄷ   → LLVM 시도, 미지원 시 인터프리터 (별칭: run)\n");
    fprintf(stderr, "  가나다 대화               → REPL\n");
    fprintf(stderr, "  GANADA_FORCE_INTERP=1     → 항상 인터프리터\n");
    fprintf(stderr, "  GANADA_FORCE_LLVM=1       → 네이티브만 (실패 시 오류)\n");
    return 2;
}
