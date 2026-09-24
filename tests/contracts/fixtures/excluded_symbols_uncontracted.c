#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/*
 * EXCLUDED-CLASS fixture: symbols that must never appear in a reviewed
 * bundle (docs/contracts/EXTERNAL-API-TRUST-MODEL.md section 3).
 *
 *   strtol         writes through a char **endptr (pointer-to-pointer
 *                  output; the #41 boundary). ANY contract on such a
 *                  function would activate the summary path and suppress
 *                  the fail-closed unknown-call-with-pointer-output
 *                  obligation, so it stays uncontracted.
 *   strtok_r       writes through char **saveptr (pointer-to-pointer
 *                  state output) — same exclusion class as strtol and
 *                  as the BSD strsep (also excluded; not visible under
 *                  this fixture's strict feature set).
 *   __errno_location returns a pointer to thread-local storage (no
 *                  accepted class can express the lifetime).
 *
 * All calls must keep INCOMPLETE verdicts with the merged reviewed
 * contracts active.
 */
int run(const char *text) {
    char *end = 0;
    long v = strtol(text, &end, 10); /* pointer-output: stays uncontracted */
    char *save = 0;
    char *tok = strtok_r("a,b", ",", &save); /* state output: uncontracted */
    int e = *(__errno_location()) != 0; /* thread-local return: uncontracted */
    return (int)(v & 1) + e + (tok ? 1 : 0);
}

int main(void) { return run("42"); }
