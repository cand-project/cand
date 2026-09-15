static int read_value(int *p) { return *p; }
int main(void) { int x = 3; return read_value(&x) == 3 ? 0 : 1; }
