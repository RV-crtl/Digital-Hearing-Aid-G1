#ifndef R10_STRING_H
#define R10_STRING_H
#include <stddef.h>
void *memcpy(void *restrict, const void *restrict, size_t);
void *memset(void *, int, size_t);
void *memmove(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
char *strncpy(char *, const char *, size_t);
char *strcpy(char *, const char *);
int strcmp(const char *, const char *);
#endif
