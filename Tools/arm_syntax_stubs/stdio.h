#ifndef R10_STDIO_H
#define R10_STDIO_H
#include <stdarg.h>
#include <stddef.h>
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
int sprintf(char *, const char *, ...);
int printf(const char *, ...);
int puts(const char *);
#endif
