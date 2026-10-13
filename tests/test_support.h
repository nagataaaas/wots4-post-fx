#pragma once
#include <cstdio>
namespace test {
inline unsigned checks = 0, failures = 0;
inline void check(const char* label, bool success) {
    ++checks;
    if (!success) ++failures;
    std::printf("%s: %s\n", success ? "PASS" : "FAIL", label);
}
inline int finish() {
    std::printf("RESULT: %u checks passed, %u failed\n", checks - failures, failures);
    std::puts("No writes to the installation, saves or game process.");
    return failures ? 1 : 0;
}
}
