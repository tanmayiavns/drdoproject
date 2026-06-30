
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>

// Rule 98 PP: macro reuses stdlib name
#define memset(d,v,n) (d)
#define strcpy(d,s)   (d)

// Rule 74: function-like macros
#define MAX(a,b)   ((a) > (b) ? (a) : (b))
#define SQUARE(x)  ((x) * (x))

// Rule 78: object-like macro not parenthesized
#define OFFSET   10 + 5
#define SCALE    2 * 3

// Rule 80: more than one # or ## in macro
#define PASTE2(a,b)   #a ## b
#define PASTE3(a,b,c) #a ## #b

// Rule 73: #undef
#undef memset
#undef strcpy

// ---------------------------------------------------------------------------
// GLOBAL DECLARATIONS
// ---------------------------------------------------------------------------

// Rule 200: non-static global variable
// Rule D17: file-scope declaration should be static
int    g_count;                    /* Rule 200 + Rule 8 + Rule D17 */
char   g_flag;                     /* Rule 200 + Rule 8 + Rule D17 */
double g_ratio;                    /* Rule 200 + Rule 8 + Rule D17 */

// Rule 119: incomplete array
extern int  g_open_arr[];          /* Rule 119 + Rule 18 + Rule 19 */
extern char g_open_str[];          /* Rule 119 + Rule 18 + Rule 19 */

// Rule 18 + Rule 19: extern without definition
extern int  g_extern_a;            /* Rule 18 + Rule 19 */
extern int  g_extern_b;            /* Rule 18 + Rule 19 */

// Rule 120: partial array initialization
int  g_partial5[5] = { 10, 20 };    /* Rule 120 */
char g_partial8[8] = { 'a', 'b' };  /* Rule 120 */

// Rule 7: tag name collides with ordinary name
int  Buffer;
struct Buffer { int x; int y; };  /* Rule 7 */

int  Packet;
union Packet { int i; float f; }; /* Rule 7 */

// Rule 28: typedefs
typedef unsigned int  MyUint;     /* Rule 28 */
typedef unsigned char MyByte;     /* Rule 28 */
typedef int           MyInt;      /* Rule 28 */

// Rule 29: enums
enum State { STATE_IDLE, STATE_RUN, STATE_DONE };   /* Rule 29 */
enum Color { COLOR_RED, COLOR_GREEN, COLOR_BLUE };  /* Rule 29 */

// Rule 24: partial enum initialization
enum MixedA { MA_FIRST = 0, MA_SECOND, MA_THIRD = 10 }; /* Rule 24 */
enum MixedB { MB_A = 5, MB_B, MB_C = 20, MB_D };        /* Rule 24 */

// Rule 86: non-const function pointer
typedef void (*VoidFn)(void);
typedef int  (*IntFn)(int, int);
VoidFn g_fn1;                     /* Rule 86 */
IntFn  g_fn2;                     /* Rule 86 */

// Rule 93 + Rule 94: bit-field violations
struct BitTest {
    char         bf_char : 3;    /* Rule 93 */
    short        bf_short : 2;    /* Rule 93 */
    int          bf_s1 : 1;    /* Rule 94 */
    int          bf_s2 : 1;    /* Rule 94 */
    unsigned int bf_ok : 4;    /* OK */
};

// Rule 98 AST: stdlib name reused
void strlen(const char* s) { (void)s; }   /* Rule 98 + Rule 58 */
void memcpy(void* d, const void* s, unsigned n)
{
    (void)d;(void)s;(void)n;
}

// Rule 56: variadic functions
void var_logger(int level, ...) { (void)level; } /* Rule 56 */
int  var_sum(int count, ...) { (void)count; return 0; }

// ---------------------------------------------------------------------------
// TYPE D RULE TRIGGERS — GLOBAL SCOPE
// ---------------------------------------------------------------------------

// Rule D3 [x2]: Wide string literals shall not be used
const wchar_t* wd3a = L"wide string one";   /* Rule D3 */
const wchar_t* wd3b = L"wide string two";   /* Rule D3 */

// Rule D10 [x2]: Float bit representation via union (type punning)
union FloatBits1 {                          /* Rule D10 */
    float    f;
    int      i;
};
union FloatBits2 {                          /* Rule D10 */
    double   d;
    long     l;
};

// Rule D95 [x2]: Anonymous struct/union members
struct WithAnonymous1 {
    int named;
    struct {                               /* Rule D95 — anonymous struct */
        int x;
        int y;
    };
};
union WithAnonymous2 {
    int a;
    struct {                               /* Rule D95 — anonymous struct */
        short lo;
        short hi;
    };
};

// ---------------------------------------------------------------------------
// FORWARD PROTOTYPES (with prototype = no Rule 58)
// ---------------------------------------------------------------------------
void func_with_proto_a(void);
int  func_with_proto_b(int x);

// ---------------------------------------------------------------------------
// Rule 12: never-called + Rule 4: unused vars + Rule 72: block #define
// ---------------------------------------------------------------------------
static void never_called_a(void)   /* Rule 12 */
{
    int dead1 = 0;                 /* Rule 4 + Rule 8 */
    char dead2 = 'X';             /* Rule 4 + Rule 8 */
    (void)dead1; (void)dead2;
#define BLOCK_DEFINE_A  42    /* Rule 72 */
}

static void never_called_b(void)   /* Rule 12 */
{
    double dead3 = 0.0;           /* Rule 4 + Rule 8 */
    (void)dead3;
#define BLOCK_DEFINE_B  99    /* Rule 72 */
}

// ---------------------------------------------------------------------------
// Rule 57: recursion + Rule 55: complexity + Rule 64/196: multiple returns
// ---------------------------------------------------------------------------
int factorial(int n)               /* Rule 59 + Rule 57 + Rule 64 + Rule 58 */
{
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

int fib(int n)                     /* Rule 59 + Rule 57 + Rule 58 */
{
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}

int classify(int a, int b, int c, int d)  /* Rule 55 + Rule 59 + Rule 58 */
{
    int r = 0;
    if (a > 0)   r++;
    if (b > 0)   r++;
    if (c > 0)   r++;
    if (d > 0)   r++;
    if (a < 100) r++;
    if (b < 100) r++;
    if (c < 100) r++;
    if (d < 100) r++;
    if (a == b)  r++;
    if (b == c)  r++;
    if (c == d)  r++;              /* complexity = 12 -> Rule 55 */
    return r;
}

// Functions with prototypes (no Rule 58)
void func_with_proto_a(void) {
#define INSIDE_PROTO  1       /* Rule 72 */
}

int func_with_proto_b(int x) { return x + 1; } /* Rule 59 */

// ---------------------------------------------------------------------------
// Rule D54 [x2]: Function declared inside function (not at file scope)
// ---------------------------------------------------------------------------
void outer_func_a(void)            /* Rule 58 */
{
    void inner_decl_a(int x);      /* Rule D54 */
    (void)inner_decl_a;
}

void outer_func_b(void)            /* Rule 58 */
{
    int helper_b(int a, int b);    /* Rule D54 */
    (void)helper_b;
}

// =============================================================================
// main
// =============================================================================
int main(void)
{
    // ── Rule 100: errno ───────────────────────────────────────────────────
    int err1 = errno;              /* Rule 100 */
    int err2 = errno;              /* Rule 100 */
    (void)err1; (void)err2;

    // ── Rule 2 [x3]: trigraphs ───────────────────────────────────────────
    const char* tg1 = "??=";      /* Rule 2 */
    const char* tg2 = "??<";      /* Rule 2 */
    const char* tg3 = "??>";      /* Rule 2 */
    (void)tg1; (void)tg2; (void)tg3;

    // ── Rule 130 [x2]: digraphs ──────────────────────────────────────────
    const char* dg1 = "<:array:>"; /* Rule 130 */
    const char* dg2 = "<%block%>"; /* Rule 130 */
    (void)dg1; (void)dg2;

    // ── Rule 8 [x7]: plain basic types ───────────────────────────────────
    int           r8a = 0;
    char          r8b = 0;
    short         r8c = 0;
    long          r8d = 0L;
    double        r8e = 0.0;
    unsigned int  r8f = 0U;
    unsigned char r8g = 0U;

    // ── Rule 13 [x3]: octal constants ────────────────────────────────────
    int oct1 = 07;                 /* Rule 13 */
    int oct2 = 011;                /* Rule 13 */
    int oct3 = 0755;               /* Rule 13 */
    (void)oct1; (void)oct2; (void)oct3;

    // ── Rule 34 [x6]: lowercase suffixes ─────────────────────────────────
    long         sf1 = 10l;       /* Rule 34 */
    long         sf2 = 20l;       /* Rule 34 */
    float        sf3 = 1.0f;      /* Rule 34 */
    float        sf4 = 2.5f;      /* Rule 34 */
    unsigned int sf5 = 5u;        /* Rule 34 */
    long long    sf6 = 100ll;     /* Rule 34 */
    (void)sf1;(void)sf2;(void)sf3;(void)sf4;(void)sf5;(void)sf6;

    // ── Rule 27 [x2]: assignment in if ───────────────────────────────────
    int x = 0;
    if (x = 5) { x = 0; }        /* Rule 27 + Rule 46 + Rule 181 */
    if (x = 10) { x = 0; }        /* Rule 27 + Rule 46 + Rule 181 */

    // ── Rule 38 [x2]: float equality ─────────────────────────────────────
    float f1 = 1.0F, f2 = 2.0F;
    if (f1 == 1.0F) { f1 = 0.0F; } /* Rule 38 + Rule 46 + Rule 181 */
    if (f2 != 0.0F) { f2 = 0.0F; } /* Rule 38 + Rule 46 + Rule 181 */
    (void)f1; (void)f2;

    // ── Rule 41 [x3]: null statements ────────────────────────────────────
    ;                              /* Rule 41 */
    ;                              /* Rule 41 */
    ;                              /* Rule 41 */

    // ── Rule 43 [x2]: goto ───────────────────────────────────────────────
    goto lbl_a;                    /* Rule 43 */
lbl_a:;
    goto lbl_b;                    /* Rule 43 */
lbl_b:;

    // ── Rule 44 [x4]: break/continue ─────────────────────────────────────
    for (int i = 0; i < 10; i++) {
        if (i == 2) break;         /* Rule 44 */
        if (i == 5) continue;      /* Rule 44 */
    }
    int wv = 0;
    while (wv < 5) {
        if (wv == 2) break;        /* Rule 44 */
        wv++;
    }

    // ── Rule 46 [x3]: if without else ────────────────────────────────────
    int v = 1;
    if (v > 0)  v++;               /* Rule 46 + Rule 181 */
    if (v > 1)  v--;               /* Rule 46 + Rule 181 */
    if (v == 0) v = 1;             /* Rule 46 + Rule 181 */

    // ── Rule 47 [x2]: fall-through ───────────────────────────────────────
    int sw = 2;
    switch (sw) {
    case 1:                    /* Rule 47 */
        sw = 10;
    case 2:                    /* Rule 47 */
        sw = 20;
    case 3:
        sw = 30; break;
    default: break;
    }

    // ── Rule 48 [x2]: switch without default ─────────────────────────────
    switch (sw) {                  /* Rule 48 */
    case 1: sw = 1; break;
    case 2: sw = 2; break;
    }
    switch (v) {                   /* Rule 48 */
    case 0: v = 1; break;
    }

    // ── Rule 49 [x2]: switch on boolean ──────────────────────────────────
    switch (sw > 0) { default: break; }  /* Rule 49 */
    switch (sw == 1) { default: break; }  /* Rule 49 */

                              // ── Rule 50 [x2]: switch no cases ────────────────────────────────────
    switch (sw) { default: sw = 0; break; } /* Rule 50 */
    switch (v) { default: v = 0; break; } /* Rule 50 */

                        // ── Rule 143 [x2]: empty switch ──────────────────────────────────────
                        switch (sw) {}                /* Rule 143 */
                        switch (v) {}                /* Rule 143 */

                        // ── Rule 51/188 [x2]: float loop counter ─────────────────────────────
                        for (float fc1 = 0.0F; fc1 < 10.0F; fc1 += 1.0F) { /* Rule 51+188 */
                            (void)fc1;
                        }
                        for (double dc1 = 0.0; dc1 < 5.0; dc1 += 0.5) {    /* Rule 51+188 */
                            (void)dc1;
                        }

                        // ── Rule 53 [x2]: for counter modified in body ───────────────────────
                        for (int j = 0; j < 8; j++) {
                            j = j + 2;                 /* Rule 53 */
                        }
                        for (int k = 0; k < 10; k++) {
                            k += 1;                    /* Rule 53 */
                        }

                        // ── Rule 83 [x3]: pointer arithmetic ─────────────────────────────────
                        int arr[6] = { 1, 2, 3, 4, 5, 6 };
                        int* pa = arr;
                        int* pb = pa + 2;              /* Rule 83 */
                        int* pc = pb - 1;              /* Rule 83 */
                        int* pd = pc + 3;              /* Rule 83 */
                        (void)pd;

                        // ── Rule 99 [x5]: dynamic allocation ─────────────────────────────────
                        void* m1 = malloc(32);        /* Rule 99 */
                        void* m2 = calloc(8, 4);     /* Rule 99 */
                        void* m3 = realloc(m1, 64);  /* Rule 99 */
                        free(m2);                      /* Rule 99 */
                        free(m3);                      /* Rule 99 */

                        // ── Rule 105 [x2]: signal ────────────────────────────────────────────
                        signal(SIGINT, SIG_DFL);     /* Rule 105 */
                        signal(SIGTERM, SIG_IGN);     /* Rule 105 */

                        // ── Rule 106 [x3]: stdio ─────────────────────────────────────────────
                        printf("val=%d\n", v);        /* Rule 106 */
                        fprintf(stderr, "err\n");     /* Rule 106 */
                        puts("done");                  /* Rule 106 */

                        // ── Rule 107 [x3]: atoi/atof/atol ────────────────────────────────────
                        int    p1 = atoi("42");       /* Rule 107 */
                        long   p2 = atol("1000");     /* Rule 107 */
                        double p3 = atof("3.14");     /* Rule 107 */
                        (void)p1; (void)p2; (void)p3;

                        // ── Rule 108 [x2]: exit/abort ────────────────────────────────────────
                        if (0) { exit(0); }           /* Rule 108 */
                        if (0) { abort(); }           /* Rule 108 */

                        // ── Rule 123 [x2]: int to pointer ────────────────────────────────────
                        void* vp1 = (void*)42;        /* Rule 123 */
                        void* vp2 = (void*)0xDEADU;  /* Rule 123 */
                        (void)vp1; (void)vp2;

                        // ── Rule 124 [x2]: pointer to int ────────────────────────────────────
                        int pi1 = (int)pa;            /* Rule 124 */
                        int pi2 = (int)pb;            /* Rule 124 */
                        (void)pi1; (void)pi2;

                        // ── Rule 161 [x2]: pointer to pointer ────────────────────────────────
                        int* pp1 = (int*)vp1;        /* Rule 161 */
                        char* pp2 = (char*)vp1;       /* Rule 161 */
                        (void)pp1; (void)pp2;

                        // ── Rule 125 [x2]: conditional type mismatch ─────────────────────────
                        float  cf1 = 1.0F;
                        int    ci1 = (v > 0) ? cf1 : 10;          /* Rule 125 */
                        double dc3 = (v > 0) ? (float)0.5F : 100; /* Rule 125 */
                        (void)ci1; (void)dc3;

                        // ── Rule 127 [x2]: int to float implicit ─────────────────────────────
                        int   ii1 = 7, ii2 = 3;
                        float ff1 = ii1;              /* Rule 127 */
                        float ff2 = ii2;              /* Rule 127 */
                        (void)ff1; (void)ff2;

                        // ── Rule 128 [x2]: narrowing double to float ──────────────────────────
                        double dd1 = 3.14, dd2 = 2.71;
                        float  nf1 = dd1;             /* Rule 128 */
                        float  nf2 = dd2;             /* Rule 128 */
                        (void)nf1; (void)nf2;

                        // ── Rule 129 [x2]: narrowing long to short ────────────────────────────
                        long  ll1 = 9999L, ll2 = 1234L;
                        short ns1 = ll1;              /* Rule 129 */
                        short ns2 = ll2;              /* Rule 129 */
                        (void)ns1; (void)ns2;

                        // ── Rule 136 [x3]: magic numbers ─────────────────────────────────────
                        int mn1 = 42;                 /* Rule 136 */
                        int mn2 = 255;                /* Rule 136 */
                        int mn3 = 1024;               /* Rule 136 */
                        (void)mn1; (void)mn2; (void)mn3;

                        // ── Rule 137 [x3]: literal subscript ─────────────────────────────────
                        int va1 = arr[2];             /* Rule 137 */
                        int va2 = arr[3];             /* Rule 137 */
                        int va3 = arr[5];             /* Rule 137 */
                        (void)va1; (void)va2; (void)va3;

                        // ── Rule 139 [x2]: float cast to non-float ───────────────────────────
                        double dv1 = 9.9, dv2 = 3.3;
                        int    iv1 = (int)dv1;       /* Rule 139 */
                        long   iv2 = (long)dv2;      /* Rule 139 */
                        (void)iv1; (void)iv2;

                        // ── Rule 163 [x3]: ++/-- mixed with operators ─────────────────────────
                        int y = 5;
                        int ro1 = y++ + 1;           /* Rule 163 */
                        int ro2 = --y + 2;           /* Rule 163 */
                        int ro3 = y++ * 3;           /* Rule 163 */
                        (void)ro1; (void)ro2; (void)ro3;

                        // ── Rule 165 [x3]: && || non-bool operands ───────────────────────────
                        int a = 1, b = 2, c = 3;
                        if (a && b) { a = 0; }  /* Rule 165 */
                        if (a || c) { a = 0; }  /* Rule 165 */
                        if (a && b && c) { a = 0; }  /* Rule 165 */

                        // ── Rule 25 [x2]: side effects in && || ──────────────────────────────
                        int p = 1, q = 0;
                        if (p && q++) { p = 0; }     /* Rule 25 */
                        if (p || ++q) { p = 0; }   /* Rule 25 */

                        // ── Rule 166 [x2]: unary minus on unsigned ────────────────────────────
                        unsigned int u1 = 5U, u2 = 10U;
                        unsigned int ng1 = -u1;      /* Rule 166 */
                        unsigned int ng2 = -u2;      /* Rule 166 */
                        (void)ng1; (void)ng2;

                        // ── Rule 171/32 [x2]: comma operator ─────────────────────────────────
                        int ca = 0, cb = 0, cc = 0;
                        int cd = (ca = 1, cb = 2);   /* Rule 171 + Rule 32 */
                        int ce = (cb = 3, cc = 4);   /* Rule 171 + Rule 32 */
                        (void)cd; (void)ce;

                        // ── Rule 181 [x3]: if without braces ─────────────────────────────────
                        int rv = 1;
                        if (rv > 0) rv++;             /* Rule 181 + Rule 46 */
                        if (rv > 1) rv--;             /* Rule 181 + Rule 46 */
                        if (rv == 2) rv = 0;          /* Rule 181 + Rule 46 */

                        // ── Rule 182 [x2]: else without braces ───────────────────────────────
                        int ev = 1;
                        if (ev > 0) { ev++; }
                        else ev--;  /* Rule 182 */
                        if (ev > 5) { ev = 0; }
                        else ev++;/* Rule 182 */
                        (void)ev;

                        // ── Rule 195 [x2]: multiple breaks in loop ────────────────────────────
                        for (int k2 = 0; k2 < 10; k2++) {
                            if (k2 == 3) break;       /* break 1 */
                            if (k2 == 7) break;       /* Rule 195 */
                        }
                        int w2 = 0;
                        while (w2 < 20) {
                            if (w2 == 5)  break;      /* break 1 */
                            if (w2 == 15) break;      /* Rule 195 */
                            w2++;
                        }

                        // ── Rule 39 [x2]: dead code after goto ───────────────────────────────
                        {
                            int dead_a = 0;
                            goto skip_a;
                            dead_a = 99;              /* Rule 39 */
                        skip_a:
                            (void)dead_a;
                        }

                        // ── Rule 15 [x3]: variable shadowing ─────────────────────────────────
                        {
                            int x = 100;              /* Rule 15 */
                            (void)x;
                        }
                        {
                            int v = 200;              /* Rule 15 */
                            (void)v;
                        }
                        {
                            int y = 300;              /* Rule 15 */
                            (void)y;
                        }

                        // ── Rule 56 [x2]: variadic function calls ────────────────────────────
                        var_logger(1, 42, 99);        /* Rule 56 call */
                        var_logger(2, 10, 20, 30);   /* Rule 56 call */

                        // =========================================================================
                        // TYPE C RULES
                        // =========================================================================

                        // Rule 1 [x2]: non-standard characters in string literals
                        const char* r1a = "hello\x80world"; /* Rule 1 */
                        const char* r1b = "data\xFF";        /* Rule 1 */
                        (void)r1a; (void)r1b;

                        // Rule 22 [x3]: automatic variable without initializer
                        {
                            int  r22a;                /* Rule 22 */
                            char r22b;                /* Rule 22 */
                            long r22c;                /* Rule 22 */
                            (void)r22a; (void)r22b; (void)r22c;
                        }

                        // Rule 126 [x3]: constant branch condition (infeasible code)
                        if (0) { int dead = 1; (void)dead; }   /* Rule 126 */
                        if (1) { int always = 2; (void)always; }/* Rule 126 + Rule 181 */
                        if (2 + 2 == 4) { int c2 = 0; (void)c2; } /* Rule 126 */
                        while (0) { int never = 0; (void)never; }  /* Rule 126 */

                        // Rule 134 [x3]: negative shift RHS
                        {
                            int val = 8;
                            int signed_shift = 3;
                            int r134a = val >> -1;    /* Rule 134 */
                            int r134b = val << -2;    /* Rule 134 */
                            int r134c = val >> signed_shift; /* Rule 134 (signed var) */
                            (void)r134a; (void)r134b; (void)r134c;
                        }

                        // Rule 6 [x2]: unreachable code after return (see static functions below)

                        // =========================================================================
                        // TYPE D RULES
                        // =========================================================================

                        // Rule D3 [x2]: wide character literals shall not be used
                        wchar_t wd3c = L'A';          /* Rule D3 */
                        wchar_t wd3d = L'Z';          /* Rule D3 */
                        (void)wd3c; (void)wd3d;

                        // Rule D10: already triggered at global scope (union FloatBits1, FloatBits2)

                        // Rule D17: already triggered at global scope (g_count, g_flag, g_ratio)

                        // Rule D20 [x2]: register storage class
                        register int  rd20a = 5;     /* Rule D20 */
                        register char rd20b = 'A';   /* Rule D20 */
                        (void)rd20a; (void)rd20b;

                        // Rule D30 [x2]: unary minus on unsigned (same as Rule 166)
                        unsigned int  du1 = 8U;
                        unsigned int  du2 = 12U;
                        unsigned int  dn1 = -du1;    /* Rule D30 + Rule 166 */
                        unsigned int  dn2 = -du2;    /* Rule D30 + Rule 166 */
                        (void)dn1; (void)dn2;

                        // Rule D31 [x2]: sizeof on expression with side effects
                        int sz_var = 5;
                        size_t d31a = sizeof(sz_var++);    /* Rule D31 */
                        size_t d31b = sizeof(++sz_var);    /* Rule D31 */
                        (void)d31a; (void)d31b;

                        // Rule D33 [x3]: implicit conversions with loss of information
                        int   d33_i = 100;
                        float d33_f = d33_i;         /* Rule D33 (int to float) */
                        long  d33_l = 9999L;
                        short d33_s = d33_l;         /* Rule D33 (narrowing int) */
                        double d33_d = 3.14;
                        float  d33_nf = d33_d;       /* Rule D33 (double to float) */
                        (void)d33_f; (void)d33_s; (void)d33_nf;

                        // Rule D34 [x2]: redundant explicit cast (same type)
                        int   d34_i = 10;
                        int   d34_r1 = (int)d34_i;   /* Rule D34 — redundant (int)int */
                        float d34_f = 1.0F;
                        float d34_r2 = (float)d34_f; /* Rule D34 — redundant (float)float */
                        (void)d34_r1; (void)d34_r2;

                        // Rule D35 [x2]: C-style cast involving pointers
                        int   d35_i = 42;
                        void* d35_p = (void*)&d35_i; /* Rule D35 — cast to pointer */
                        int   d35_r = (int)d35_p;    /* Rule D35 — cast from pointer */
                        (void)d35_p; (void)d35_r;

                        // Rule D42 [x2]: labels not in switch (goto labels)
                        goto d42_label1;              /* Rule 43 */
                    d42_label1:                   /* Rule D42 */
                        x = 1;
                        goto d42_label2;              /* Rule 43 */
                    d42_label2:                   /* Rule D42 */
                        x = 2;
                        (void)x;

                        // Rule D69 [x2]: null pointer dereference
                        {
                            int* null_ptr = (int*)0;
                            // Dereferencing null pointer
                            int rd69a = *((int*)0);   /* Rule D69 */
                            (void)rd69a;
                            (void)null_ptr;
                        }

                        // Rule D95: already triggered at global scope
                        //           (struct WithAnonymous1, union WithAnonymous2)

                        // Rule D101 [x2]: offsetof macro shall not be used
                        struct Point { int x; int y; };
                        size_t d101a = offsetof(struct Point, x);  /* Rule D101 */
                        size_t d101b = offsetof(struct Point, y);  /* Rule D101 */
                        (void)d101a; (void)d101b;

                        // Suppress unused-variable warnings
                        (void)r8a;(void)r8b;(void)r8c;(void)r8d;(void)r8e;(void)r8f;(void)r8g;
                        (void)cf1;(void)ii1;(void)ii2;(void)dd1;(void)dd2;(void)dv1;(void)dv2;
                        (void)ll1;(void)ll2;(void)u1;(void)u2;(void)du1;(void)du2;
                        (void)ca;(void)cb;(void)cc;(void)rv;(void)wv;
                        (void)a;(void)b;(void)c;(void)p;(void)q;
                        (void)pa;(void)pb;(void)pc;(void)arr;(void)y;
                        (void)g_count;(void)g_flag;(void)g_ratio;
                        (void)g_partial5;(void)g_partial8;
                        (void)Buffer;(void)Packet;
                        (void)wd3a;(void)wd3b;(void)sz_var;
                        (void)d33_i;(void)d33_l;(void)d33_d;(void)d34_i;(void)d34_f;(void)d35_i;
                        (void)w2;

                        return 0;
}

// ── Rule 6 [x2]: unreachable code after return ────────────────────────────
static int r6_func_a(int x)   /* Rule 58 */
{
    return x + 1;
    x = 99;                    /* Rule 6: unreachable */
}

static int r6_func_b(int x)   /* Rule 58 */
{
    if (x > 0) {
        return x;
        x = 0;                 /* Rule 6: unreachable */
    }
    return -x;
}

// ── Rule 22 [x3]: automatic vars without initializer ─────────────────────
static void r22_func(void)    /* Rule 58 */
{
    int  r22a;                 /* Rule 22 */
    char r22b;                 /* Rule 22 */
    long r22c;                 /* Rule 22 */
    (void)r22a; (void)r22b; (void)r22c;
}

// ── Rule 88 [x2]: function pointer type mismatch ─────────────────────────
typedef void (*VoidIntFn)(int);
static void  takes_int(int x) { (void)x; }  /* Rule 58 */
static int   returns_int(void) { return 0; } /* Rule 58 + Rule 59 */

static void r88_func(void)       /* Rule 58 */
{
    VoidIntFn fp1 = (VoidIntFn)returns_int; /* Rule 88 + Rule 161 */
    VoidIntFn fp2 = (VoidIntFn)takes_int;   /* Rule 88 + Rule 161 */
    (void)fp1; (void)fp2;
}

