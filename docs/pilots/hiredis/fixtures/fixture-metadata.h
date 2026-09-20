#ifndef CAND_HIREDIS_FIXTURE_METADATA_H
#define CAND_HIREDIS_FIXTURE_METADATA_H

#if defined(CAND_FIXTURE_METADATA)
#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))
#define CAND_DESTROYS __attribute__((annotate("cand:destroys")))
#define CAND_BORROW __attribute__((annotate("cand:borrow")))
#define CAND_TAKES __attribute__((annotate("cand:takes")))
#define CAND_MOVE(value) value
#else
#define CAND_RETURNS_OWN
#define CAND_DESTROYS
#define CAND_BORROW
#define CAND_TAKES
#define CAND_MOVE(value) value
#endif

#endif
