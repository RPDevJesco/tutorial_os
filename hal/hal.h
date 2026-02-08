/*
 * hal/hal.h - Hardware Abstraction Layer Master Header
 *
 * Tutorial-OS: HAL Interface Definitions
 *
 * This is the single header to include for HAL access.
 * It pulls in all HAL subsystem headers.
 *
 * USAGE IN kernel/main.c:
 *
 *   #include "hal/hal.h"
 *
 *   void kernel_main(uintptr_t dtb, uintptr_t ram_base, uintptr_t ram_size) {
 *       // Initialize platform
 *       hal_platform_init();
 *
 *       // Initialize display
 *       framebuffer_t *fb;
 *       hal_display_init(&fb);
 *
 *       // Use existing drawing functions - they still work!
 *       fb_clear(fb, 0xFF000000);
 *       fb_draw_string(fb, 10, 10, "Hello from HAL!", 0xFFFFFFFF, 0xFF000000);
 *
 *       // Present to screen
 *       hal_display_present(fb);
 *   }
 *
 */

#ifndef HAL_H
#define HAL_H

/* =============================================================================
 * HAL VERSION
 * =============================================================================
 */

#define HAL_VERSION_MAJOR   1
#define HAL_VERSION_MINOR   0
#define HAL_VERSION_PATCH   0
#define HAL_VERSION_STRING  "1.0.0"

/* =============================================================================
 * HAL SUBSYSTEM HEADERS
 * =============================================================================
 */

/* Fundamental types and utilities */
#include "hal_types.h"

/* Platform initialization and information */
#include "hal_platform.h"

/* Timer and delay functions */
#include "hal_timer.h"

/* GPIO pin control */
#include "hal_gpio.h"

/* Display/framebuffer abstraction */
#include "hal_display.h"

/* =============================================================================
 * CONVENIENCE MACROS
 * =============================================================================
 */

/*
 * Standard initialization sequence
 *
 * Call this at the start of kernel_main() to initialize all HAL subsystems.
 * Returns HAL_SUCCESS or first error encountered.
 */
#define HAL_INIT_ALL() do { \
    hal_error_t _err; \
    _err = hal_platform_init(); \
    if (HAL_FAILED(_err)) return _err; \
} while(0)

/*
 * Check and panic on error
 */
#define HAL_CHECK(expr) do { \
    hal_error_t _err = (expr); \
    if (HAL_FAILED(_err)) { \
        hal_panic(#expr " failed"); \
    } \
} while(0)

/* =============================================================================
 * BUILD CONFIGURATION
 * =============================================================================
 * These are set by the build system based on BOARD selection.
 */

/* Platform identifier - set by build system */
#ifndef HAL_PLATFORM
#define HAL_PLATFORM    HAL_PLATFORM_UNKNOWN
#endif

/* SoC identifier - set by build system */
#ifndef HAL_SOC
#define HAL_SOC         "unknown"
#endif

/* Board name - set by build system */
#ifndef HAL_BOARD
#define HAL_BOARD       "unknown"
#endif

/* =============================================================================
 * PHASE 1 STATUS
 * =============================================================================
 *
 * This is Phase 1 of the HAL implementation. The headers define the
 * interfaces but implementations are not yet complete for all platforms.
 *
 * Phase 1 Complete:
 *   [x] hal_types.h      - Types and utilities
 *   [x] hal_platform.h   - Platform info interface
 *   [x] hal_timer.h      - Timer/delay interface
 *   [x] hal_gpio.h       - GPIO interface
 *   [x] hal_display.h    - Display interface
 *
 * Phase 2 (Implementation):
 *   [ ] soc/bcm2710/     - Pi Zero 2W implementation
 *   [ ] soc/bcm2711/     - Pi 4/CM4 implementation
 *   [ ] soc/bcm2712/     - Pi 5/CM5 implementation
 *   [ ] soc/rk3528a/     - Rock 2A implementation
 *   [ ] soc/s905x/       - Le Potato implementation
 *   [ ] soc/h618/        - KICKPI K2B implementation
 *   [ ] soc/k1/          - Orange Pi RV2 implementation
 *
 */

#endif /* HAL_H */
