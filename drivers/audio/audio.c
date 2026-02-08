/*
 * audio.c - PWM Audio Driver Implementation
 * =========================================
 *
 * This file implements PWM-based audio output for the GPi Case 2W.
 * See audio.h for detailed documentation.
 *
 * IMPLEMENTATION NOTES:
 * ---------------------
 *
 * CLOCK SETUP:
 * The most critical part of audio is getting the clock right. We use PLLD
 * (500 MHz) as the source and calculate a fractional divider to achieve
 * the exact sample rate we want.
 *
 * The divider has integer and fractional parts:
 *   - Integer: bits [23:12] of CM_PWMDIV
 *   - Fractional: bits [11:0] of CM_PWMDIV (4096 = 1.0)
 *
 * Formula: PWM_freq = PLLD / (integer + fractional/4096)
 * And: Sample_rate = PWM_freq / PWM_RANGE
 *
 * For 44.1kHz with range 256:
 *   Target PWM_freq = 44100 x 256 = 11,289,600 Hz
 *   Divider = 500,000,000 / 11,289,600 = 44.2861
 *   Integer = 44, Fractional = 0.2861 x 4096 ~ 1172
 *
 * FIFO OPERATION:
 * The PWM hardware has a 16-entry FIFO shared between both channels.
 * When using stereo, samples alternate: L, R, L, R, ...
 * If the FIFO underruns, the hardware repeats the last sample (RPTL bit).
 *
 * MARK-SPACE MODE:
 * We use Mark-Space (M/S) mode rather than the default balanced mode.
 * In M/S mode, the output is high for (data/range) of the period,
 * giving cleaner audio that's easier to filter.
 *
 * VOLUME CONTROL:
 * Since we're doing 8-bit PWM, we implement volume by scaling the
 * sample values. Full volume (255) = original value.
 * Lower volumes reduce the PWM range, which reduces amplitude.
 */

#include "audio.h"
#include "gpio.h"

/* =============================================================================
 * MMIO ACCESS HELPERS
 * =============================================================================
 */

static inline uint32_t mmio_read(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

static inline void dmb(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}


/* =============================================================================
 * AUDIO BUFFER IMPLEMENTATION
 * =============================================================================
 */

/*
 * audio_buffer_init() - Initialize buffer with silence
 */
void audio_buffer_init(audio_buffer_t *buf)
{
    /*
     * Fill with 128 (silence for unsigned 8-bit)
     *
     * In unsigned 8-bit audio:
     *   0 = minimum voltage (negative peak)
     *   128 = center (silence)
     *   255 = maximum voltage (positive peak)
     */
    for (size_t i = 0; i < AUDIO_BUFFER_SIZE * 2; i++) {
        buf->buffer[i] = 128;
    }
    buf->write_pos = 0;
    buf->read_pos = 0;
    buf->count = 0;
}

/*
 * audio_buffer_push() - Add stereo sample to buffer
 */
bool audio_buffer_push(audio_buffer_t *buf, uint8_t left, uint8_t right)
{
    if (buf->count >= AUDIO_BUFFER_SIZE) {
        return false;  /* Buffer full */
    }

    /* Store as interleaved stereo (L, R) */
    size_t idx = buf->write_pos * 2;
    buf->buffer[idx] = left;
    buf->buffer[idx + 1] = right;

    /* Advance write position with wraparound */
    buf->write_pos = (buf->write_pos + 1) % AUDIO_BUFFER_SIZE;
    buf->count++;

    return true;
}

/*
 * audio_buffer_pop() - Remove and return next sample
 */
bool audio_buffer_pop(audio_buffer_t *buf, uint8_t *left, uint8_t *right)
{
    if (buf->count == 0) {
        return false;  /* Buffer empty */
    }

    size_t idx = buf->read_pos * 2;
    *left = buf->buffer[idx];
    *right = buf->buffer[idx + 1];

    buf->read_pos = (buf->read_pos + 1) % AUDIO_BUFFER_SIZE;
    buf->count--;

    return true;
}

/*
 * audio_buffer_count() - Get sample count
 */
size_t audio_buffer_count(const audio_buffer_t *buf)
{
    return buf->count;
}

/*
 * audio_buffer_available() - Get free space
 */
size_t audio_buffer_available(const audio_buffer_t *buf)
{
    return AUDIO_BUFFER_SIZE - buf->count;
}

/*
 * audio_buffer_clear() - Clear the buffer
 */
void audio_buffer_clear(audio_buffer_t *buf)
{
    buf->write_pos = 0;
    buf->read_pos = 0;
    buf->count = 0;
}


/* =============================================================================
 * RESAMPLER IMPLEMENTATION
 * =============================================================================
 */

/*
 * audio_resampler_init() - Initialize resampler
 *
 * We use fixed-point math (16.16 format) for the resampling ratio.
 * This avoids floating-point operations while maintaining precision.
 */
void audio_resampler_init(audio_resampler_t *resampler,
                          uint32_t input_rate, uint32_t output_rate)
{
    resampler->prev_left = 0;
    resampler->prev_right = 0;
    resampler->accumulator = 0;

    /*
     * Calculate step size in 16.16 fixed point
     *
     * step = (input_rate / output_rate) x 65536
     *
     * For 65536 -> 44100:
     *   step = 65536 / 44100 x 65536 ~ 97391
     *
     * This means for each output sample, we advance ~1.486 input samples.
     */
    resampler->step = ((uint64_t)input_rate << 16) / output_rate;
}

/*
 * audio_resampler_push() - Push input sample, get outputs
 *
 * Uses linear interpolation between samples for smoother output.
 * Returns the number of output samples produced (usually 0-2).
 */
size_t audio_resampler_push(audio_resampler_t *resampler,
                            int16_t left, int16_t right,
                            audio_buffer_t *buffer)
{
    size_t output_count = 0;

    /*
     * Produce output samples while our position is < 1.0 (65536)
     *
     * The accumulator tracks our fractional position between the
     * previous sample (0) and current sample (65536).
     */
    while (resampler->accumulator < 0x10000) {
        /*
         * Linear interpolation
         *
         * output = prev + (current - prev) x fraction
         *
         * fraction = accumulator / 65536
         */
        uint32_t frac = resampler->accumulator & 0xFFFF;

        int32_t out_left = resampler->prev_left +
            (((int32_t)(left - resampler->prev_left) * (int32_t)frac) >> 16);
        int32_t out_right = resampler->prev_right +
            (((int32_t)(right - resampler->prev_right) * (int32_t)frac) >> 16);

        /*
         * Convert from signed 16-bit to unsigned 8-bit
         *
         * Input range: -32768 to 32767
         * Output range: 0 to 255
         *
         * Formula: output = (input + 32768) / 256
         */
        uint8_t left_u8 = (uint8_t)((out_left + 32768) >> 8);
        uint8_t right_u8 = (uint8_t)((out_right + 32768) >> 8);

        if (audio_buffer_push(buffer, left_u8, right_u8)) {
            output_count++;
        }

        /* Advance by one output sample interval */
        resampler->accumulator += resampler->step;
    }

    /*
     * Save current sample as previous for next interpolation
     * Subtract 65536 from accumulator since we've passed the current sample
     */
    resampler->prev_left = left;
    resampler->prev_right = right;
    resampler->accumulator -= 0x10000;

    return output_count;
}


/* =============================================================================
 * PWM HARDWARE CONFIGURATION
 * =============================================================================
 */

/*
 * configure_gpio_for_pwm() - Set up GPIO 18/19 for PWM
 */
static void configure_gpio_for_pwm(void)
{
    /*
     * GPIO 18 and 19 use ALT5 for PWM function
     *
     * This is called from the driver, but could also be done
     * through gpio_configure_for_audio().
     */
    gpio_configure_for_audio();
    dmb();
}

/*
 * configure_pwm_clock() - Set up the PWM clock
 *
 * This is the most critical part. Wrong clock = wrong sample rate.
 */
static void configure_pwm_clock(void)
{
    /*
     * Step 1: Stop the clock
     *
     * We must stop the clock before changing its settings.
     * The KILL bit forces it to stop immediately.
     */
    mmio_write(CM_PWMCTL, CM_PASSWD | CM_KILL);
    dmb();

    /*
     * Wait for clock to stop
     *
     * The BUSY bit indicates the clock is still running.
     */
    for (int i = 0; i < 10000; i++) {
        if ((mmio_read(CM_PWMCTL) & CM_BUSY) == 0) {
            break;
        }
        /* spin_loop_hint() - tell CPU we're waiting */
        __asm__ volatile("yield");
    }

    /*
     * Step 2: Calculate and set divisor
     *
     * We want: Sample rate = PLLD / (divisor x PWM_RANGE)
     *
     * For 44100 Hz with 256 range:
     *   target_pwm_freq = 44100 x 256 = 11,289,600 Hz
     *   divisor = 500,000,000 / 11,289,600 ~ 44.29
     *
     * The divisor register format:
     *   Bits [23:12] = Integer part (DIVI)
     *   Bits [11:0] = Fractional part (DIVF), where 4096 = 1.0
     */
    uint32_t target_pwm_freq = AUDIO_SAMPLE_RATE * PWM_RANGE;
    uint32_t divi = PLLD_FREQ / target_pwm_freq;
    uint32_t divf = (((uint64_t)(PLLD_FREQ % target_pwm_freq) * 4096) / target_pwm_freq);

    mmio_write(CM_PWMDIV, CM_PASSWD | (divi << 12) | divf);
    dmb();

    /* Small delay for divisor to stabilize */
    for (int i = 0; i < 100; i++) {
        __asm__ volatile("yield");
    }

    /*
     * Step 3: Enable clock with PLLD source
     */
    mmio_write(CM_PWMCTL, CM_PASSWD | CM_ENAB | CM_SRC_PLLD);
    dmb();

    /*
     * Wait for clock to start
     */
    for (int i = 0; i < 10000; i++) {
        if ((mmio_read(CM_PWMCTL) & CM_BUSY) != 0) {
            break;
        }
        __asm__ volatile("yield");
    }
}

/*
 * configure_pwm_hardware() - Set up the PWM peripheral
 */
static void configure_pwm_hardware(void)
{
    /*
     * Step 1: Disable PWM while configuring
     */
    mmio_write(PWM_CTL, 0);
    dmb();

    /* Small delay */
    for (int i = 0; i < 100; i++) {
        __asm__ volatile("yield");
    }

    /*
     * Step 2: Clear any pending errors
     *
     * Writing 1 to the error bits clears them.
     */
    mmio_write(PWM_STA, PWM_STA_ERRORS);
    dmb();

    /*
     * Step 3: Set range for both channels
     *
     * Range = 256 for 8-bit audio resolution.
     * The PWM output duty cycle = data / range.
     */
    mmio_write(PWM_RNG1, PWM_RANGE);
    dmb();
    mmio_write(PWM_RNG2, PWM_RANGE);
    dmb();

    /*
     * Step 4: Clear the FIFO
     */
    mmio_write(PWM_CTL, PWM_CTL_CLRF);
    dmb();

    /* Small delay */
    for (int i = 0; i < 100; i++) {
        __asm__ volatile("yield");
    }

    /*
     * Step 5: Configure and enable both channels
     *
     * Configuration:
     *   PWEN1/2: Enable channel
     *   USEF1/2: Use FIFO (vs DAT register)
     *   RPTL1/2: Repeat last value on underrun (prevents clicking)
     *   MSEN1/2: Mark-Space mode (cleaner audio)
     *
     * Note: We don't set MODE bits (serializer mode) - we want PWM.
     */
    uint32_t ctl = PWM_CTL_PWEN1 | PWM_CTL_USEF1 | PWM_CTL_RPTL1 | PWM_CTL_MSEN1
                 | PWM_CTL_PWEN2 | PWM_CTL_USEF2 | PWM_CTL_RPTL2 | PWM_CTL_MSEN2;
    
    mmio_write(PWM_CTL, ctl);
    dmb();
}


/* =============================================================================
 * PUBLIC API IMPLEMENTATION
 * =============================================================================
 */

/*
 * audio_init() - Initialize PWM audio
 */
bool audio_init(audio_driver_t *driver)
{
    if (driver->initialized) {
        return true;  /* Already initialized */
    }

    /* Step 1: Configure GPIO pins */
    configure_gpio_for_pwm();
    dmb();

    /* Step 2: Configure PWM clock */
    configure_pwm_clock();
    dmb();

    /* Step 3: Configure PWM hardware */
    configure_pwm_hardware();
    dmb();

    driver->initialized = true;
    driver->volume = 255;  /* Full volume */

    return true;
}

/*
 * audio_drain_buffer() - Transfer samples to PWM FIFO
 *
 * This is the main function for getting audio out. Call it regularly
 * to keep the FIFO fed.
 */
size_t audio_drain_buffer(audio_driver_t *driver, audio_buffer_t *buffer,
                          size_t max_count)
{
    if (!driver->initialized) {
        return 0;
    }

    size_t written = 0;

    while (written < max_count && buffer->count > 0) {
        /* Check if FIFO is full */
        if (mmio_read(PWM_STA) & PWM_STA_FULL) {
            break;  /* FIFO full, stop */
        }

        /* Get next sample from buffer */
        uint8_t left, right;
        if (!audio_buffer_pop(buffer, &left, &right)) {
            break;  /* Buffer empty */
        }

        /*
         * Apply volume scaling
         *
         * volume is 0-255, where 255 = full volume.
         * We scale the sample value accordingly.
         */
        uint32_t left_scaled = ((uint32_t)left * driver->volume) >> 8;
        uint32_t right_scaled = ((uint32_t)right * driver->volume) >> 8;

        /*
         * Write to FIFO
         *
         * The FIFO is shared between channels. Data is consumed
         * alternating: left, right, left, right, ...
         */
        mmio_write(PWM_FIF1, left_scaled);
        mmio_write(PWM_FIF1, right_scaled);

        written++;
    }

    return written;
}

/*
 * audio_write_sample() - Write single sample to FIFO
 */
bool audio_write_sample(audio_driver_t *driver, uint8_t left, uint8_t right)
{
    if (!driver->initialized) {
        return false;
    }

    /* Check if FIFO is full */
    if (mmio_read(PWM_STA) & PWM_STA_FULL) {
        return false;
    }

    /* Apply volume */
    uint32_t left_scaled = ((uint32_t)left * driver->volume) >> 8;
    uint32_t right_scaled = ((uint32_t)right * driver->volume) >> 8;

    /* Write to FIFO */
    mmio_write(PWM_FIF1, left_scaled);
    mmio_write(PWM_FIF1, right_scaled);

    return true;
}

/*
 * audio_set_volume() - Set volume
 */
void audio_set_volume(audio_driver_t *driver, uint8_t volume)
{
    driver->volume = volume;
}

/*
 * audio_get_volume() - Get volume
 */
uint8_t audio_get_volume(const audio_driver_t *driver)
{
    return driver->volume;
}

/*
 * audio_stop() - Stop audio output
 */
void audio_stop(audio_driver_t *driver)
{
    if (!driver->initialized) {
        return;
    }

    /* Clear FIFO and stop PWM */
    mmio_write(PWM_CTL, PWM_CTL_CLRF);
    dmb();
}

/*
 * audio_get_status() - Get PWM status for debugging
 */
pwm_status_t audio_get_status(void)
{
    uint32_t sta = mmio_read(PWM_STA);

    pwm_status_t status = {
        .fifo_full = (sta & PWM_STA_FULL) != 0,
        .fifo_empty = (sta & PWM_STA_EMPT) != 0,
        .write_error = (sta & PWM_STA_WERR) != 0,
        .read_error = (sta & PWM_STA_RERR) != 0,
        .gap_ch1 = (sta & PWM_STA_GAP1) != 0,
        .gap_ch2 = (sta & PWM_STA_GAP2) != 0,
        .bus_error = (sta & PWM_STA_BERR) != 0,
        .ch1_active = (sta & PWM_STA_STA1) != 0,
        .ch2_active = (sta & PWM_STA_STA2) != 0,
        .raw = sta
    };

    return status;
}

/*
 * audio_clear_errors() - Clear error flags
 */
void audio_clear_errors(void)
{
    mmio_write(PWM_STA, PWM_STA_ERRORS);
}
