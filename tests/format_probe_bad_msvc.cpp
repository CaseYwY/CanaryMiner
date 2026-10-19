// Negative probe for the MSVC build-hygiene gate: this file must NOT compile with /we4477 (C4477, printf format mismatch).
// The Linux probe (format_probe_bad.cpp) reproduces the Gate 1a '%' bug; MSVC does not flag that case, so this one uses a type mismatch.
#include <cstdio>
int main() {
    printf("fee: %d\n", "1% of productive mining time");
    return 0;
}
