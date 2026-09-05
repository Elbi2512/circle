#include <stdio.h>
#include <sys/reent.h>
#include <stdarg.h>
#include <circle/util.h>

/* Newlib reentrancy structuur definiëren zodat het type exact overeenkomt */
static struct _reent s_reent;
struct _reent *_impure_ptr = &s_reent;

int fprintf(FILE *stream, const char *format, ...)
{
    (void)stream;
    (void)format;
    return 0;
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = 0;
    // circle_vsnprintf(str, size, format, args);
    va_end(args);
    return ret;
}