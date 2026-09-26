/* codegen_llvm.c — 가나다 부분집합 → LLVM IR (clang 이 na_rt.c 와 함께 네이티브로 묶는다)
 *
 * `실행` 은 여기서 먼저 네이티브를 시도하고, 거절되면 인터프리터로 간다(main.c run_smart).
 * 그래서 규칙은 하나다: 받아들인 프로그램은 인터프리터와 표준출력·오류 문구·종료코드가 같아야 한다.
 * 확신이 없는 것은 전부 거절한다 — 인터프리터로 가면 느릴 뿐 틀리지 않는다.
 *
 * 받는 것
 *   값   정수(i64) · 참/거짓(i64 0/1, 찍으면 참/거짓) · 문자열(NUL 끝 ptr)
 *   식   + - * % · 비교 · 그리고/또는(단락, 값은 피연산자 그대로) · 아니다 · 단항 - ·
 *        문자열 + 무엇이든(정수·참거짓은 글로) · 문자열 비교
 *   문장 대입(+= -= *=) · 만약/아니면 · 동안 · 반복 부터..까지[..씩] · 멈춤 · 계속 · 반환 ·
 *        출력(...) · 최상위 함수(정수 인자, 기본값 없음)
 * 거절하는 것 (→ 인터프리터)
 *   / (결과가 늘 실수) · 실수·없음 리터럴 · 목록·사전·람다·색인 · 가져오기·시도·순회 · 출력 외 내장 ·
 *   정의되지 않았을 수 있는 이름 · 함수 안에서 전역일 수 있는 이름 · 함수 이름과 겹치는 변수 ·
 *   정의보다 먼저 부르는 함수 · 값이 없을 수 있는 함수의 값 · 형이 바뀌는 변수
 *
 * 인터프리터를 그대로 따르는 곳 (interp.c 와 같이 읽을 것)
 *   - 반복 변수는 반복마다 새 칸이다: 몸에서 바꿔도 다음 값은 숨은 계수기에서 오고, 반복 뒤엔 없다
 *   - 스텝 부호로 끝을 비교한다. 스텝 0 · % 0 · 호출 깊이 700 초과는 인터프리터와 같은 오류 문구
 *   - 오류 줄은 cur_line 규칙 그대로다: 문장마다 @na_line 에 쓰고, 되돌리지 않는다
 *   - < > <= >= 는 double 로 비교한다(v_cmp). % 는 나누는 수의 부호를 따른다(파이썬식)
 *   - 대입은 가장 안쪽 묶음을 바꾼다. 함수 안의 대입이 전역을 바꿀 수 있으면 거절한다
 *
 * 문자열 소유: 리터럴·변수 읽기는 빌린 값, 잇기·글바꿈 결과는 소유 값이다. 소유 값은 다 쓰면
 * 풀고, 변수에 넣을 땐 빌린 값을 복사해 변수가 늘 제 문자열을 갖는다(덮어쓸 때 옛것을 푼다).
 * 반복문 안의 잇기가 메모리를 끝없이 먹지 않게 하려는 것 — 인터프리터는 GC 가 치운다.
 */
#include "ganada.h"
#include <stdarg.h>

enum { TY_NONE = 0, TY_INT, TY_BOOL, TY_STR };

/* 식의 결과: t = SSA 이름 %tN (-1 = 실패), ty = 형, own = 우리가 풀어야 하는 문자열인가 */
typedef struct { int t, ty, own; } CgV;
#define CG_BAD ((CgV){ -1, TY_NONE, 0 })

typedef struct { char *p; size_t n, cap; } Buf;

/* 칸: 이름 하나의 저장소. alloca 두 개(%vN i64, %pN ptr)를 늘 같이 잡고 형에 맞는 쪽만 쓴다. */
typedef struct {
    const char *name;   /* 인턴 포인터. 숨은 칸(반복 계수기)은 NULL */
    int ty;             /* TY_NONE = 아직 대입 전 */
    int fnlevel;        /* 이름으로 찾는 함수 수준 칸. 반복 변수 칸은 0(묶음으로만 찾는다) */
    int param;
} Slot;

typedef struct { const char *name; int slot; } Bind;   /* 반복 변수가 바깥 이름을 가림 */

typedef struct {
    const char *name;
    Node *node;
    int stmt;           /* 정의가 있는 최상위 문장 번호 */
    int ret_int;        /* 1 = 모든 길이 `반환 식`(식은 정수여야 함). 0 = 값이 없을 수 있다 */
} Fn;

typedef struct {
    Interp *it;
    char *err;
    Buf body, entry;    /* 지금 함수의 몸 / entry 블록(alloca) */
    int tmp, lab, term;
    Slot *slots; unsigned char *defd; int nslots, capslots;   /* defd: 확실히 대입된 칸 */
    Bind *binds; int nbinds, capbinds;
    int brk[64], cont[64], depth;
    Fn *fns; int nfns;
    Fn *cur;            /* NULL = 최상위(main) */
    int ret_lab, ret_slot;
    const char **globals; int nglobals, capglobals;
} CG;

/* ---------------------------------------------------------------- 버퍼·오류 */
static void buf_put(Buf *b, const char *s, size_t L) {
    if (b->n + L + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->n + L + 1) nc *= 2;
        b->p = realloc(b->p, nc); b->cap = nc;
    }
    memcpy(b->p + b->n, s, L); b->n += L; b->p[b->n] = 0;
}
static void buf_printf(Buf *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt); int L = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (L < 0) return;
    if ((size_t)L < sizeof tmp) { buf_put(b, tmp, (size_t)L); return; }
    char *big = malloc((size_t)L + 1);
    va_start(ap, fmt); vsnprintf(big, (size_t)L + 1, fmt, ap); va_end(ap);
    buf_put(b, big, (size_t)L);
    free(big);
}
#define E(...) buf_printf(&g->body, __VA_ARGS__)

static void cg_err(CG *g, const char *fmt, ...) {
    if (g->err) return;
    char tmp[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    g->err = strdup(tmp);
}

static int newtmp(CG *g) { return g->tmp++; }
static int newlab(CG *g) { return g->lab++; }
static void label(CG *g, int L) { E("L%d:\n", L); g->term = 0; }
static void br(CG *g, int L) {
    if (g->term) return;
    E("  br label %%L%d\n", L); g->term = 1;
}
static void condbr(CG *g, int c, int Lt, int Lf) {
    if (g->term) return;
    E("  br i1 %%t%d, label %%L%d, label %%L%d\n", c, Lt, Lf); g->term = 1;
}

/* 사용자 함수 기호: libc·런타임 이름(exit, main, na_…)과 겹치지 않게 접두를 붙인다 */
static void fn_sym(Buf *b, const char *name) {
    buf_put(b, "@\"ganada.", 9);
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p >= 32 && *p < 127 && *p != '"' && *p != '\\') buf_put(b, (const char *)p, 1);
        else buf_printf(b, "\\%02X", *p);
    }
    buf_put(b, "\"", 1);
}

/* ---------------------------------------------------------------- 문자열 상수 (모듈 전체) */
typedef struct { const char *data; uint32_t len; } StrG;
static StrG *sg; static int nsg, capsg;

static int intern_str(const char *data, uint32_t len) {
    for (int i = 0; i < nsg; i++)
        if (sg[i].len == len && memcmp(sg[i].data, data, len) == 0) return i;
    if (nsg == capsg) { capsg = capsg ? capsg * 2 : 8; sg = realloc(sg, sizeof(StrG) * (size_t)capsg); }
    sg[nsg] = (StrG){ data, len };
    return nsg++;
}
static int str_const(CG *g, const char *data, uint32_t len) {
    int sid = intern_str(data, len), t = newtmp(g);
    E("  %%t%d = getelementptr inbounds [%u x i8], ptr @.s%d, i64 0, i64 0\n", t, len + 1, sid);
    return t;
}
static void emit_str_globals(Buf *b) {
    for (int i = 0; i < nsg; i++) {
        buf_printf(b, "@.s%d = private unnamed_addr constant [%u x i8] c\"", i, sg[i].len + 1);
        for (uint32_t k = 0; k < sg[i].len; k++) {
            unsigned char c = (unsigned char)sg[i].data[k];
            if (c >= 32 && c < 127 && c != '"' && c != '\\') buf_put(b, (const char *)&c, 1);
            else buf_printf(b, "\\%02X", c);
        }
        buf_put(b, "\\00\"\n", 5);
    }
}

/* 인터프리터와 같은 문구로 멈춘다: na_fail 이 "[줄행] 문구" 와 소스 줄을 붙여 찍고 종료 1 */
static void emit_fail(CG *g, const char *msg) {
    int t = str_const(g, msg, (uint32_t)strlen(msg));
    E("  call void @na_fail(ptr %%t%d)\n  unreachable\n", t);
    g->term = 1;
}

/* ---------------------------------------------------------------- 칸·묶음·확실한 대입 */
static int new_slot(CG *g, const char *name, int ty, int fnlevel) {
    if (g->nslots == g->capslots) {
        g->capslots = g->capslots ? g->capslots * 2 : 16;
        g->slots = realloc(g->slots, sizeof(Slot) * (size_t)g->capslots);
        g->defd = realloc(g->defd, (size_t)g->capslots);
    }
    int s = g->nslots++;
    g->slots[s] = (Slot){ name, ty, fnlevel, 0 };
    g->defd[s] = 0;
    buf_printf(&g->entry, "  %%v%d = alloca i64, align 8\n  store i64 0, ptr %%v%d, align 8\n"
                          "  %%p%d = alloca ptr, align 8\n  store ptr null, ptr %%p%d, align 8\n",
               s, s, s, s);
    return s;
}

/* 이름 → 칸: 가장 안쪽 반복 변수부터, 없으면 함수 수준 이름 */
static int lookup(CG *g, const char *name) {
    for (int i = g->nbinds - 1; i >= 0; i--)
        if (g->binds[i].name == name) return g->binds[i].slot;
    for (int i = 0; i < g->nslots; i++)
        if (g->slots[i].fnlevel && g->slots[i].name == name) return i;
    return -1;
}

static void push_bind(CG *g, const char *name, int slot) {
    if (g->nbinds == g->capbinds) {
        g->capbinds = g->capbinds ? g->capbinds * 2 : 8;
        g->binds = realloc(g->binds, sizeof(Bind) * (size_t)g->capbinds);
    }
    g->binds[g->nbinds++] = (Bind){ name, slot };
}

typedef struct { unsigned char *v; int n; } DSet;
static DSet ds_save(CG *g) {
    DSet d = { malloc((size_t)g->nslots + 1), g->nslots };
    memcpy(d.v, g->defd, (size_t)g->nslots);
    return d;
}
static void ds_load(CG *g, DSet d) {
    memcpy(g->defd, d.v, (size_t)d.n);
    for (int i = d.n; i < g->nslots; i++) g->defd[i] = 0;
}
static void ds_meet(CG *g, DSet d) {   /* 두 길 모두에서 대입된 것만 남긴다 */
    for (int i = 0; i < g->nslots; i++) g->defd[i] = i < d.n ? (g->defd[i] & d.v[i]) : 0;
}

static int is_global(CG *g, const char *name) {
    for (int i = 0; i < g->nglobals; i++) if (g->globals[i] == name) return 1;
    return 0;
}
static void add_global(CG *g, const char *name) {
    if (is_global(g, name)) return;
    if (g->nglobals == g->capglobals) {
        g->capglobals = g->capglobals ? g->capglobals * 2 : 16;
        g->globals = realloc(g->globals, sizeof(char *) * (size_t)g->capglobals);
    }
    g->globals[g->nglobals++] = name;
}

static Fn *find_fn(CG *g, const char *name) {
    for (int i = 0; i < g->nfns; i++) if (g->fns[i].name == name) return &g->fns[i];
    return NULL;
}

/* ---------------------------------------------------------------- 값 도우미 */
static int is_num(int ty) { return ty == TY_INT || ty == TY_BOOL; }

static void release(CG *g, CgV v) {
    if (v.t >= 0 && v.ty == TY_STR && v.own) E("  call void @na_str_free(ptr %%t%d)\n", v.t);
}

/* v_stringify: 정수는 십진, 참거짓은 참/거짓 */
static CgV to_str(CG *g, CgV v) {
    if (v.ty == TY_STR) return v;
    int t = newtmp(g);
    if (v.ty == TY_BOOL) {
        E("  %%t%d = call ptr @na_bool_to_str(i64 %%t%d)\n", t, v.t);
        return (CgV){ t, TY_STR, 0 };
    }
    E("  %%t%d = call ptr @na_i64_to_str(i64 %%t%d)\n", t, v.t);
    return (CgV){ t, TY_STR, 1 };
}

/* v_truthy → i1. 문자열은 비어 있지 않으면 참. v 는 여기서 다 쓴다 */
static int truthy(CG *g, CgV v) {
    int t = newtmp(g);
    if (v.ty == TY_STR) {
        int c = newtmp(g);
        E("  %%t%d = load i8, ptr %%t%d, align 1\n", c, v.t);
        E("  %%t%d = icmp ne i8 %%t%d, 0\n", t, c);
        release(g, v);
    } else {
        E("  %%t%d = icmp ne i64 %%t%d, 0\n", t, v.t);
    }
    return t;
}

/* ---------------------------------------------------------------- 식 */
static CgV emit_val(CG *g, Node *n);
static int emit_cond(CG *g, Node *n);

static int is_print_call(CG *g, Node *n) {
    return n->kind == N_CALL && n->a && n->a->kind == N_VAR
        && n->a->name == g->it->builtin_ptr[B_CHULRYEOG];
}

/* 사용자 함수 호출의 대상. 내장·변수·간접 호출은 거절 */
static Fn *call_target(CG *g, Node *n) {
    if (!n->a || n->a->kind != N_VAR) { cg_err(g, "LLVM: 간접 호출 미지원 (행 %d)", n->line); return NULL; }
    if (is_print_call(g, n)) { cg_err(g, "LLVM: 출력의 값(없음)은 쓸 수 없음 (행 %d)", n->line); return NULL; }
    Fn *f = find_fn(g, n->a->name);
    if (!f) { cg_err(g, "LLVM: '%s' 는 네이티브 미지원 (내장 함수이거나 사용자 함수가 아님, 행 %d)", n->a->name, n->line); return NULL; }
    if (n->nitems != f->node->nparams) {
        cg_err(g, "LLVM: '%s' 인자 수가 다름 (행 %d)", f->name, n->line);
        return NULL;
    }
    return f;
}

static CgV emit_call(CG *g, Node *n, Fn *f) {
    int *args = n->nitems ? malloc(sizeof(int) * (size_t)n->nitems) : NULL;
    for (int i = 0; i < n->nitems; i++) {
        CgV v = emit_val(g, n->items[i]);
        if (v.t < 0) { free(args); return CG_BAD; }
        if (v.ty != TY_INT) {   /* 참거짓·문자열 인자는 함수 안에서 형이 달라진다 */
            cg_err(g, "LLVM: 함수 인자는 정수만 (행 %d)", n->line);
            free(args); return CG_BAD;
        }
        args[i] = v.t;
    }
    int t = newtmp(g);
    E("  %%t%d = call i64 ", t);
    fn_sym(&g->body, f->name);
    E("(");
    for (int i = 0; i < n->nitems; i++) E("%si64 %%t%d", i ? ", " : "", args[i]);
    E(")\n");
    free(args);
    return (CgV){ t, TY_INT, 0 };
}

static CgV emit_bin(CG *g, Node *n) {
    int op = n->op;
    if (op == OP_AND || op == OP_OR) {
        /* 단락. 값은 왼쪽(단락됨) 또는 오른쪽 그대로 — 그래서 양쪽 형이 같아야 한다 */
        CgV a = emit_val(g, n->a);
        if (a.t < 0) return CG_BAD;
        if (a.ty == TY_STR) { cg_err(g, "LLVM: 문자열 그리고/또는 값 미지원 (행 %d)", n->line); return CG_BAD; }
        int r = newtmp(g);
        buf_printf(&g->entry, "  %%r%d = alloca i64, align 8\n", r);
        E("  store i64 %%t%d, ptr %%r%d, align 8\n", a.t, r);
        int c = newtmp(g), Lb = newlab(g), Le = newlab(g);
        E("  %%t%d = icmp ne i64 %%t%d, 0\n", c, a.t);
        if (op == OP_AND) condbr(g, c, Lb, Le); else condbr(g, c, Le, Lb);
        label(g, Lb);
        CgV b = emit_val(g, n->b);
        if (b.t < 0) return CG_BAD;
        if (b.ty != a.ty) { cg_err(g, "LLVM: 그리고/또는 양쪽 형이 다름 (행 %d)", n->line); return CG_BAD; }
        E("  store i64 %%t%d, ptr %%r%d, align 8\n", b.t, r);
        br(g, Le);
        label(g, Le);
        int t = newtmp(g);
        E("  %%t%d = load i64, ptr %%r%d, align 8\n", t, r);
        return (CgV){ t, a.ty, 0 };
    }
    if (op == OP_DIV) {
        cg_err(g, "LLVM: 나누기(/)는 늘 실수를 만든다 — 네이티브 미지원 (행 %d)", n->line);
        return CG_BAD;
    }
    CgV a = emit_val(g, n->a);
    if (a.t < 0) return CG_BAD;
    CgV b = emit_val(g, n->b);
    if (b.t < 0) return CG_BAD;

    if (op == OP_ADD && (a.ty == TY_STR || b.ty == TY_STR)) {
        CgV sa = to_str(g, a), sb = to_str(g, b);
        int t = newtmp(g);
        E("  %%t%d = call ptr @na_str_concat(ptr %%t%d, ptr %%t%d)\n", t, sa.t, sb.t);
        release(g, sa); release(g, sb);
        return (CgV){ t, TY_STR, 1 };
    }
    if (op == OP_ADD || op == OP_SUB || op == OP_MUL) {
        /* 참/거짓은 0/1 정수로 셈한다(num_add). 문자열 반복(* 수)은 미지원 */
        if (!is_num(a.ty) || !is_num(b.ty)) { cg_err(g, "LLVM: 문자열 셈 미지원 (행 %d)", n->line); return CG_BAD; }
        int t = newtmp(g);
        E("  %%t%d = %s i64 %%t%d, %%t%d\n", t, op == OP_ADD ? "add" : op == OP_SUB ? "sub" : "mul", a.t, b.t);
        return (CgV){ t, TY_INT, 0 };
    }
    if (op == OP_MOD) {
        /* 참/거짓이 끼면 인터프리터는 실수(fmod)로 간다 */
        if (a.ty != TY_INT || b.ty != TY_INT) { cg_err(g, "LLVM: %% 는 정수끼리만 (행 %d)", n->line); return CG_BAD; }
        int z = newtmp(g), Lz = newlab(g), Lok = newlab(g);
        E("  %%t%d = icmp eq i64 %%t%d, 0\n", z, b.t);
        condbr(g, z, Lz, Lok);
        label(g, Lz); emit_fail(g, ERR_DIV_ZERO);
        label(g, Lok);
        /* 나누는 수 -1 은 srem 넘침(INT64_MIN) — 나머지는 늘 0 */
        int m1 = newtmp(g), bs = newtmp(g), r = newtmp(g), nz = newtmp(g), x = newtmp(g),
            ng = newtmp(g), adj = newtmp(g), rb = newtmp(g), t = newtmp(g);
        E("  %%t%d = icmp eq i64 %%t%d, -1\n", m1, b.t);
        E("  %%t%d = select i1 %%t%d, i64 1, i64 %%t%d\n", bs, m1, b.t);
        E("  %%t%d = srem i64 %%t%d, %%t%d\n", r, a.t, bs);
        E("  %%t%d = icmp ne i64 %%t%d, 0\n", nz, r);
        E("  %%t%d = xor i64 %%t%d, %%t%d\n", x, r, b.t);
        E("  %%t%d = icmp slt i64 %%t%d, 0\n", ng, x);
        E("  %%t%d = and i1 %%t%d, %%t%d\n", adj, nz, ng);
        E("  %%t%d = add i64 %%t%d, %%t%d\n", rb, r, b.t);
        E("  %%t%d = select i1 %%t%d, i64 %%t%d, i64 %%t%d\n", t, adj, rb, r);
        return (CgV){ t, TY_INT, 0 };
    }
    if (op == OP_EQ || op == OP_NE) {
        int t = newtmp(g), r = newtmp(g);
        if (a.ty == TY_STR && b.ty == TY_STR) {
            E("  %%t%d = call i64 @na_str_eq(ptr %%t%d, ptr %%t%d)\n", t, a.t, b.t);
            release(g, a); release(g, b);
            if (op == OP_EQ) return (CgV){ t, TY_BOOL, 0 };
            E("  %%t%d = xor i64 %%t%d, 1\n", r, t);
            return (CgV){ r, TY_BOOL, 0 };
        }
        if (a.ty == TY_STR || b.ty == TY_STR) {   /* v_eq: 문자열과 수는 늘 다르다 */
            release(g, a); release(g, b);
            E("  %%t%d = add i64 0, %d\n", t, op == OP_NE);
            return (CgV){ t, TY_BOOL, 0 };
        }
        E("  %%t%d = icmp %s i64 %%t%d, %%t%d\n", t, op == OP_EQ ? "eq" : "ne", a.t, b.t);
        E("  %%t%d = zext i1 %%t%d to i64\n", r, t);
        return (CgV){ r, TY_BOOL, 0 };
    }
    if (op == OP_LT || op == OP_GT || op == OP_LE || op == OP_GE) {
        const char *ip = op == OP_LT ? "slt" : op == OP_GT ? "sgt" : op == OP_LE ? "sle" : "sge";
        const char *fp = op == OP_LT ? "olt" : op == OP_GT ? "ogt" : op == OP_LE ? "ole" : "oge";
        int c = newtmp(g), r = newtmp(g);
        if (a.ty == TY_STR && b.ty == TY_STR) {
            int k = newtmp(g);
            E("  %%t%d = call i64 @na_str_cmp(ptr %%t%d, ptr %%t%d)\n", k, a.t, b.t);
            release(g, a); release(g, b);
            E("  %%t%d = icmp %s i64 %%t%d, 0\n", c, ip, k);
        } else if (is_num(a.ty) && is_num(b.ty)) {
            /* 인터프리터 v_cmp 는 double 로 비교한다 — 2^53 을 넘는 정수도 똑같이 */
            int da = newtmp(g), db = newtmp(g);
            E("  %%t%d = sitofp i64 %%t%d to double\n", da, a.t);
            E("  %%t%d = sitofp i64 %%t%d to double\n", db, b.t);
            E("  %%t%d = fcmp %s double %%t%d, %%t%d\n", c, fp, da, db);
        } else {
            cg_err(g, "LLVM: 문자열과 수의 크기 비교 (행 %d)", n->line);
            return CG_BAD;
        }
        E("  %%t%d = zext i1 %%t%d to i64\n", r, c);
        return (CgV){ r, TY_BOOL, 0 };
    }
    cg_err(g, "LLVM: 미지원 이항 (행 %d)", n->line);
    return CG_BAD;
}

static CgV emit_val(CG *g, Node *n) {
    if (!n || g->err) return CG_BAD;
    switch (n->kind) {
    case N_LIT: {
        if (n->lit.tag == VT_STR) {
            Str *s = n->lit.as.s;
            return (CgV){ str_const(g, s->data, s->len), TY_STR, 0 };
        }
        if (n->lit.tag == VT_INT || n->lit.tag == VT_BOOL) {
            int t = newtmp(g);
            long long v = n->lit.tag == VT_INT ? (long long)n->lit.as.i : (n->lit.as.b ? 1 : 0);
            E("  %%t%d = add i64 0, %lld\n", t, v);
            return (CgV){ t, n->lit.tag == VT_INT ? TY_INT : TY_BOOL, 0 };
        }
        cg_err(g, "LLVM: 실수·없음 값은 네이티브 미지원 (행 %d)", n->line);
        return CG_BAD;
    }
    case N_VAR: {
        int s = lookup(g, n->name);
        if (s < 0 || !g->defd[s] || g->slots[s].ty == TY_NONE) {
            cg_err(g, "LLVM: '%s' 가 정의되지 않았을 수 있음 (행 %d)", n->name, n->line);
            return CG_BAD;
        }
        int t = newtmp(g);
        if (g->slots[s].ty == TY_STR) {
            E("  %%t%d = load ptr, ptr %%p%d, align 8\n", t, s);
            return (CgV){ t, TY_STR, 0 };
        }
        E("  %%t%d = load i64, ptr %%v%d, align 8\n", t, s);
        return (CgV){ t, g->slots[s].ty, 0 };
    }
    case N_UN: {
        if (n->op == OP_NOT) {
            int c = emit_cond(g, n->a);
            if (c < 0) return CG_BAD;
            int x = newtmp(g), t = newtmp(g);
            E("  %%t%d = xor i1 %%t%d, true\n", x, c);
            E("  %%t%d = zext i1 %%t%d to i64\n", t, x);
            return (CgV){ t, TY_BOOL, 0 };
        }
        CgV a = emit_val(g, n->a);
        if (a.t < 0) return CG_BAD;
        if (!is_num(a.ty)) { cg_err(g, "LLVM: 문자열에 단항 - (행 %d)", n->line); return CG_BAD; }
        int t = newtmp(g);
        E("  %%t%d = sub i64 0, %%t%d\n", t, a.t);
        return (CgV){ t, TY_INT, 0 };
    }
    case N_BIN: return emit_bin(g, n);
    case N_CALL: {
        Fn *f = call_target(g, n);
        if (!f) return CG_BAD;
        if (!f->ret_int) {
            cg_err(g, "LLVM: '%s' 는 값(없음)을 돌려줄 수 있어 값으로 못 씀 (행 %d)", f->name, n->line);
            return CG_BAD;
        }
        return emit_call(g, n, f);
    }
    default:
        cg_err(g, "LLVM: 미지원 식 (행 %d)", n->line);
        return CG_BAD;
    }
}

/* 조건 자리: 참/거짓만 필요하므로 그리고/또는·아니다를 형과 상관없이 단락 i1 로 */
static int emit_cond(CG *g, Node *n) {
    if (!n || g->err) return -1;
    if (n->kind == N_BIN && (n->op == OP_AND || n->op == OP_OR)) {
        int r = newtmp(g);
        buf_printf(&g->entry, "  %%r%d = alloca i1, align 1\n", r);
        int c1 = emit_cond(g, n->a);
        if (c1 < 0) return -1;
        E("  store i1 %%t%d, ptr %%r%d, align 1\n", c1, r);
        int Lb = newlab(g), Le = newlab(g);
        if (n->op == OP_AND) condbr(g, c1, Lb, Le); else condbr(g, c1, Le, Lb);
        label(g, Lb);
        int c2 = emit_cond(g, n->b);
        if (c2 < 0) return -1;
        E("  store i1 %%t%d, ptr %%r%d, align 1\n", c2, r);
        br(g, Le);
        label(g, Le);
        int t = newtmp(g);
        E("  %%t%d = load i1, ptr %%r%d, align 1\n", t, r);
        return t;
    }
    if (n->kind == N_UN && n->op == OP_NOT) {
        int c = emit_cond(g, n->a);
        if (c < 0) return -1;
        int t = newtmp(g);
        E("  %%t%d = xor i1 %%t%d, true\n", t, c);
        return t;
    }
    CgV v = emit_val(g, n);
    if (v.t < 0) return -1;
    return truthy(g, v);
}

/* ---------------------------------------------------------------- 문장 */
static void emit_stmt(CG *g, Node *n);

static void emit_block(CG *g, Node *blk) {
    if (!blk) return;
    if (blk->kind == N_BLOCK) { for (int i = 0; i < blk->nitems; i++) emit_stmt(g, blk->items[i]); return; }
    emit_stmt(g, blk);
}

static int push_loop(CG *g, int Lbrk, int Lcont) {
    if (g->depth >= 64) { cg_err(g, "LLVM: 반복이 너무 깊게 겹침"); return -1; }
    g->brk[g->depth] = Lbrk; g->cont[g->depth] = Lcont; g->depth++;
    return 0;
}

static void emit_print(CG *g, Node *n) {
    /* b_print: 인자를 다 계산한 뒤 공백으로 이어 찍고 줄바꿈 */
    CgV *vs = n->nitems ? malloc(sizeof(CgV) * (size_t)n->nitems) : NULL;
    for (int i = 0; i < n->nitems; i++) {
        vs[i] = emit_val(g, n->items[i]);
        if (vs[i].t < 0) { free(vs); return; }
    }
    for (int i = 0; i < n->nitems; i++) {
        if (i) E("  call void @na_write_byte(i64 32)\n");
        if (vs[i].ty == TY_STR) E("  call void @na_write_cstr(ptr %%t%d)\n", vs[i].t);
        else if (vs[i].ty == TY_BOOL) E("  call void @na_write_bool(i64 %%t%d)\n", vs[i].t);
        else E("  call void @na_write_i64(i64 %%t%d)\n", vs[i].t);
    }
    E("  call void @na_write_byte(i64 10)\n");
    for (int i = 0; i < n->nitems; i++) release(g, vs[i]);
    free(vs);
}

static void emit_exprstmt(CG *g, Node *e) {
    if (e->kind == N_CALL) {
        if (is_print_call(g, e)) { emit_print(g, e); return; }
        Fn *f = call_target(g, e);
        if (f) (void)emit_call(g, e, f);   /* 값은 버린다 — 없음이어도 된다 */
        return;
    }
    CgV v = emit_val(g, e);
    if (v.t >= 0) release(g, v);
}

static void emit_assign(CG *g, Node *n) {
    CgV v = emit_val(g, n->a);   /* 오른쪽이 먼저 — `x = x + 1` 의 x 는 이미 있어야 한다 */
    if (v.t < 0) return;
    int s = lookup(g, n->name);
    if (s < 0) s = new_slot(g, n->name, TY_NONE, 1);
    Slot *sl = &g->slots[s];
    /* 인터프리터의 대입은 이미 있는 묶음을 찾아 바꾼다: 함수 안에서 전역일 수 있는 이름이면 전역이 바뀐다 */
    if (g->cur && sl->fnlevel && !sl->param && is_global(g, n->name)) {
        cg_err(g, "LLVM: 함수 안에서 전역일 수 있는 '%s' 에 대입 (행 %d)", n->name, n->line);
        return;
    }
    if (sl->ty == TY_NONE) sl->ty = v.ty;
    else if (sl->ty != v.ty) {
        cg_err(g, "LLVM: 변수 '%s' 의 형이 바뀜 (행 %d)", n->name, n->line);
        return;
    }
    if (v.ty == TY_STR) {
        int nv = v.t;
        if (!v.own) { nv = newtmp(g); E("  %%t%d = call ptr @na_str_dup(ptr %%t%d)\n", nv, v.t); }
        int old = newtmp(g);
        E("  %%t%d = load ptr, ptr %%p%d, align 8\n", old, s);
        E("  store ptr %%t%d, ptr %%p%d, align 8\n", nv, s);
        E("  call void @na_str_free(ptr %%t%d)\n", old);
    } else {
        E("  store i64 %%t%d, ptr %%v%d, align 8\n", v.t, s);
    }
    g->defd[s] = 1;
}

static void emit_if(CG *g, Node *n) {
    int c = emit_cond(g, n->a);
    if (c < 0) return;
    int Lt = newlab(g), Le = newlab(g), Lf = n->c ? newlab(g) : Le;
    condbr(g, c, Lt, Lf);
    DSet pre = ds_save(g);
    label(g, Lt);
    emit_block(g, n->b);
    br(g, Le);
    if (n->c) {
        DSet then_set = ds_save(g);
        ds_load(g, pre);
        label(g, Lf);
        if (n->c->kind == N_IF) emit_if(g, n->c);   /* 아니면 만약 — 인터프리터처럼 줄은 안 바꾼다 */
        else emit_block(g, n->c);
        br(g, Le);
        ds_meet(g, then_set);
        free(then_set.v);
    } else {
        ds_load(g, pre);
    }
    free(pre.v);
    label(g, Le);
}

static void emit_while(CG *g, Node *n) {
    int Lc = newlab(g), Lb = newlab(g), Le = newlab(g);
    br(g, Lc);
    label(g, Lc);
    int c = emit_cond(g, n->a);
    if (c < 0) return;
    condbr(g, c, Lb, Le);
    label(g, Lb);
    DSet pre = ds_save(g);            /* 몸은 한 번도 안 돌 수 있다 */
    if (push_loop(g, Le, Lc) < 0) { free(pre.v); return; }
    emit_block(g, n->b);
    br(g, Lc);
    g->depth--;
    ds_load(g, pre);
    free(pre.v);
    label(g, Le);
}

/* interp.c N_FOR: 시작·끝·스텝을 한 번 계산, 스텝 0 은 오류, 스텝 부호로 끝 비교,
 * 반복마다 새 묶음에 변수를 정의한다(몸에서 바꿔도 다음 값은 계수기에서, 반복 뒤엔 없음). */
static void emit_for(CG *g, Node *n) {
    CgV a = emit_val(g, n->a);
    if (a.t < 0) return;
    CgV b = emit_val(g, n->b);
    if (b.t < 0) return;
    CgV s = { -1, TY_INT, 0 };
    if (n->c) { s = emit_val(g, n->c); if (s.t < 0) return; }
    else { s.t = newtmp(g); E("  %%t%d = add i64 0, 1\n", s.t); }
    if (a.ty != TY_INT || b.ty != TY_INT || s.ty != TY_INT) {
        /* 참/거짓이 끼면 인터프리터는 실수 반복, 문자열이면 오류 */
        cg_err(g, "LLVM: 범위 반복의 시작·끝·스텝은 정수만 (행 %d)", n->line);
        return;
    }
    int z = newtmp(g), Lz = newlab(g), Lok = newlab(g);
    E("  %%t%d = icmp eq i64 %%t%d, 0\n", z, s.t);
    condbr(g, z, Lz, Lok);
    label(g, Lz); emit_fail(g, ERR_STEP_ZERO);
    label(g, Lok);
    int ctr = new_slot(g, NULL, TY_INT, 0);
    E("  store i64 %%t%d, ptr %%v%d, align 8\n", a.t, ctr);
    int pos = newtmp(g);
    E("  %%t%d = icmp sgt i64 %%t%d, 0\n", pos, s.t);
    int Lc = newlab(g), Lb = newlab(g), Li = newlab(g), Le = newlab(g);
    br(g, Lc);
    label(g, Lc);
    int cur = newtmp(g), le = newtmp(g), ge = newtmp(g), ok = newtmp(g);
    E("  %%t%d = load i64, ptr %%v%d, align 8\n", cur, ctr);
    E("  %%t%d = icmp sle i64 %%t%d, %%t%d\n", le, cur, b.t);
    E("  %%t%d = icmp sge i64 %%t%d, %%t%d\n", ge, cur, b.t);
    E("  %%t%d = select i1 %%t%d, i1 %%t%d, i1 %%t%d\n", ok, pos, le, ge);
    condbr(g, ok, Lb, Le);
    label(g, Lb);
    int var = new_slot(g, n->name, TY_INT, 0);
    E("  store i64 %%t%d, ptr %%v%d, align 8\n", cur, var);
    DSet pre = ds_save(g);
    g->defd[var] = 1;
    push_bind(g, n->name, var);
    if (push_loop(g, Le, Li) < 0) { free(pre.v); return; }
    emit_block(g, n->items[0]);
    br(g, Li);
    g->depth--;
    g->nbinds--;
    label(g, Li);
    int c2 = newtmp(g), c3 = newtmp(g);
    E("  %%t%d = load i64, ptr %%v%d, align 8\n", c2, ctr);
    E("  %%t%d = add i64 %%t%d, %%t%d\n", c3, c2, s.t);
    E("  store i64 %%t%d, ptr %%v%d, align 8\n", c3, ctr);
    br(g, Lc);
    ds_load(g, pre);
    free(pre.v);
    label(g, Le);
}

static void emit_return(CG *g, Node *n) {
    if (!g->cur) {   /* 최상위 반환: 인터프리터는 조용히 끝낸다(finish_err GS_RET) */
        if (n->a) {
            CgV v = emit_val(g, n->a);
            if (v.t < 0) return;
            release(g, v);
        }
        E("  ret i32 0\n");
        g->term = 1;
        return;
    }
    if (g->cur->ret_int) {
        CgV v = emit_val(g, n->a);
        if (v.t < 0) return;
        if (v.ty != TY_INT) { cg_err(g, "LLVM: '%s' 가 정수가 아닌 값을 돌려줌 (행 %d)", g->cur->name, n->line); return; }
        E("  store i64 %%t%d, ptr %%r%d, align 8\n", v.t, g->ret_slot);
    } else if (n->a) {   /* 값을 아무도 안 쓰는 함수: 식은 계산만 */
        CgV v = emit_val(g, n->a);
        if (v.t < 0) return;
        release(g, v);
    }
    br(g, g->ret_lab);
}

static void emit_stmt(CG *g, Node *n) {
    if (!n || g->err || g->term) return;   /* 반환·멈춤·계속 뒤는 인터프리터도 안 돈다 */
    if (n->kind == N_STMT) {
        E("  store i64 %d, ptr @na_line, align 8\n", n->line);   /* it->cur_line */
        emit_stmt(g, n->a);
        return;
    }
    switch (n->kind) {
    case N_ASSIGN: emit_assign(g, n); break;
    case N_EXPRSTMT: emit_exprstmt(g, n->a); break;
    case N_IF: emit_if(g, n); break;
    case N_WHILE: emit_while(g, n); break;
    case N_FOR: emit_for(g, n); break;
    case N_BREAK: case N_CONT:
        /* 함수 안 반복 밖의 멈춤은 인터프리터에선 부른 쪽 반복까지 번진다 — 따라 하지 않는다 */
        if (g->depth <= 0) { cg_err(g, "LLVM: 반복 밖 멈춤/계속 (행 %d)", n->line); break; }
        br(g, n->kind == N_BREAK ? g->brk[g->depth - 1] : g->cont[g->depth - 1]);
        break;
    case N_RETURN: emit_return(g, n); break;
    case N_FUNC: cg_err(g, "LLVM: 안쪽 함수 정의 미지원 (행 %d)", n->line); break;
    default: cg_err(g, "LLVM: 미지원 문장 (행 %d)", n->line); break;
    }
}

/* ---------------------------------------------------------------- 앞 검사 */
static Node *unwrap(Node *n) { return (n && n->kind == N_STMT) ? n->a : n; }

typedef void (*WalkFn)(CG *g, Node *n, void *ud);
/* 자식 a/b/c/items 를 다 돈다. into_funcs=0 이면 함수·람다 몸으로는 안 들어간다 */
static void walk(CG *g, Node *n, WalkFn f, void *ud, int into_funcs) {
    if (!n) return;
    f(g, n, ud);
    if (!into_funcs && (n->kind == N_FUNC || n->kind == N_LAMBDA)) return;
    walk(g, n->a, f, ud, into_funcs);
    walk(g, n->b, f, ud, into_funcs);
    walk(g, n->c, f, ud, into_funcs);
    for (int i = 0; i < n->nitems; i++) walk(g, n->items[i], f, ud, into_funcs);
}

static int stmt_returns(Node *s);
static int block_returns(Node *blk) {
    if (!blk) return 0;
    if (blk->kind != N_BLOCK) return stmt_returns(blk);
    for (int i = 0; i < blk->nitems; i++) if (stmt_returns(blk->items[i])) return 1;
    return 0;
}
/* 이 문장을 지나는 모든 길이 `반환 식` 으로 끝나는가 (반복은 보수적으로 아니라고 본다) */
static int stmt_returns(Node *s) {
    s = unwrap(s);
    if (!s) return 0;
    if (s->kind == N_RETURN) return s->a != NULL;
    if (s->kind == N_IF) return s->c && block_returns(s->b) && block_returns(s->c);
    return 0;
}

static void w_bare_return(CG *g, Node *n, void *ud) {
    (void)g;
    if (n->kind == N_RETURN && !n->a) *(int *)ud = 1;
}
static void w_globals(CG *g, Node *n, void *ud) {
    (void)ud;
    if (n->kind == N_ASSIGN || n->kind == N_FOR) add_global(g, n->name);
}
static void w_binding_clash(CG *g, Node *n, void *ud) {
    (void)ud;
    if (n->kind == N_ASSIGN || n->kind == N_FOR) {
        if (find_fn(g, n->name)) cg_err(g, "LLVM: 변수 '%s' 가 함수 이름과 겹침 (행 %d)", n->name, n->line);
    } else if (n->kind == N_FUNC) {
        for (int i = 0; i < n->nparams; i++)
            if (find_fn(g, n->pnames[i])) cg_err(g, "LLVM: 인자 '%s' 가 함수 이름과 겹침 (행 %d)", n->pnames[i], n->line);
    }
}
typedef struct { unsigned char *mark; } CallSet;
static void w_calls(CG *g, Node *n, void *ud) {
    if (n->kind != N_CALL || !n->a || n->a->kind != N_VAR) return;
    Fn *f = find_fn(g, n->a->name);
    if (f) ((CallSet *)ud)->mark[f - g->fns] = 1;
}

/* 최상위 함수 표를 만들고, 인터프리터와 어긋날 수 있는 모양을 미리 거절한다 */
static void prepass(CG *g, Node *root) {
    Interp *it = g->it;
    for (int i = 0; i < root->nitems && !g->err; i++) {
        Node *st = unwrap(root->items[i]);
        if (!st || st->kind != N_FUNC) continue;
        if (find_fn(g, st->name)) { cg_err(g, "LLVM: 함수 '%s' 를 다시 정의 (행 %d)", st->name, st->line); return; }
        /* 호출은 내장·고르기를 먼저 본다 — 같은 이름의 사용자 함수는 불리지 않는다 */
        if (builtin_id_of(it, st->name) >= 0 || st->name == it->goreugi_ptr) {
            cg_err(g, "LLVM: 함수 '%s' 가 내장 이름과 겹침 (행 %d)", st->name, st->line);
            return;
        }
        for (int k = 0; k < st->nparams; k++) {
            if (st->pdefs[k]) { cg_err(g, "LLVM: 기본값 인자 미지원 (행 %d)", st->line); return; }
            for (int j = 0; j < k; j++)
                if (st->pnames[j] == st->pnames[k]) { cg_err(g, "LLVM: 같은 이름 인자 (행 %d)", st->line); return; }
        }
        g->fns = realloc(g->fns, sizeof(Fn) * (size_t)(g->nfns + 1));
        int bare = 0;
        walk(g, st->a, w_bare_return, &bare, 0);
        g->fns[g->nfns++] = (Fn){ st->name, st, i, !bare && block_returns(st->a) };
    }
    if (g->err) return;

    /* 전역일 수 있는 이름: 최상위 코드의 대입·반복 변수, 그리고 함수 이름 */
    for (int i = 0; i < root->nitems; i++) walk(g, root->items[i], w_globals, NULL, 0);
    for (int i = 0; i < g->nfns; i++) add_global(g, g->fns[i].name);
    for (int i = 0; i < root->nitems && !g->err; i++) walk(g, root->items[i], w_binding_clash, NULL, 1);
    if (g->err) return;

    /* 함수는 그 문장이 실행돼야 생긴다: 최상위 문장 k 에서 (거쳐서라도) 부르는 함수는 k 보다 앞에 있어야 */
    int nf = g->nfns;
    if (!nf) return;
    unsigned char *adj = calloc((size_t)nf * (size_t)nf, 1);
    for (int i = 0; i < nf; i++) {
        CallSet cs = { adj + (size_t)i * (size_t)nf };
        walk(g, g->fns[i].node->a, w_calls, &cs, 0);
    }
    unsigned char *reach = malloc((size_t)nf);
    int *stack = malloc(sizeof(int) * (size_t)nf);
    for (int k = 0; k < root->nitems && !g->err; k++) {
        Node *st = unwrap(root->items[k]);
        if (st && st->kind == N_FUNC) continue;
        memset(reach, 0, (size_t)nf);
        CallSet cs = { reach };
        walk(g, root->items[k], w_calls, &cs, 0);
        int sp = 0;
        for (int j = 0; j < nf; j++) if (reach[j]) stack[sp++] = j;
        while (sp) {
            int j = stack[--sp];
            for (int m = 0; m < nf; m++)
                if (adj[(size_t)j * (size_t)nf + m] && !reach[m]) { reach[m] = 1; stack[sp++] = m; }
        }
        for (int j = 0; j < nf; j++)
            if (reach[j] && g->fns[j].stmt > k)
                cg_err(g, "LLVM: 함수 '%s' 를 정의보다 먼저 부름 (행 %d)", g->fns[j].name, root->items[k]->line);
    }
    free(adj); free(reach); free(stack);
}

/* ---------------------------------------------------------------- 함수·모듈 */
static void reset_fn(CG *g) {
    g->body.n = 0; if (g->body.p) g->body.p[0] = 0;
    g->entry.n = 0; if (g->entry.p) g->entry.p[0] = 0;
    g->tmp = g->lab = g->term = g->depth = 0;
    g->nslots = 0; g->nbinds = 0;
}

static void assemble(CG *g, Buf *out) {
    buf_put(out, g->entry.p ? g->entry.p : "", g->entry.n);
    buf_put(out, g->body.p ? g->body.p : "", g->body.n);
    buf_put(out, "}\n\n", 3);
}

static void emit_function(CG *g, Fn *f, Buf *out) {
    reset_fn(g);
    g->cur = f;
    Node *fn = f->node;
    g->ret_lab = newlab(g);
    g->ret_slot = newtmp(g);
    buf_printf(&g->entry, "  %%r%d = alloca i64, align 8\n  store i64 0, ptr %%r%d, align 8\n",
               g->ret_slot, g->ret_slot);
    for (int i = 0; i < fn->nparams; i++) {
        int s = new_slot(g, fn->pnames[i], TY_INT, 1);
        g->slots[s].param = 1;
        g->defd[s] = 1;
        E("  store i64 %%a%d, ptr %%v%d, align 8\n", i, s);
    }
    /* apply_func: 인자를 묶은 뒤 깊이 +1, 700 을 넘으면 재귀 오류. 나갈 때 -1 */
    int d0 = newtmp(g), d1 = newtmp(g), dc = newtmp(g), Lrec = newlab(g), Lgo = newlab(g);
    E("  %%t%d = load i64, ptr @na_depth, align 8\n", d0);
    E("  %%t%d = add i64 %%t%d, 1\n", d1, d0);
    E("  store i64 %%t%d, ptr @na_depth, align 8\n", d1);
    E("  %%t%d = icmp sgt i64 %%t%d, 700\n", dc, d1);
    condbr(g, dc, Lrec, Lgo);
    label(g, Lrec); emit_fail(g, ERR_RECURSION);
    label(g, Lgo);
    emit_block(g, fn->a);
    br(g, g->ret_lab);   /* 끝까지 오면 없음 — ret_int 가 아니면 값은 아무도 안 쓴다 */
    label(g, g->ret_lab);
    for (int s = 0; s < g->nslots; s++) {   /* 지역 문자열은 함수와 함께 끝난다 */
        if (g->slots[s].ty != TY_STR) continue;
        int t = newtmp(g);
        E("  %%t%d = load ptr, ptr %%p%d, align 8\n", t, s);
        E("  call void @na_str_free(ptr %%t%d)\n", t);
    }
    int x = newtmp(g), y = newtmp(g), r = newtmp(g);
    E("  %%t%d = load i64, ptr @na_depth, align 8\n", x);
    E("  %%t%d = sub i64 %%t%d, 1\n", y, x);
    E("  store i64 %%t%d, ptr @na_depth, align 8\n", y);
    E("  %%t%d = load i64, ptr %%r%d, align 8\n", r, g->ret_slot);
    E("  ret i64 %%t%d\n", r);
    if (g->err) return;
    buf_put(out, "define internal i64 ", 20);
    fn_sym(out, f->name);
    buf_put(out, "(", 1);
    for (int i = 0; i < fn->nparams; i++) buf_printf(out, "%si64 %%a%d", i ? ", " : "", i);
    buf_put(out, ") {\nentry:\n", 11);
    assemble(g, out);
}

static void emit_main(CG *g, Node *root, const char *src, Buf *out) {
    reset_fn(g);
    g->cur = NULL;
    int t = str_const(g, src, (uint32_t)strlen(src));   /* 오류 때 소스 줄을 붙이려고 */
    E("  call void @na_init(ptr %%t%d)\n", t);
    for (int i = 0; i < root->nitems && !g->err; i++) {
        Node *st = root->items[i];
        Node *inner = unwrap(st);
        if (inner && inner->kind == N_FUNC) {   /* 정의 자체는 위에서 낳았다. 줄만 인터프리터처럼 */
            if (!g->term && st->kind == N_STMT) E("  store i64 %d, ptr @na_line, align 8\n", st->line);
            continue;
        }
        emit_stmt(g, st);
    }
    if (!g->term) E("  ret i32 0\n");
    if (g->err) return;
    buf_put(out, "define i32 @main() {\nentry:\n", 28);
    assemble(g, out);
}

char *llvm_emit_module(Interp *it, Node *root, const char *src, char **errp) {
    CG g = {0};
    g.it = it;
    free(sg); sg = NULL; nsg = capsg = 0;
    if (!root || root->kind != N_BLOCK) {
        if (errp) *errp = strdup("LLVM: 루트가 블록이 아님");
        return NULL;
    }
    Buf funcs = {0}, mainb = {0};
    prepass(&g, root);
    for (int i = 0; i < g.nfns && !g.err; i++) emit_function(&g, &g.fns[i], &funcs);
    if (!g.err) emit_main(&g, root, src ? src : "", &mainb);
    free(g.body.p); free(g.entry.p);
    free(g.slots); free(g.defd); free(g.binds); free(g.fns); free(g.globals);
    if (g.err) {
        if (errp) *errp = g.err;
        free(funcs.p); free(mainb.p);
        free(sg); sg = NULL; nsg = capsg = 0;
        return NULL;
    }
    Buf out = {0};
    buf_printf(&out, "; 가나다 → LLVM IR (부분집합) + na_rt\n\n");
    emit_str_globals(&out);
    buf_printf(&out,
        "\n@na_line = external global i64\n"
        "@na_depth = external global i64\n"
        "declare void @na_init(ptr)\n"
        "declare void @na_fail(ptr) noreturn\n"
        "declare void @na_write_i64(i64)\n"
        "declare void @na_write_cstr(ptr)\n"
        "declare void @na_write_bool(i64)\n"
        "declare void @na_write_byte(i64)\n"
        "declare ptr @na_str_concat(ptr, ptr)\n"
        "declare ptr @na_i64_to_str(i64)\n"
        "declare ptr @na_bool_to_str(i64)\n"
        "declare ptr @na_str_dup(ptr)\n"
        "declare void @na_str_free(ptr)\n"
        "declare i64 @na_str_eq(ptr, ptr)\n"
        "declare i64 @na_str_cmp(ptr, ptr)\n\n");
    buf_put(&out, funcs.p ? funcs.p : "", funcs.n);
    buf_put(&out, mainb.p ? mainb.p : "", mainb.n);
    free(funcs.p); free(mainb.p);
    free(sg); sg = NULL; nsg = capsg = 0;
    if (errp) *errp = NULL;
    return out.p;
}
