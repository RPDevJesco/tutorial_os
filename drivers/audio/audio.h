/*
 * audio.h - PWM Audio Driver for RetroFlag GPi Case 2W
 * =====================================================
 *
 * This driver outputs audio via PWM (Pulse Width Modulation) on GPIO 18
 * (left channel) and GPIO 19 (right channel). The GPi Case 2W has built-in
 * low-pass filtering and amplification that converts PWM to analog audio.
 *
 * WHY PWM FOR AUDIO?
 * ------------------
 * The Raspberry Pi doesn't have a dedicated audio DAC (Digital-to-Analog
 * Converter). Instead, we use PWM: a digital technique where the average
 * voltage is controlled by the ratio of high vs low time.
 *
 * For 8-bit audio:
 *   - PWM range = 256 (0-255)
 *   - Sample value 0 = 0% duty cycle = minimum voltage
 *   - Sample value 128 = 50% duty cycle = center voltage (silence)
 *   - Sample value 255 = ~100% duty cycle = maximum voltage
 *
 * The hardware low-pass filter smooths the PWM pulses into a continuous
 * analog waveform.
 *
 * AUDIO ARCHITECTURE:
 * -------------------
 *
 *   [Emulator APU] --> [Resampler] --> [AudioBuffer] --> [PWM FIFO] --> [Speaker]
 *        65kHz            v              2048              16
 *                      44.1kHz         samples          entries
 *
 * The AudioBuffer is crucial for smooth audio:
 *   - Emulator produces samples at irregular intervals
 *   - PWM hardware consumes samples at fixed rate
 *   - Buffer decouples these, preventing stuttering
 *
 * HARDWARE DETAILS:
 * -----------------
 * The BCM283x has two PWM channels that can share a FIFO:
 *   - PWM0 Channel 1 -> GPIO 18 (ALT5) -> Left audio
 *   - PWM0 Channel 2 -> GPIO 19 (ALT5) -> Right audio
 *
 * We configure them to:
 *   - Use Mark-Space mode (for cleaner output)
 *   - Share the FIFO (alternating L/R samples)
 *   - Repeat last value if FIFO underruns
 *
 * CLOCK CONFIGURATION:
 * --------------------
 * Audio quality depends on correct clock setup:
 *   - Source: PLLD (500 MHz on Pi Zero 2W)
 *   - Divider: Calculated for target sample rate
 *   - Formula: Sample rate = PLLD / (divider x PWM_RANGE)
 *
 * For 44.1kHz with 256-level PWM:
 *   divider = 500,000,000 / (44,100 x 256) ~ 44.29
 *
 * GPi CASE 2W SPECIFICS:
 * ----------------------
 *   - Internal speaker is MONO (right channel only)
 *   - 3.5mm headphone jack is STEREO
 *   - Built-in amplifier and low-pass filter
 *   - GPIO 18/19 must NOT be used for DPI display
 */

#ifndef AUDIO_H
#define AUDIO_H

#include "types.h"  /* Bare-metal type definitions */

/* =============================================================================
 * PWM REGISTER ADDRESSES
 * =============================================================================
 */

#define PERIPHERAL_BASE     0x3F000000

/* PWM registers */
#define PWM_BASE            (PERIPHERAL_BASE + 0x0020C000)
#define PWM_CTL             (PWM_BASE + 0x00)   /* Control register */
#define PWM_STA             (PWM_BASE + 0x04)   /* Status register */
#define PWM_RNG1            (PWM_BASE + 0x10)   /* Channel 1 range */
#define PWM_DAT1            (PWM_BASE + 0x14)   /* Channel 1 data */
#define PWM_FIF1            (PWM_BASE + 0x18)   /* FIFO input (shared) */
#define PWM_RNG2            (PWM_BASE + 0x20)   /* Channel 2 range */
#define PWM_DAT2            (PWM_BASE + 0x24)   /* Channel 2 data */

/* Clock manager registers */
#define CM_BASE             (PERIPHERAL_BASE + 0x00101000)
#define CM_PWMCTL           (CM_BASE + 0xA0)    /* PWM clock control */
#define CM_PWMDIV           (CM_BASE + 0xA4)    /* PWM clock divisor */


/* =============================================================================
 * PWM CONTROL REGISTER BITS
 * =============================================================================
 */

/* Channel 1 bits */
#define PWM_CTL_PWEN1       (1 << 0)    /* Enable channel 1 */
#define PWM_CTL_MODE1       (1 << 1)    /* 0=PWM, 1=Serializer mode */
#define PWM_CTL_RPTL1       (1 << 2)    /* Repeat last data when FIFO empty */
#define PWM_CTL_SBIT1       (1 << 3)    /* Silence bit (output when no data) */
#define PWM_CTL_POLA1       (1 << 4)    /* Polarity (0=normal, 1=inverted) */
#define PWM_CTL_USEF1       (1 << 5)    /* Use FIFO (vs DAT1 register) */
#define PWM_CTL_CLRF        (1 << 6)    /* Clear FIFO (write 1 to clear) */
#define PWM_CTL_MSEN1       (1 << 7)    /* Mark-Space enable (cleaner audio) */

/* Channel 2 bits */
#define PWM_CTL_PWEN2       (1 << 8)    /* Enable channel 2 */
#define PWM_CTL_MODE2       (1 << 9)
#define PWM_CTL_RPTL2       (1 << 10)
#define PWM_CTL_SBIT2       (1 << 11)
#define PWM_CTL_POLA2       (1 << 12)
#define PWM_CTL_USEF2       (1 << 13)
#define PWM_CTL_MSEN2       (1 << 15)


/* =============================================================================
 * PWM STATUS REGISTER BITS
 * =============================================================================
 */

#define PWM_STA_FULL        (1 << 0)    /* FIFO is full */
#define PWM_STA_EMPT        (1 << 1)    /* FIFO is empty */
#define PWM_STA_WERR        (1 << 2)    /* FIFO write error (write when full) */
#define PWM_STA_RERR        (1 << 3)    /* FIFO read error (read when empty) */
#define PWM_STA_GAP1        (1 << 4)    /* Channel 1 had a gap */
#define PWM_STA_GAP2        (1 << 5)    /* Channel 2 had a gap */
#define PWM_STA_BERR        (1 << 8)    /* Bus error */
#define PWM_STA_STA1        (1 << 9)    /* Channel 1 is transmitting */
#define PWM_STA_STA2        (1 << 10)   /* Channel 2 is transmitting */

/* Mask for all clearable error bits */
#define PWM_STA_ERRORS      (PWM_STA_WERR | PWM_STA_RERR | PWM_STA_GAP1 | \
                             PWM_STA_GAP2 | PWM_STA_BERR)


/* =============================================================================
 * CLOCK MANAGER BITS
 * =============================================================================
 */

#define CM_PASSWD           0x5A000000  /* Password (must be in bits 31:24) */
#define CM_BUSY             (1 << 7)    /* Clock generator is running */
#define CM_KILL             (1 << 5)    /* Kill the clock generator */
#define CM_ENAB             (1 << 4)    /* Enable the clock generator */

/* Clock sources */
#define CM_SRC_GND          0           /* Ground (0 Hz) */
#define CM_SRC_OSC          1           /* Oscillator (19.2 MHz) */
#define CM_SRC_PLLA         4           /* PLLA */
#define CM_SRC_PLLC         5           /* PLLC */
#define CM_SRC_PLLD         6           /* PLLD (500 MHz) */
#define CM_SRC_HDMI         7           /* HDMI auxiliary */


/* =============================================================================
 * AUDIO CONFIGURATION
 * =============================================================================
 */

/* Target sample rate (standard CD quality) */
#define AUDIO_SAMPLE_RATE       44100

/* GameBoy APU native sample rate (~4194304 / 64) */
#define GB_SAMPLE_RATE          65536

/* PWM resolution (8-bit audio = 256 levels) */
#define PWM_RANGE               256

/* PLLD clock frequency on BCM2837 */
#define PLLD_FREQ               500000000

/* Samples per frame at 60fps (44100 / 60 ~ 735) */
#define SAMPLES_PER_FRAME       735

/* Audio buffer size (enough for ~3 frames) */
#define AUDIO_BUFFER_SIZE       2048


/* =============================================================================
 * DATA STRUCTURES
 * =============================================================================
 */

/*
 * Audio Buffer (Ring Buffer)
 *
 * Decouples emulator timing from PWM hardware timing.
 * Stores stereo samples as interleaved pairs (L, R, L, R, ...).
 */
typedef struct {
    uint8_t buffer[AUDIO_BUFFER_SIZE * 2];  /* Stereo pairs */
    size_t write_pos;                        /* Next write position */
    size_t read_pos;                         /* Next read position */
    size_t count;                            /* Samples currently in buffer */
} audio_buffer_t;

/*
 * Audio Resampler
 *
 * Converts from GameBoy's ~65kHz to our 44.1kHz output.
 * Uses linear interpolation for smoother sound.
 */
typedef struct {
    int16_t prev_left;          /* Previous left sample (for interpolation) */
    int16_t prev_right;         /* Previous right sample */
    uint32_t accumulator;       /* Fractional sample position (16.16 fixed) */
    uint32_t step;              /* Step size per output sample (16.16 fixed) */
} audio_resampler_t;

/*
 * PWM Audio Driver State
 */
typedef struct {
    bool initialized;           /* Driver has been initialized */
    uint8_t volume;             /* Current volume (0-255) */
} audio_driver_t;

/*
 * PWM Status (for debugging)
 */
typedef struct {
    bool fifo_full;
    bool fifo_empty;
    bool write_error;
    bool read_error;
    bool gap_ch1;
    bool gap_ch2;
    bool bus_error;
    bool ch1_active;
    bool ch2_active;
    uint32_t raw;
} pwm_status_t;


/* =============================================================================
 * AUDIO BUFFER API
 * =============================================================================
 */

/*
 * audio_buffer_init() - Initialize audio buffer
 *
 * Clears the buffer and fills with silence (128 for unsigned 8-bit).
 */
void audio_buffer_init(audio_buffer_t *buf);

/*
 * audio_buffer_push() - Add a stereo sample to the buffer
 *
 * @param buf    Audio buffer
 * @param left   Left channel sample (0-255)
 * @param right  Right channel sample (0-255)
 *
 * Returns: true if sample was added, false if buffer full
 */
bool audio_buffer_push(audio_buffer_t *buf, uint8_t left, uint8_t right);

/*
 * audio_buffer_pop() - Remove and return next sample
 *
 * @param buf    Audio buffer
 * @param left   Output: left channel sample
 * @param right  Output: right channel sample
 *
 * Returns: true if sample was available, false if buffer empty
 */
bool audio_buffer_pop(audio_buffer_t *buf, uint8_t *left, uint8_t *right);

/*
 * audio_buffer_count() - Get number of samples in buffer
 */
size_t audio_buffer_count(const audio_buffer_t *buf);

/*
 * audio_buffer_available() - Get available space in buffer
 */
size_t audio_buffer_available(const audio_buffer_t *buf);

/*
 * audio_buffer_clear() - Clear the buffer
 */
void audio_buffer_clear(audio_buffer_t *buf);


/* =============================================================================
 * RESAMPLER API
 * =============================================================================
 */

/*
 * audio_resampler_init() - Initialize resampler
 *
 * @param resampler     Resampler state
 * @param input_rate    Input sample rate (e.g., GB_SAMPLE_RATE)
 * @param output_rate   Output sample rate (e.g., AUDIO_SAMPLE_RATE)
 */
void audio_resampler_init(audio_resampler_t *resampler,
                          uint32_t input_rate, uint32_t output_rate);

/*
 * audio_resampler_push() - Push input sample, get resampled outputs
 *
 * @param resampler  Resampler state
 * @param left       Input left sample (signed 16-bit)
 * @param right      Input right sample (signed 16-bit)
 * @param buffer     Output buffer for resampled samples
 *
 * Returns: Number of output samples produced (0-2 typically)
 */
size_t audio_resampler_push(audio_resampler_t *resampler,
                            int16_t left, int16_t right,
                            audio_buffer_t *buffer);


/* =============================================================================
 * PWM AUDIO DRIVER API
 * =============================================================================
 */

/*
 * audio_init() - Initialize the PWM audio hardware
 *
 * Configures GPIO, clock, and PWM for audio output.
 *
 * Returns: true on success
 */
bool audio_init(audio_driver_t *driver);

/*
 * audio_drain_buffer() - Transfer samples from buffer to PWM FIFO
 *
 * Call this regularly (e.g., once per frame) to keep audio flowing.
 *
 * @param driver     Audio driver
 * @param buffer     Audio buffer to drain from
 * @param max_count  Maximum samples to transfer (or SIZE_MAX for all)
 *
 * Returns: Number of samples transferred
 */
size_t audio_drain_buffer(audio_driver_t *driver, audio_buffer_t *buffer,
                          size_t max_count);

/*
 * audio_write_sample() - Write single sample directly to FIFO
 *
 * For low-latency applications. Prefer audio_drain_buffer() normally.
 *
 * Returns: true if written, false if FIFO full
 */
bool audio_write_sample(audio_driver_t *driver, uint8_t left, uint8_t right);

/*
 * audio_set_volume() - Set output volume
 *
 * @param driver  Audio driver
 * @param volume  Volume level (0-255, where 255 is full volume)
 */
void audio_set_volume(audio_driver_t *driver, uint8_t volume);

/*
 * audio_get_volume() - Get current volume
 */
uint8_t audio_get_volume(const audio_driver_t *driver);

/*
 * audio_stop() - Stop audio output
 */
void audio_stop(audio_driver_t *driver);

/*
 * audio_get_status() - Get PWM status for debugging
 */
pwm_status_t audio_get_status(void);

/*
 * audio_clear_errors() - Clear PWM error flags
 */
void audio_clear_errors(void);

#endif /* AUDIO_H */
