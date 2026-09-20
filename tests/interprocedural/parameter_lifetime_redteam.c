#include <stdlib.h>

#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_BORROW CAND_A("cand:borrow_shared")
#define CAND_TAKES CAND_A("cand:takes")
#define CAND_DESTROYS CAND_A("cand:destroys")
#define CAND_MOVE(value) (value)

#if defined(CASE_A)
static int run(int *p CAND_BORROW) { return *p; }
int main(void) { int *p = malloc(sizeof *p); *p = 1; int r = run(p); free(p); return r; }
#elif defined(CASE_B)
static int run(int *p CAND_BORROW) { *p = 2; return *p; }
int main(void) { int *p = malloc(sizeof *p); int r = run(p); free(p); return r; }
#elif defined(CASE_C)
static int run(int *p CAND_BORROW) { free(p); return *p; }
int main(void) { int *p = malloc(sizeof *p); return run(p); }
#elif defined(CASE_D)
static int run(int *p CAND_DESTROYS) { free(p); return *p; }
int main(void) { int *p = malloc(sizeof *p); return run(p); }
#elif defined(CASE_E)
static int run(int *p CAND_DESTROYS) { free(p); *p = 3; return *p; }
int main(void) { int *p = malloc(sizeof *p); return run(p); }
#elif defined(CASE_F)
static void run(int *p CAND_DESTROYS) { free(p); free(p); }
int main(void) { int *p = malloc(sizeof *p); run(p); return 0; }
#elif defined(CASE_G)
static int run(int *p CAND_TAKES) { free(p); return *p; }
int main(void) { int *p = malloc(sizeof *p); return run(CAND_MOVE(p)); }
#elif defined(CASE_H)
static int run(int *p CAND_TAKES) { free(p); *p = 4; return *p; }
int main(void) { int *p = malloc(sizeof *p); return run(CAND_MOVE(p)); }
#elif defined(CASE_I)
static void run(int *p CAND_TAKES) { free(p); free(p); }
int main(void) { int *p = malloc(sizeof *p); run(CAND_MOVE(p)); return 0; }
#elif defined(CASE_J)
static int run(int *p CAND_TAKES) { int *q = p; free(p); return *q; }
int main(void) { int *p = malloc(sizeof *p); return run(CAND_MOVE(p)); }
#elif defined(CASE_K)
static int run(int *p CAND_TAKES) { int *q = p; free(q); return *p; }
int main(void) { int *p = malloc(sizeof *p); return run(CAND_MOVE(p)); }
#elif defined(CASE_L)
static void sink(int *p CAND_TAKES) { free(p); }
static int run(int *p CAND_TAKES) { sink(CAND_MOVE(p)); return *p; }
int main(void) { int *p = malloc(sizeof *p); return run(CAND_MOVE(p)); }
#elif defined(CASE_M)
extern void vendor_destroy(int *p);
int main(void) { int *p = malloc(sizeof *p); vendor_destroy(p); return *p; }
#elif defined(CASE_N)
static int run(int *p CAND_TAKES, int flag) { if (flag) free(p); return *p; }
int main(void) { int *p = malloc(sizeof *p); return run(CAND_MOVE(p), 1); }
#elif defined(CASE_O)
static int run(int *p CAND_TAKES, int flag) { while (flag--) free(p); return *p; }
int main(void) { int *p = malloc(sizeof *p); return run(CAND_MOVE(p), 1); }
#else
#error "select one CASE_* fixture"
#endif
