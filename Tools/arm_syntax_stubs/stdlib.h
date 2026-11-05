#ifndef R10_STDLIB_H
#define R10_STDLIB_H
#include <stddef.h>
void *malloc(size_t); void free(void*);
long strtol(const char *, char **, int);
int atoi(const char *); int abs(int); void abort(void);
#endif
