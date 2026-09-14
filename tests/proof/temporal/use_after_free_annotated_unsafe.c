#include <cand/cand.h>
#include <stdlib.h>

int main(void)
{
    int *p CAND_OWN = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    *p = 7;
    free(p);

    /*
     * C& metadata is not a runtime mitigation. If cand checking is bypassed,
     * this remains the same ordinary-C temporal defect and ASan must observe it.
     * A future strict cand1 check is expected to reject this before production.
     */
    volatile int observed = *p;
    (void)observed;
    return 0;
}
