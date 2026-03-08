/*
 * gpio.c — GPIO Driver for the StarFive JH-7110 SoC
 * ===================================================
 *
 * This implements GPIO control for the Milk-V Mars.
 * Unlike the Ky X1 (which uses a Marvell PXA/MMP-style GPIO controller
 * with 4 banks × 32 pins and a GPIOX_PDR/GPIOX_DIR register scheme),
 * the JH7110 GPIO uses a simpler per-pin model with individual dout/doen
 * registers for each GPIO.
 *
 * TWO HARDWARE SUBSYSTEMS:
 * ========================
 *
 * 1. sys_iomux (0x13000000) — Pin Function Selection
 *    Controls which peripheral signal is routed to each physical pad.
 *    Example: "GPIO5 should carry UART0 TX output" is configured here.
 *    The iomux has two sides:
 *      - Output mux: which hardware function drives the pad
 *      - Input mux:  which pad provides the hardware function's input
 *
 * 2. sys_gpio (0x13040000) — GPIO Output/Input Data
 *    When a pin is in GPIO mode (not a peripheral function), these
 *    registers control the output value and direction.
 *    - DOUT register: output data (0 = low, 1 = high)
 *    - DOEN register: output enable (0 = output, 1 = input/high-Z)
 *    - DIN  register: read input data
 *
 * NOTE ON UART0 PINMUX:
 *   The Mars schematic shows UART0 on GPIO5 (TX) and GPIO6 (RX).
 *   U-Boot initializes this pinmux before jumping to our kernel.
 *   We don't need to reprogram UART0 iomux — it's already done.
 *
 * WHAT WE DO IMPLEMENT:
 *   - GPIO output for status indicators (user LED if present)
 *   - Minimal iomux programming for any GPIO we want to control
 *   - Read GPIO input for boot mode / button detection
 *
 * CONTRAST WITH KYX1:
 *   The Ky X1 has 4 GPIO banks (MMP-style), each with a 32-bit PDR
 *   (data), PSR (set), PCR (clear), and DIR (direction) register set.
 *   Ky X1 also has a heartbeat LED on GPIO96 (bank 3, pin 0).
 *
 *   The JH7110 uses per-pin dout/doen registers and the LED GPIO
 *   depends on the board. The Mars CM5 module doesn't expose a
 *   user-controlled LED in the same way. We stub this gracefully.
 */

#include "jh7110_regs.h"
#include "types.h"

extern void jh7110_uart_puts(const char *str);
extern void jh7110_uart_putdec(uint32_t val);

/* =============================================================================
 * LOW-LEVEL GPIO ACCESS
 * =============================================================================
 */

/*
 * jh7110_gpio_set_output — configure a GPIO pin as output
 *
 * Sets DOEN register to 0 (output enabled, active low sense in JH7110).
 */
void jh7110_gpio_set_output(uint32_t gpio_num)
{
    if (gpio_num >= 64) return;
    *((volatile uint32_t *)JH7110_GPIO_DOEN(gpio_num)) = JH7110_GPIO_OUTPUT_EN;
}

/*
 * jh7110_gpio_set_input — configure a GPIO pin as input
 *
 * Sets DOEN register to 1 (output disabled = input mode).
 */
void jh7110_gpio_set_input(uint32_t gpio_num)
{
    if (gpio_num >= 64) return;
    *((volatile uint32_t *)JH7110_GPIO_DOEN(gpio_num)) = JH7110_GPIO_INPUT_EN;
}

/*
 * jh7110_gpio_write — set GPIO output value
 *
 * @param gpio_num  GPIO number (0-63)
 * @param value     0 = low, 1 = high
 */
void jh7110_gpio_write(uint32_t gpio_num, uint32_t value)
{
    if (gpio_num >= 64) return;
    *((volatile uint32_t *)JH7110_GPIO_DOUT(gpio_num)) = value ? 1 : 0;
}

/*
 * jh7110_gpio_read — read GPIO input value
 *
 * @param gpio_num  GPIO number (0-63)
 * @return          0 or 1
 */
uint32_t jh7110_gpio_read(uint32_t gpio_num)
{
    if (gpio_num >= 64) return 0;
    return *((volatile uint32_t *)JH7110_GPIO_DIN(gpio_num)) & 1;
}

/* =============================================================================
 * SYS_IOMUX PIN FUNCTION SELECTION
 * =============================================================================
 *
 * The JH7110 sys_iomux register layout is defined in:
 *   Linux: drivers/pinctrl/starfive/pinctrl-starfive-jh7110.c
 *   U-Boot: arch/riscv/dts/jh7110.dtsi
 *
 * The iomux has two register groups:
 *
 * 1. PADCFG (pad configuration): sets drive strength, pull-up/down, schmitt
 *    One register per pad, starting at sys_iomux base + 0x000.
 *
 * 2. GPI (peripheral input mux): selects which GPIO pad feeds a peripheral's
 *    input signal. One register per input function.
 *    Located at sys_iomux base + 0x478 (from Linux driver).
 *
 * 3. GPIOIN is in sys_gpio (0x13040000), not sys_iomux.
 *
 * For Tutorial-OS bring-up, we note that U-Boot already programs the
 * UART0 iomux (GPIO5=UART0_TX, GPIO6=UART0_RX). We don't need to
 * reprogram it. This function is a stub for future use when we need
 * to configure other peripherals' pins.
 *
 * TEACHING NOTE:
 *   This "inherited pinmux" pattern appears on every U-Boot based board
 *   in this project (Orange Pi RV2, Rock 2A, Libre Le Potato). U-Boot
 *   configures pins it needs, and our kernel inherits that state.
 *   We only need to configure GPIO pins that U-Boot didn't touch.
 */
void jh7110_iomux_set_uart0(void)
{
    /*
     * UART0 iomux (GPIO5=TX, GPIO6=RX) is already configured by U-Boot.
     * This function is intentionally a no-op — it exists to document
     * the intent and serve as a hook for future direct iomux programming.
     *
     * If you need to reprogram it (e.g., after a warm reset that doesn't
     * go through U-Boot), the Linux pinctrl driver is the reference:
     *   JH7110_UART0_TXD: gpiomux = 26 (GPIO5, function uart0_txd)
     *   JH7110_UART0_RXD: gpiomux = 27 (GPIO6, function uart0_rxd)
     */
    (void)0;
}

/* =============================================================================
 * BOOT MODE GPIO READ
 * =============================================================================
 *
 * The Mars has a boot mode DIP switch (SW2) connected to AON GPIO pins
 * RGPIO0, RGPIO1, RGPIO2. Reading these tells us the selected boot device.
 *
 * Boot mode encoding (from JH7110 datasheet section "Others"):
 *   RGPIO2=0: QSPI flash boot (normal operation)
 *   RGPIO2=1: UART debug boot
 *
 * These are in the AON GPIO domain (0x17020000), not the sys_gpio.
 * Documented here for reference. The Mars ships with RGPIO2=0.
 */
uint32_t jh7110_get_boot_mode(void)
{
    /* Read AON GPIO input: RGPIO0 (bit 0) and RGPIO1 (bit 1) */
    volatile uint32_t *aon_din = (volatile uint32_t *)(JH7110_AON_GPIO_BASE + 0x080);
    return (*aon_din) & 0x3;
}

/* =============================================================================
 * HEARTBEAT / STATUS LED
 * =============================================================================
 *
 * The Milk-V Mars does not have a dedicated user LED on the SoM itself.
 * The carrier board (if any) may have one, but no GPIO is standardized.
 *
 * We implement the init as a no-op and document why. This matches the
 * pattern in other soc/ implementations: the function exists in the API
 * (called from soc_init.c) but does nothing on hardware that lacks the LED.
 *
 * HAL TEACHING POINT:
 *   The same soc_init.c calls jh7110_gpio_init_heartbeat_led() regardless
 *   of whether hardware actually exists. The function gracefully does nothing
 *   if the LED isn't present. This is better than #ifdef guards everywhere —
 *   the API is uniform, the hardware capability varies.
 */
void jh7110_gpio_init_heartbeat_led(void)
{
    /*
     * Milk-V Mars has no standardized user LED GPIO.
     * If your specific carrier board has an LED, configure it here.
     *
     * Example for a hypothetical LED on GPIO40:
     *   jh7110_gpio_set_output(40);
     *   jh7110_gpio_write(40, 0);  // active low
     */
    jh7110_uart_puts("[gpio] Note: No standard user LED on Milk-V Mars SoM\n");
}

void jh7110_gpio_set_led(bool on)
{
    /*
     * No-op — see jh7110_gpio_init_heartbeat_led() comment above.
     * If you add a carrier board LED, drive it here.
     */
    (void)on;
}

/* =============================================================================
 * HAL GPIO CONTRACT IMPLEMENTATION
 * =============================================================================
 *
 * The kernel uses hal_gpio_configure_dpi() to set up DPI display pins.
 * The JH7110 doesn't use DPI — it uses HDMI via SimpleFB inherited from
 * U-Boot. So this is a no-op, just like the Ky X1's implementation.
 */
void hal_gpio_configure_dpi(void)
{
    /* JH7110 uses HDMI output via DC8200 + HDMI TX, not DPI.
     * Display is fully configured by U-Boot before we boot. */
}

void hal_gpio_set_function(uint32_t pin, uint32_t function)
{
    /* Future: implement sys_iomux function select for arbitrary pins */
    (void)pin;
    (void)function;
}

void hal_gpio_set_high(uint32_t pin)
{
    jh7110_gpio_write(pin, 1);
}

void hal_gpio_set_low(uint32_t pin)
{
    jh7110_gpio_write(pin, 0);
}

bool hal_gpio_read(uint32_t pin)
{
    return jh7110_gpio_read(pin) != 0;
}