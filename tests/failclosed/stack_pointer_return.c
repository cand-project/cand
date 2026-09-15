/*
 * Returning a pointer to automatic storage is always a lifetime bug, yet it
 * involves no tracked heap object and would previously pass silently.
 *
 * Required P0.1 result: INCOMPLETE (stack-pointer-return).
 */
int *leak_local(void)
{
    int x = 5;
    return &x;
}

int main(void)
{
    int *p = leak_local();
    return *p;
}
