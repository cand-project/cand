#include <stdlib.h>
struct W { int *damage; }; struct P { struct W weapon; };
int main(void) { struct P p = {0}; p.weapon.damage = malloc(sizeof *p.weapon.damage); free(p.weapon.damage); return *p.weapon.damage; }
