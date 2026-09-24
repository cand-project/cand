/* #41 Gate B: array-element destination. */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *arr[4];
    arr[0] = NULL;
    po_always(&arr[0]);
    *arr[0] = 1;
    free(arr[0]);
    return 0;
}
