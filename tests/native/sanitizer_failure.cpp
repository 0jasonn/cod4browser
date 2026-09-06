#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Only the parent policy test runs this deliberately invalid synthetic code.
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    std::fputs("KISAK_SANITIZER_CHILD_STARTED\n", stderr);
    if (std::strcmp(argv[1], "asan") == 0) {
        volatile char *bytes = static_cast<char *>(std::malloc(4));
        if (!bytes) return 3;
        bytes[4] = 1;
        std::free(const_cast<char *>(bytes));
    } else if (std::strcmp(argv[1], "ubsan") == 0) {
        volatile int value = INT_MAX;
        volatile int overflow = value + 1;
        (void)overflow;
    } else return 2;
    std::fputs("KISAK_SANITIZER_CHILD_RETURNED\n", stderr);
    return 0;
}
