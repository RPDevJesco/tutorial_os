# Drivers - Talking to Hardware

Device drivers are the code that knows how to communicate with specific
hardware. Each peripheral has its own protocol, registers, and quirks.

## 📁 Structure

```
drivers/
├── gpio/           # General Purpose I/O pins
├── mailbox/        # GPU communication (VideoCore)
├── sdcard/         # SD card (SDHOST controller)
├── audio/          # PWM audio output
└── usb/            # USB host (DWC2 controller)
```

## 🔌 How Drivers Work

### Memory-Mapped I/O (MMIO)
Hardware registers appear as memory addresses. To control hardware:
1. Write to specific addresses to send commands
2. Read from addresses to get status

```c
// Write to a hardware register
#define GPIO_SET  0x3F20001C
*(volatile uint32_t *)GPIO_SET = (1 << 17);  // Set pin 17 high

// Read from a hardware register
#define GPIO_LEV  0x3F200034
uint32_t pins = *(volatile uint32_t *)GPIO_LEV;  // Read all pin levels
```

### The `volatile` Keyword
Critical for hardware access! Tells the compiler:
- Don't cache reads (hardware might change the value)
- Don't skip writes (hardware needs them)
- Don't reorder operations (protocols matter)

### Memory Barriers
On ARM, memory operations can be reordered. We use barriers:
```c
__asm__ volatile("dmb sy" ::: "memory");
```
This ensures all previous memory accesses complete before continuing.

## 📚 Driver Complexity Spectrum

From simplest to most complex:

### 1. GPIO (Simplest)
- Just read/write registers
- No state machine
- No interrupts
- ~200 lines

### 2. Mailbox
- Simple request/response protocol
- Polling (no interrupts)
- ~300 lines

### 3. SD Card
- Multi-step initialization sequence
- State machine for card detection
- Command/response protocol
- ~500 lines

### 4. Audio
- Clock configuration
- DMA or FIFO management
- Real-time constraints
- ~600 lines

### 5. USB (Most Complex)
- Full protocol stack
- Multiple transfer types
- Device enumeration
- ~2000+ lines

## 🎯 Reading Order

1. **gpio/** - Start here! Simple register manipulation
2. **mailbox/** - Learn the GPU communication pattern
3. **sdcard/** - Understand state machines and protocols
4. **audio/** - See clock configuration and real-time concerns
5. **usb/** - Deep dive into complex protocol stacks

## 🔧 Common Patterns

### Register Definitions
```c
#define DEVICE_BASE     0x3F200000
#define DEVICE_CTRL     (DEVICE_BASE + 0x00)
#define DEVICE_STATUS   (DEVICE_BASE + 0x04)
#define DEVICE_DATA     (DEVICE_BASE + 0x08)
```

### Bit Manipulation
```c
// Set bits
reg |= (1 << BIT_NUMBER);

// Clear bits  
reg &= ~(1 << BIT_NUMBER);

// Check bits
if (reg & (1 << BIT_NUMBER)) { ... }

// Modify field (clear then set)
reg = (reg & ~FIELD_MASK) | (value << FIELD_SHIFT);
```

### Polling vs Interrupts
Our drivers use polling (checking status in a loop) for simplicity.
Production code often uses interrupts for efficiency.

```c
// Polling (simple, but wastes CPU)
while (!(mmio_read(STATUS) & READY_BIT)) {
    // spin
}

// Interrupts (efficient, but complex setup)
// CPU sleeps until hardware signals completion
```

## 📖 Further Reading

Each driver directory has its own README with hardware-specific details.
