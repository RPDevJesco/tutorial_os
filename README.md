# Tutorial-OS Hardware Abstraction Layer (HAL)

A multi-platform bare-metal operating system designed to teach low-level systems programming through hands-on hardware interaction.

## Supported Platforms

| Board | SoC | Status |
|-------|-----|--------|
| Raspberry Pi Zero 2W + GPi Case | BCM2710 | ✅ Complete |
| Raspberry Pi 4B / CM4 | BCM2711 | ✅ Complete |
| Raspberry Pi 5 / CM5 | BCM2712 | ❌ InComplete |
| Radxa Rock 2A | RK3528A | ❌ InComplete |
| LattePanda Iota | ??? | ❌ InComplete |
| Orange Pi RV 2 | ??? | ❌ InComplete |
| Libre Le Potato | AML-s905X-CC | ❌ InComplete |


## Directory Structure

```
tutorial-os/
├── hal/                        # Hardware Abstraction Layer interfaces
│   ├── hal.h                   # Master include
│   ├── hal_types.h             # Types, error codes, MMIO
│   ├── hal_platform.h          # Platform info, temp, clocks
│   ├── hal_timer.h             # Timing and delays
│   ├── hal_gpio.h              # GPIO control
│   └── hal_display.h           # Display initialization
│
├── soc/                        # SoC-specific implementations
│   ├── bcm2710/                # Pi Zero 2W, Pi 3
│   ├── bcm2711/                # Pi 4, CM4
│   └── rk3528a/                # Radxa Rock 2A
│
├── board/                      # Board-specific configurations
│   ├── rpi-zero2w-gpi/
│   │   ├── board.mk            # Build configuration
│   │   └── boot/               # Boot files for SD card
│   │       ├── config.txt      # VideoCore GPU config
│   │       └── BOOT_FILES.md   # Instructions
│   │
│   ├── rpi-cm4-io/
│   │   ├── board.mk
│   │   └── boot/
│   │       ├── config.txt
│   │       └── BOOT_FILES.md
│   │
│   └── radxa-rock2a/
│       ├── board.mk
│       └── boot/
│           ├── extlinux/
│           │   └── extlinux.conf   # U-Boot config
│           └── BOOT_FILES.md
│
├── boot/                       # Core assembly entry points
├── common/                     # Shared (less than) minimal libc and mmio
├── drivers/                    # Portable drivers
├── kernel/                     # Kernel code
├── memory/                     # Memory management
├── ui/                         # UI widgets
│
├── Makefile                    # Build system
└── README.md                   # This file
```

## Building

```bash
# Build for Raspberry Pi Zero 2W with GPi Case
make BOARD=rpi-zero2w-gpi

# Build for Raspberry Pi CM4
make BOARD=rpi-cm4-io

# Build for Radxa Rock 2A
make BOARD=radxa-rock2a

# Show build info
make info

# Clean
make clean
```

## Boot Files (⚠️ Platform-Specific!)

Each board has different boot requirements. See `board/<name>/boot/BOOT_FILES.md` for details.

### Raspberry Pi (Zero 2W, CM4)

Boot partition needs:
```
/boot/
├── bootcode.bin      # (Pi Zero 2W only, not CM4)
├── start.elf         # (or start4.elf for CM4)
├── fixup.dat         # (or fixup4.dat for CM4)
├── config.txt        # PROVIDED in board/xxx/boot/
└── kernel8.img       # YOUR OS (build output)
```

Get firmware from: https://github.com/raspberrypi/firmware/tree/master/boot

### Radxa Rock 2A (Rockchip)

**Completely different!** Uses U-Boot, not VideoCore:
```
SD Card:
├── [sector 64]     idbloader.img    # From Radxa/U-Boot
├── [sector 16384]  u-boot.itb       # From Radxa/U-Boot
└── /boot/
    ├── extlinux/
    │   └── extlinux.conf            # PROVIDED
    └── Image                         # YOUR OS (build output)
```

Note: Kernel is called `Image`, not `kernel8.img`!

## HAL API Overview

### Initialization

```c
#include "hal/hal.h"

void kernel_main(...) {
    // Initialize platform (timer, GPIO, board detection)
    hal_platform_init();
    
    // Initialize display
    framebuffer_t *fb;
    hal_display_init(&fb);
    
    // Your code here...
}
```

### Timer Functions

```c
hal_delay_us(1000);              // Delay 1ms
hal_delay_ms(100);               // Delay 100ms
uint64_t ticks = hal_timer_get_ticks();  // Microseconds since boot
```

### GPIO Functions

```c
hal_gpio_set_function(18, HAL_GPIO_OUTPUT);
hal_gpio_set_high(18);
hal_gpio_set_low(18);
bool level = hal_gpio_read(26);

// Peripheral configuration
hal_gpio_configure_dpi();       // DPI display pins
hal_gpio_configure_audio();     // PWM audio pins
hal_gpio_configure_sdcard();    // SD card pins
```

### Platform Information

```c
const char *board = hal_platform_get_board_name();  // "Raspberry Pi Zero 2W"
const char *soc = hal_platform_get_soc_name();      // "BCM2710"
int32_t temp = hal_platform_get_temp_celsius();     // CPU temperature
uint32_t freq = hal_platform_get_arm_freq();        // CPU frequency
bool throttled = hal_platform_is_throttled();       // Thermal throttling?
```

### Display Functions

```c
framebuffer_t *fb;
hal_display_init(&fb);                    // Default resolution
hal_display_init_with_size(800, 600, &fb); // Custom resolution

// After drawing...
hal_display_present(fb);                  // Swap buffers (with vsync)
hal_display_present_immediate(fb);        // Swap buffers (no vsync)

hal_display_set_vsync(fb, false);         // Disable vsync
```

## Key Design Principles

### 1. Drawing Code Stays Portable

Your `fb_*()` drawing functions don't change between platforms:

```c
// This works on ALL platforms!
fb_clear(fb, 0xFF000000);
fb_draw_rect(fb, 10, 10, 100, 50, 0xFFFFFFFF);
fb_draw_string(fb, 20, 20, "Hello World!", 0xFFFFFFFF, 0xFF000000);
```

### 2. HAL Abstracts Hardware Differences

| Feature | BCM2710/BCM2711 | RK3528A |
|---------|-----------------|---------|
| Timer | MMIO (System Timer) | System registers (Generic Timer) |
| GPIO Pull | GPPUD sequence / Direct | GRF registers |
| Display | VideoCore mailbox | VOP2 direct programming |
| Platform Info | Mailbox queries | Fixed values / device tree |

### 3. Compile-Time Platform Selection

No runtime `if (platform == X)` checks. The build system selects the correct implementation:

```makefile
# board/rpi-zero2w-gpi/board.mk
SOC := bcm2710
include soc/$(SOC)/soc.mk
```

### 4. Error Handling

HAL functions return `hal_error_t`:

```c
hal_error_t err = hal_display_init(&fb);
if (HAL_FAILED(err)) {
    // Handle error - err contains specific error code
    if (err == HAL_ERROR_DISPLAY_MAILBOX) { ... }
}
```

## Migration from Existing Code

### Before (Direct Hardware Access)

```c
#include "gpio.h"
#include "mailbox.h"
#include "framebuffer.h"

gpio_configure_for_dpi();
framebuffer_t fb;
fb_init(&fb);
fb_present(&fb);

uint32_t temp;
mailbox_get_temperature(&temp);
```

### After (HAL)

```c
#include "hal/hal.h"
#include "framebuffer.h"  // Still need for drawing functions

hal_platform_init();
hal_gpio_configure_dpi();
framebuffer_t *fb;
hal_display_init(&fb);
hal_display_present(fb);

int32_t temp = hal_platform_get_temp_celsius();
```

## Adding a New Platform

1. **Create SoC directory**: `soc/newsoc/`
2. **Implement HAL interfaces**:
   - `timer.c` - Timer/delay functions
   - `gpio.c` - GPIO control
   - `soc_init.c` - Platform initialization
   - `display_*.c` - Display driver
3. **Create register header**: `newsoc_regs.h`
4. **Create build rules**: `soc.mk`
5. **Create board config**: `board/newboard/board.mk`

## Platform-Specific Notes

### BCM2710 (Pi Zero 2W, Pi 3)
- Peripheral base: `0x3F000000`
- GPIO pull requires GPPUD + GPPUDCLK sequence
- 54 GPIO pins
- VideoCore mailbox for display/temp/clocks

### BCM2711 (Pi 4, CM4)
- Peripheral base: `0xFE000000`
- GPIO pull via direct 2-bit registers (simpler!)
- 58 GPIO pins
- Same mailbox interface

### RK3528A (Rock 2A)
- Peripheral base: `0x02000000`
- ARM Generic Timer via system registers
- 5 GPIO banks × 32 pins = 160 pins
- IOMUX in GRF for pin functions
- VOP2 display controller
- TSADC for temperature

## License

Educational use. See LICENSE file.




https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b

