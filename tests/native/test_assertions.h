#pragma once

// Forced into every translation unit in the test directory, including new
// targets. Production targets outside this directory retain their own policy.
#ifdef NDEBUG
#error "Runtime test assertions must remain enabled in optimized builds"
#endif
