/*
 * string.c - Memory and String Functions for Bare-Metal
 * ======================================================
 *
 * WHY THIS FILE EXISTS:
 * ---------------------
 * Even with -fno-builtin, GCC sometimes generates calls to memset/memcpy
 * for operations like:
 *   - Struct initialization: struct foo x = {0};
 *   - Large struct copies
 *   - Array zeroing
 *
 * The inline versions in types.h work for explicit calls, but the compiler
 * needs actual linkable symbols for its auto-generated calls.
 *
 * These implementations are intentionally simple and unoptimized for clarity.
 * A production OS would use optimized assembly versions.
 */

#include "string.h"

/*
 * memset - Fill memory with a byte value
 *
 * This is THE most commonly auto-generated function. GCC uses it for:
 *   - Zeroing structs/arrays
 *   - Initializing large local variables
 *
 * @param s     Destination pointer
 * @param c     Byte value to fill (only low 8 bits used)
 * @param n     Number of bytes to fill
 * @return      Original destination pointer
 */
void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

/*
 * memcpy - Copy memory (non-overlapping regions only)
 *
 * GCC uses this for struct assignments and passing large structs by value.
 *
 * WARNING: Source and destination must not overlap!
 *          Use memmove() for overlapping regions.
 *
 * @param dest  Destination pointer
 * @param src   Source pointer
 * @param n     Number of bytes to copy
 * @return      Original destination pointer
 */
void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/*
 * memmove - Copy memory (handles overlapping regions)
 *
 * Safer than memcpy but slightly slower. Use when source and
 * destination might overlap.
 *
 * @param dest  Destination pointer
 * @param src   Source pointer
 * @param n     Number of bytes to copy
 * @return      Original destination pointer
 */
void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    if (d < s) {
        /* Copy forward */
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        /* Copy backward to handle overlap */
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

/*
 * memcmp - Compare memory regions
 *
 * @param s1    First memory region
 * @param s2    Second memory region
 * @param n     Number of bytes to compare
 * @return      0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

/*
 * strlen - Get string length
 *
 * @param s     Null-terminated string
 * @return      Number of characters (not including null terminator)
 */
size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

/*
 * strcmp - Compare two strings
 *
 * @param s1    First string
 * @param s2    Second string
 * @return      0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/*
 * strncmp - Compare two strings up to n characters
 *
 * @param s1    First string
 * @param s2    Second string
 * @param n     Maximum characters to compare
 * @return      0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/*
 * strcpy - Copy string
 *
 * @param dest  Destination buffer (must be large enough!)
 * @param src   Source string
 * @return      Original destination pointer
 */
char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

/*
 * strncpy - Copy string up to n characters
 *
 * Note: If src is shorter than n, dest is padded with nulls.
 *       If src is n or longer, dest is NOT null-terminated!
 *
 * @param dest  Destination buffer
 * @param src   Source string
 * @param n     Maximum characters to copy
 * @return      Original destination pointer
 */
char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (n && (*d++ = *src++) != '\0') {
        n--;
    }
    while (n--) {
        *d++ = '\0';
    }
    return dest;
}
