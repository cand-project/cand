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
#define CAND_RETURNS_BORROW CAND_ANNOTATE("cand:returns_borrow")
#define CAND_SAFE CAND_ANNOTATE("cand:safe")
#define CAND_UNSAFE CAND_ANNOTATE("cand:unsafe")
#define CAND_MOVE(x) (x)

#endif
