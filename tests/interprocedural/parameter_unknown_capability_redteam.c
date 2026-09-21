#include <stdlib.h>

/*
 * Parameter-identity repair red-team: adversarial matrix for pointer parameters
 * whose verified summary is ParamEffect::Unknown (no proven borrow or
 * ownership-transfer authority; introduced by the seedParameterState() repair that
 * tracks such parameters as live-at-entry objects with ParameterCapability::Unknown).
 *
 * These cases exist precisely because the incident matrix
 * (parameter_lifetime_redteam.c) only exercises explicitly annotated parameters
 * (CAND_BORROW / CAND_TAKES / CAND_DESTROYS). The Unknown capability is the class
 * that ordinary, unannotated C overwhelmingly lands in, and is where the repair
 * changed behavior. Every case below is a deliberate soundness probe:
 *
 *   - any escape of an Unknown-capability parameter to an opaque callee must be
 *     reported (never a silent PASS);
 *   - destruction or transfer of an Unknown-capability parameter must stay
 *     fail-closed or be reported as a violation;
 *   - a parameter derived-by-return only (borrow-from-arg) remains safe.
 *
 * Select one case with -DCASE_UNKNOWN_<TAG>.
 */

extern void external(void *); /* opaque external call, no contract, no summary */

struct S { int a; int b; };

/* UNKNOWN_ESCAPE: parameter's only use is an escape to an opaque call.
 * Must be INCOMPLETE (the escape is now the tracked-pointer obligation). */
#if defined(CASE_UNKNOWN_ESCAPE)
static void run(struct S *p) { external(p); }
int main(void) { struct S *p = malloc(sizeof *p); if (!p) return 0; run(p); free(p); return 0; }

/* UNKNOWN_ESCAPE_READ: escape then read; read must stay clean, escape flagged. */
#elif defined(CASE_UNKNOWN_ESCAPE_READ)
static int run(struct S *p) { external(p); return p->a; }
int main(void) { struct S *p = malloc(sizeof *p); if (!p) return 0; return run(p); }

/* UNKNOWN_ESCAPE_FREE_READ: escape, then local free, then read.
 * Must be FAIL (definite use-after-destroy from the local free), never PASS. */
#elif defined(CASE_UNKNOWN_ESCAPE_FREE_READ)
static int run(struct S *p) { external(p); free(p); return p->a; }
int main(void) { struct S *p = malloc(sizeof *p); if (!p) return 0; return run(p); }

/* UNKNOWN_COND_FREE_READ: conditional destroy then use -> FAIL (possible UAF),
 * identical to the qualified local semantics. */
#elif defined(CASE_UNKNOWN_COND_FREE_READ)
static int run(struct S *p, int flag) { if (flag) free(p); return p->a; }
int main(void) { struct S *p = malloc(sizeof *p); if (!p) return 0; return run(p, 1); }

/* UNKNOWN_ALIAS_FREE_READ: destroy through one alias, read through the other.
 * An opaque escape keeps p genuinely Unknown (so this exercises the Unknown
 * capability, unlike a bare `q=p; free(q)` which the summary would classify as
 * a Borrow-capability object). The tracked end-of-lifetime must be visible
 * through every alias -> FAIL (definite use after object destruction). */
#elif defined(CASE_UNKNOWN_ALIAS_FREE_READ)
static int run(struct S *p) { external(p); struct S *q = p; free(q); return p->a; }
int main(void) { struct S *p = malloc(sizeof *p); if (!p) return 0; return run(p); }

/* UNKNOWN_DOUBLE_FREE: destroy an Unknown-capability parameter twice -> FAIL (definite). */
#elif defined(CASE_UNKNOWN_DOUBLE_FREE)
static void run(struct S *p) { free(p); free(p); }
int main(void) { struct S *p = malloc(sizeof *p); if (!p) return 0; run(p); return 0; }

/* UNKNOWN_FREE_READ: unconditional destroy then use -> FAIL (definite UAF). */
#elif defined(CASE_UNKNOWN_FREE_READ)
static int run(struct S *p) { free(p); return p->a; }
int main(void) { struct S *p = malloc(sizeof *p); if (!p) return 0; return run(p); }

/* UNKNOWN_MOVE: attempt to transfer an Unknown-capability parameter to an opaque
 * (indirect) consuming sink. Because the sink is opaque the body cannot refine the
 * parameter to TakeOwnership, so the capability stays Unknown and the transfer
 * attempt must stay fail-closed: the tracked pointer reaches the indirect call and
 * produces an unknown-call-with-tracked-pointer:indirect obligation -> INCOMPLETE,
 * never a silent move/PASS. (A transfer to a KNOWN consuming callee is correctly
 * reclassified TakeOwnership by summary inference and is not an Unknown transfer.) */
#elif defined(CASE_UNKNOWN_MOVE)
#define CAND_MOVE(x) (x)
static void run(struct S *p, void (*sink)(void *)) { sink(CAND_MOVE(p)); }
int main(void) { struct S *p = malloc(sizeof *p); if (!p) return 0; run(p, external); return 0; }

/* UNKNOWN_RETURN_PARAM: returning the parameter directly declares a borrow-from-arg
 * (body-derived BorrowFromArg summary) -> PASS; the return relationship is explicit. */
#elif defined(CASE_UNKNOWN_RETURN_PARAM)
static struct S *run(struct S *p) { return p; }
int main(void) { struct S *p = malloc(sizeof *p); if (!p) return 0; struct S *q = run(p); free(q); return 0; }

#else
#error "select one CASE_UNKNOWN_* fixture"
#endif
