#ifndef CAND_CAND_H
#define CAND_CAND_H

#if defined(CAND_ANALYSIS) && defined(__clang__)
#define CAND_ANNOTATE(value) __attribute__((annotate(value)))
#else
#define CAND_ANNOTATE(value)
#endif

#define CAND_OWN CAND_ANNOTATE("cand:own")
#define CAND_BORROW CAND_ANNOTATE("cand:borrow_shared")
#define CAND_BORROW_MUT CAND_ANNOTATE("cand:borrow_mut")
#define CAND_TAKES CAND_ANNOTATE("cand:takes")
#define CAND_DESTROYS CAND_ANNOTATE("cand:destroys")
#define CAND_RETURNS_OWN CAND_ANNOTATE("cand:returns_own")
#define CAND_DETAIL_STRINGIFY_1(value) #value
#define CAND_DETAIL_STRINGIFY(value) CAND_DETAIL_STRINGIFY_1(value)
#define CAND_RETURNS_BORROW_FROM(n) \
    CAND_ANNOTATE("cand:returns_borrow_from:" CAND_DETAIL_STRINGIFY(n))
#define CAND_SAFE CAND_ANNOTATE("cand:safe")
#define CAND_UNSAFE CAND_ANNOTATE("cand:unsafe")
/* Analysis records CAND_MOVE as an ownership transition; production C sees
 * exactly the original expression and no runtime move operation. */
#define CAND_MOVE(x) (x)

#endif
