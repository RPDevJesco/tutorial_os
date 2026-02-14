#ifndef TYPES_H
#define TYPES_H

/* Fixed-width integer types — use compiler builtins to avoid
 * conflicts with <stdint.h> on LP64 targets (ARM64, RV64)
 * where 'long' and 'long long' are both 64-bit but different types. */
typedef signed char         int8_t;
typedef unsigned char       uint8_t;
typedef signed short        int16_t;
typedef unsigned short      uint16_t;
typedef signed int          int32_t;
typedef unsigned int        uint32_t;
typedef __INT64_TYPE__      int64_t;
typedef __UINT64_TYPE__     uint64_t;

typedef __UINTPTR_TYPE__    uintptr_t;
typedef __INTPTR_TYPE__     intptr_t;
typedef __SIZE_TYPE__       size_t;

/* Boolean */
typedef _Bool bool;
#define true  1
#define false 0

/* NULL */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Utility macros */
#define ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))
#define UNUSED(x)           ((void)(x))

#ifndef MIN
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#endif

#define BIT(n)              (1UL << (n))
#define ALIGN_UP(x, align)  (((x) + (align) - 1) & ~((align) - 1))

#endif /* TYPES_H */