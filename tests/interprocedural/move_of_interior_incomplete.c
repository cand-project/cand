#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area C: ownership transfer of an interior
 * cursor.
 *
 * Exercises the transferBinding (TakeOwnership) path. No reviewed
 * bundle carries a consumes-effect symbol callable from plain C, so
 * the ownership transfer is exercised through a same-TU function with
 * a cand:takes parameter annotation whose body frees at the end (the
 * parameter_owner_move_then_use_fail.c mechanism: the annotation sets
 * TakeOwnership and the consuming body does not downgrade it).
 *
 * The cursor advances by a pure integer delta (relation Interior after
 * the Area C repair) and is then moved into the taking callee; the
 * exact-base-required destruction predicate in transferBinding must
 * flag the move-of-interior.
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE (poisoned cursor; exact rows recorded
 *                    in the milestone verification artifacts).
 *   AFTER  (post-fix): INCOMPLETE (move-of-interior obligation; never
 *                      a PASS).
 */
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_TAKES CAND_A("cand:takes")

static void take_and_free(int *p CAND_TAKES) { free(p); }

static void move_advanced(int *base) {
    int *p = base;
    p += 1;
    take_and_free(p);
}

int main(void) {
    int *buf = malloc(4 * sizeof(int));
    if (!buf) return 1;
    move_advanced(buf);
    return 0;
}
