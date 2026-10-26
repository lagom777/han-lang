/* lexer.c — tokenizer (utf8 aware) */
#include "ganada.h"
#include <ctype.h>
#include <errno.h>

const char LIT_2_63[] = "9223372036854775808";

static int is_id_start_cp(uint32_t cp) {
    if (cp == '_' || (cp < 0x80 && isalpha((int)cp))) return 1;
    if (cp < 0x80) return 0;
    /* non-ascii: letters (hangul, cjk...) — exclude spaces/punctuation */
    if (cp == 0x3000) return 0;                       /* ideographic space */
    if (cp >= 0x2000 && cp <= 0x206F) return 0;       /* general punctuation */
    if (cp >= 0x3001 && cp <= 0x303F) return 0;       /* cjk punctuation */
    if (cp >= 0xFF00 && cp <= 0xFF65) return 0;       /* fullwidth forms */
    if (cp >= 0x2028 && cp <= 0x2029) return 0;
    return 1;
}

static int is_id_char_cp(uint32_t cp) {
    if (cp == '_' || (cp < 0x80 && isalnum((int)cp))) return 1;
    return is_id_start_cp(cp);
}

/* column number in codepoints (matches the python implementation) */
static int cp_col(const char *bol, size_t off) {
    int col = 1;
    size_t i = 0;
    while (i < off) {
        uint32_t adv;
        utf8_dec(bol + i, &adv);
        i += adv;
        col++;
    }
    return col;
}

Tok *lex_all(Interp *it, const char *src, int *ntoks) {
    int cap = 256, n = 0;
    Tok *toks = malloc(sizeof(Tok) * cap);
    size_t i = 0, len = strlen(src);
    int line = 1;
    size_t bol = 0;

#define PUSH(k, sv) do { \
        if (n == cap) { cap *= 2; toks = realloc(toks, sizeof(Tok) * cap); } \
        toks[n].kind = (k); toks[n].s = (sv); toks[n].line = line; \
        toks[n].col = cp_col(src + bol, i - bol); n++; \
    } while (0)

    while (i < len) {
        char ch = src[i];
        if (ch == '\n') { line++; i++; bol = i; continue; }
        if (ch == ' ' || ch == '\t' || ch == '\r') { i++; continue; }
        if (ch == '#') { while (i < len && src[i] != '\n') i++; continue; }
        if (ch == '"') {
            int tline = line, tcol = cp_col(src + bol, i - bol);
            i++;
            size_t bcap = 32, bn = 0;
            char *buf = malloc(bcap);

            while (i < len && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < len) {
                    char e = src[i + 1], r;
                    switch (e) {
                    case 'n': r = '\n'; break;
                    case 't': r = '\t'; break;
                    case '"': r = '"'; break;
                    case '\\': r = '\\'; break;
                    default: r = e; break;
                    }
                    if (bn + 1 > bcap) { bcap *= 2; buf = realloc(buf, bcap); }
                    buf[bn++] = r; i += 2;
                } else {
                    if (src[i] == '\n') { line++; bol = i + 1; }
                    if (bn + 1 > bcap) { bcap *= 2; buf = realloc(buf, bcap); }
                    buf[bn++] = src[i]; i++;
                }
            }
            if (i >= len) {
                free(buf); free(toks);
                g_error(it, ERR_UNTERMINATED_STR, tline, tcol);
            }
            i++;
            if (n == cap) { cap *= 2; toks = realloc(toks, sizeof(Tok) * cap); }
            buf = realloc(buf, bn + 1); buf[bn] = 0;
            toks[n].kind = T_STR; toks[n].s = buf;
            toks[n].line = tline; toks[n].col = tcol; n++;
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            size_t j = i;
            int dot = 0;
            while (j < len && ((src[j] >= '0' && src[j] <= '9') || (src[j] == '.' && !dot))) {
                if (src[j] == '.') dot = 1;
                j++;
            }
            if (n == cap) { cap *= 2; toks = realloc(toks, sizeof(Tok) * cap); }
            /* 긴 리터럴도 자르지 않는다 — 63자에서 잘라 100자리 실수가 1e62 가 됐다 */
            size_t tl = j - i;
            char sbuf[64], *tmp = tl < sizeof sbuf ? sbuf : xmalloc(tl + 1);
            memcpy(tmp, src + i, tl); tmp[tl] = 0;
            toks[n].kind = T_NUM; toks[n].s = NULL;
            toks[n].line = line; toks[n].col = cp_col(src + bol, i - bol);
            if (dot) { toks[n].num.tag = VT_FLOAT; toks[n].num.as.f = strtod(tmp, NULL); }
            else {
                /* 64비트를 넘는 정수: strtoll 은 끝값으로 붙여서 조용히 틀린 수가 됐다 */
                errno = 0;
                toks[n].num.tag = VT_INT; toks[n].num.as.i = strtoll(tmp, NULL, 10);
                if (errno == ERANGE) {
                    const char *d = tmp;
                    while (*d == '0' && d[1]) d++;
                    if (strcmp(d, LIT_2_63)) {
                        if (tmp != sbuf) free(tmp);
                        g_error(it, ERR_INT_LITERAL, toks[n].line, toks[n].col);
                    }
                    toks[n].num.as.i = INT64_MIN; toks[n].s = LIT_2_63;
                }
            }
            if (tmp != sbuf) free(tmp);
            n++;
            i = j; continue;
        }
        /* identifier / keyword (utf8) */
        uint32_t adv;
        uint32_t cp = utf8_dec(src + i, &adv);
        if (ch == '_' || is_id_start_cp(cp)) {
            size_t j = i;
            while (j < len) {
                uint32_t a2;
                uint32_t c2 = utf8_dec(src + j, &a2);
                if (c2 < 0x80 && !isalnum((int)c2) && c2 != '_') break;
                if (c2 >= 0x80 && !is_id_char_cp(c2)) break;
                j += a2;
            }
            const char *w = intern(it, src + i, (uint32_t)(j - i));
            int is_kw = 0;
            for (int k = 0; k < NUM_KEYWORDS; k++)
                if (it->kw_ptr[k] == w) { is_kw = 1; break; }
            PUSH(is_kw ? T_KW : T_ID, w);
            i = j; continue;
        }
        /* operators, longest first */
        static const char *OPS[] = {
            "==", "!=", "<=", ">=", "+=", "-=", "*=", "/=",
            "+", "-", "*", "/", "%", "=", "<", ">", "(", ")", "{", "}",
            "[", "]", ":", ","
        };
        int matched = 0;
        for (size_t k = 0; k < sizeof(OPS) / sizeof(OPS[0]); k++) {
            size_t ol = strlen(OPS[k]);
            if (i + ol <= len && memcmp(src + i, OPS[k], ol) == 0) {
                PUSH(T_OP, intern(it, OPS[k], (uint32_t)ol));
                i += ol; matched = 1; break;
            }
        }
        if (matched) continue;
        {
            int tline = line, tcol = cp_col(src + bol, i - bol);
            char bad[8]; uint32_t a3;
            utf8_dec(src + i, &a3);
            memcpy(bad, src + i, a3); bad[a3] = 0;
            free(toks);
            g_error(it, ERR_UNKNOWN_CHAR, tline, tcol, bad);
        }
    }
    if (n == cap) { cap *= 2; toks = realloc(toks, sizeof(Tok) * cap); }
    toks[n].kind = T_EOF; toks[n].s = NULL;
    toks[n].line = line; toks[n].col = cp_col(src + bol, i - bol); n++;
    *ntoks = n;
    return toks;
#undef PUSH
}
