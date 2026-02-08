# Common - less than minimal libc

This directory mostly contains the headers that compilers expect to exist when compiling.

## 📁 Files

### string.c
the compiler needs actual linkable symbols for its auto-generated calls.

### string.h
String and Memory Function Declarations

### types.h
Utilities and fixed types.

### mmio.h
low-level hardware access primitives used by all drivers for ARM devices.