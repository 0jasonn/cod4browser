#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <csignal>

int main()
{
    // Avoid native crash UI/core dumps; exit only after assert raises SIGABRT.
    std::signal(SIGABRT, [](int) { std::_Exit(86); });
    std::fputs("KISAK_ASSERTION_CHILD_STARTED\n", stderr);
    assert(!"KISAK_INTENTIONAL_ASSERTION_FAILURE");
    return 0;
}
