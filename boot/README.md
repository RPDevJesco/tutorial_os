# Boot - Where Everything Begins

This directory contains the code that runs first when the Raspberry Pi powers on.

## 🔌 What Happens at Power-On?

When you apply power to a Raspberry Pi, here's what happens:

1. **GPU Wakes First** (not the CPU!)
   - The VideoCore GPU is the "primary" processor
   - It reads `bootcode.bin` from the SD card
   - Then loads `start.elf` (the GPU firmware)

2. **GPU Reads config.txt**
   - Configures memory split, display, etc.
   - Determines which kernel file to load

3. **GPU Loads Your Kernel**
   - Loads `kernel8.img` (for 64-bit) to address 0x80000
   - Sets up a minimal device tree
   - Releases the ARM CPU from reset

4. **Your Code Runs!**
   - CPU starts executing at 0x80000
   - This is `boot.S` - your first instructions
   - You must set up everything: stack, BSS, MMU...

## 📁 Files

### boot.S - First Instructions
The very first code to execute. Written in assembly because:
- Need precise control over CPU state
- Can't use C until we have a stack
- Must configure MMU before normal memory access

Key responsibilities:
- Drop from EL3 → EL2 → EL1 (exception levels)
- Query RAM size from GPU
- Set up page tables
- Initialize the stack
- Clear BSS (zero-initialized data)
- Jump to C code

### memory_layout.ld - Memory Map
A "linker script" that tells the linker where to put things:
- Where does code go? (0x80000)
- Where do global variables go?
- How big is the stack?
- Where does the heap start?

Think of it as a blueprint for your kernel's memory layout.

## 🧠 Key Concepts

### Exception Levels (EL)
ARM64 has privilege levels (like x86 rings):
- **EL3**: Secure monitor (highest privilege)
- **EL2**: Hypervisor
- **EL1**: Kernel (where we want to run)
- **EL0**: User applications

The Pi starts at EL3, we drop down to EL1.

### BSS Section
Variables declared without initializers:
```c
int counter;           // Goes in BSS, will be 0
int values[1000];      // Goes in BSS, all zeros
```
The BSS section doesn't exist in the kernel image - we just
zero that memory at boot. Saves space!

### Stack
Function calls need a stack for:
- Return addresses
- Local variables
- Saved registers

ARM64 stack grows **downward**, so we point SP to the **top**
of our reserved stack region.

### Device Tree Blob (DTB)
The GPU passes a pointer to a data structure describing
the hardware. We can parse this to discover:
- How much RAM we have
- Which peripherals are present
- Interrupt mappings

## 🔧 Building

```bash
# Assemble boot.S
aarch64-none-elf-as boot.S -o boot.o

# Compile kernel
aarch64-none-elf-gcc -c ../kernel/main.c -o main.o

# Link with memory_layout.ld
aarch64-none-elf-ld -T memory_layout.ld boot.o main.o -o kernel.elf

# Extract raw binary
aarch64-none-elf-objcopy -O binary kernel.elf kernel8.img
```

## 📖 Further Reading

- `boot.S` - Heavily commented, read line by line
- `memory_layout.ld` - Explains each section
- ARM Architecture Reference Manual (ARMv8-A)
