#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/reent.h>

/* Newlib reentrancy stub */
static struct _reent s_reent;
struct _reent *_impure_ptr = &s_reent;

/* Stille fprintf stub */
int fprintf(FILE *stream, const char *format, ...)
{
    (void)stream;
    (void)format;
    return 0;
}

/* Helper om veilig binnen de buffer te schrijven */
static void put_c(char *str, size_t size, size_t *pos, char c)
{
    if (*pos + 1 < size) {
        str[*pos] = c;
    }
    (*pos)++;
}

/* Volledig zelfstandige ANSI C snprintf (geen libc of externe linkersymbolen nodig) */
int snprintf(char *str, size_t size, const char *format, ...)
{
    if (!str && size > 0) return -1;

    va_list args;
    va_start(args, format);

    size_t pos = 0;

    for (const char *p = format; *p != '\0'; p++) {
        if (*p != '%') {
            put_c(str, size, &pos, *p);
            continue;
        }

        p++; // sla '%' over
        if (*p == '\0') break;

        // Eenvoudige width padding parsing (zoals %02X, %04X, %2d)
        char pad_char = ' ';
        int min_width = 0;
        if (*p == '0') {
            pad_char = '0';
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            min_width = min_width * 10 + (*p - '0');
            p++;
        }

        if (*p == 's') {
            const char *s = va_arg(args, const char *);
            if (!s) s = "(null)";
            while (*s) {
                put_c(str, size, &pos, *s++);
            }
        } else if (*p == 'c') {
            char c = (char)va_arg(args, int);
            put_c(str, size, &pos, c);
        } else if (*p == 'd' || *p == 'i') {
            int val = va_arg(args, int);
            if (val < 0) {
                put_c(str, size, &pos, '-');
                val = -val;
            }
            char buf[16];
            int i = 0;
            if (val == 0) buf[i++] = '0';
            while (val > 0 && i < (int)sizeof(buf)) {
                buf[i++] = (char)('0' + (val % 10));
                val /= 10;
            }
            while (i < min_width && i < (int)sizeof(buf)) {
                buf[i++] = pad_char;
            }
            while (i > 0) {
                put_c(str, size, &pos, buf[--i]);
            }
        } else if (*p == 'x' || *p == 'X') {
            unsigned int val = va_arg(args, unsigned int);
            const char *digits = (*p == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char buf[16];
            int i = 0;
            if (val == 0) buf[i++] = '0';
            while (val > 0 && i < (int)sizeof(buf)) {
                buf[i++] = digits[val & 0xF];
                val >>= 4;
            }
            while (i < min_width && i < (int)sizeof(buf)) {
                buf[i++] = pad_char;
            }
            while (i > 0) {
                put_c(str, size, &pos, buf[--i]);
            }
        } else if (*p == '%') {
            put_c(str, size, &pos, '%');
        } else {
            put_c(str, size, &pos, '%');
            put_c(str, size, &pos, *p);
        }
    }

    if (size > 0) {
        if (pos < size) {
            str[pos] = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }

    va_end(args);
    return (int)pos;
}