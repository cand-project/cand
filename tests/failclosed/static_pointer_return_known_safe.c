/*
 * Over-trigger guard: returning a pointer to static storage or to a string
 * literal is legal and must not be reported as a stack escape.
 *
 * Required P0.1 result: PASS.
 */
static char buffer[8] = "cand";

const char *name(void)
{
    return "cand";
}

char *get_buffer(void)
{
    return buffer;
}

int main(void)
{
    return 0;
}
