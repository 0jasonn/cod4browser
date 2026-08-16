#include <qcommon/system.h>
#include <universal/assertive.h>

#include <cstdarg>
#include <cstdio>

void MyAssertHandler(const char *filename, int line, int type, const char *format, ...)
{
    char message[1024]{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format ? format : "", arguments);
    va_end(arguments);
    message[sizeof(message) - 1] = '\0';
    Sys_Error("Assertion failed at %s:%d (type %d): %s", filename ? filename : "?", line, type, message);
}
