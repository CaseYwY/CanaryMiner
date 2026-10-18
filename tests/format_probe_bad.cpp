// Negative probe for the build-hygiene gate: this file must NOT compile with -Wformat -Werror=format.
// It reproduces the Gate 1a bug: a literal '%' inside a string that is used as a printf format.
#include <cstdio>
int main() {
    printf("Developer fee: 1% of productive mining time\n");
    return 0;
}
