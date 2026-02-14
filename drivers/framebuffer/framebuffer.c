/*
 * Framebuffer Implementation (ARGB8888)
 * ======================================
 *
 * This file contains the drawing functions for a 32-bit ARGB framebuffer.
 * Matches the Rust implementation feature-for-feature.
 */

#include "common/types.h"
#include "framebuffer.h"

/* BCM platforms use VideoCore mailbox for framebuffer allocation and vsync */
#if defined(SOC_BCM2710)
#include "bcm2710_mailbox.h"
#include "bcm2710_regs.h"
#define HAS_BCM_MAILBOX 1
#elif defined(SOC_BCM2711)
#include "bcm2711_mailbox.h"
#include "bcm2711_regs.h"
#define HAS_BCM_MAILBOX 1
#elif defined(SOC_BCM2712)
#include "bcm2712_mailbox.h"
#include "bcm2712_regs.h"
#define HAS_BCM_MAILBOX 1
#else
#define HAS_BCM_MAILBOX 0
#endif

/* =============================================================================
 * CACHE AND MEMORY BARRIER OPERATIONS
 * =============================================================================
 * Provided by mmio.h which detects the target architecture at compile time:
 *   ARM64:   inline dsb/dmb/dc instructions
 *   RISC-V:  fence (inline) + extern cache ops from cache.S (Zicbom)
 */
#include "mmio.h"

/* =============================================================================
 * VOLATILE ACCESS MACROS
 * =============================================================================
 */

#define WRITE_VOLATILE(addr, val) (*(volatile uint32_t *)(addr) = (val))
#define READ_VOLATILE(addr)       (*(volatile uint32_t *)(addr))


/* =============================================================================
 * GAMEBOY PALETTE (ARGB8888) - DMG Classic Green
 * =============================================================================
 */

const uint32_t gb_palette[4] = {
    0xFFE0F8D0,  /* Lightest (white-ish green) */
    0xFF88C070,  /* Light green */
    0xFF346856,  /* Dark green */
    0xFF081820,  /* Darkest (near black) */
};


/* =============================================================================
 * SINE LOOKUP TABLE
 * =============================================================================
 * Indexed by degree (0-90), returns sin * 256
 */

static const int16_t sin_table[91] = {
    0, 4, 9, 13, 18, 22, 27, 31, 36, 40,
    44, 49, 53, 58, 62, 66, 71, 75, 79, 83,
    88, 92, 96, 100, 104, 108, 112, 116, 120, 124,
    128, 131, 135, 139, 143, 146, 150, 153, 157, 160,
    164, 167, 171, 174, 177, 181, 184, 187, 190, 193,
    196, 198, 201, 204, 207, 209, 212, 214, 217, 219,
    221, 223, 226, 228, 230, 232, 233, 235, 237, 238,
    240, 242, 243, 244, 246, 247, 248, 249, 250, 251,
    252, 252, 253, 254, 254, 255, 255, 255, 256, 256,
    256,
};


/* =============================================================================
 * 8x8 BITMAP FONT (ASCII 32-126)
 * =============================================================================
 */

static const uint8_t font_8x8[95][8] = {
    /* Space (32) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* ! (33) */
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00},
    /* " (34) */
    {0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* # (35) */
    {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00},
    /* $ (36) */
    {0x18, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x18, 0x00},
    /* % (37) */
    {0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00},
    /* & (38) */
    {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00},
    /* ' (39) */
    {0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* ( (40) */
    {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00},
    /* ) (41) */
    {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
    /* * (42) */
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    /* + (43) */
    {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
    /* , (44) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30},
    /* - (45) */
    {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    /* . (46) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
    /* / (47) */
    {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00},
    /* 0 (48) */
    {0x7C, 0xCE, 0xDE, 0xF6, 0xE6, 0xC6, 0x7C, 0x00},
    /* 1 (49) */
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    /* 2 (50) */
    {0x7C, 0xC6, 0x06, 0x7C, 0xC0, 0xC0, 0xFE, 0x00},
    /* 3 (51) */
    {0xFC, 0x06, 0x06, 0x3C, 0x06, 0x06, 0xFC, 0x00},
    /* 4 (52) */
    {0x0C, 0xCC, 0xCC, 0xCC, 0xFE, 0x0C, 0x0C, 0x00},
    /* 5 (53) */
    {0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00},
    /* 6 (54) */
    {0x7C, 0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00},
    /* 7 (55) */
    {0xFE, 0x06, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x00},
    /* 8 (56) */
    {0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00},
    /* 9 (57) */
    {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x06, 0x7C, 0x00},
    /* : (58) */
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00},
    /* ; (59) */
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30},
    /* < (60) */
    {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00},
    /* = (61) */
    {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00},
    /* > (62) */
    {0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00},
    /* ? (63) */
    {0x3C, 0x66, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00},
    /* @ (64) */
    {0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x7E, 0x00},
    /* A (65) */
    {0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00},
    /* B (66) */
    {0xFC, 0xC6, 0xC6, 0xFC, 0xC6, 0xC6, 0xFC, 0x00},
    /* C (67) */
    {0x7C, 0xC6, 0xC0, 0xC0, 0xC0, 0xC6, 0x7C, 0x00},
    /* D (68) */
    {0xF8, 0xCC, 0xC6, 0xC6, 0xC6, 0xCC, 0xF8, 0x00},
    /* E (69) */
    {0xFE, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xFE, 0x00},
    /* F (70) */
    {0xFE, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xC0, 0x00},
    /* G (71) */
    {0x7C, 0xC6, 0xC0, 0xCE, 0xC6, 0xC6, 0x7C, 0x00},
    /* H (72) */
    {0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00},
    /* I (73) */
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    /* J (74) */
    {0x06, 0x06, 0x06, 0x06, 0xC6, 0xC6, 0x7C, 0x00},
    /* K (75) */
    {0xC6, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6, 0x00},
    /* L (76) */
    {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00},
    /* M (77) */
    {0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0x00},
    /* N (78) */
    {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00},
    /* O (79) */
    {0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    /* P (80) */
    {0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, 0xC0, 0x00},
    /* Q (81) */
    {0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xDE, 0x7C, 0x06},
    /* R (82) */
    {0xFC, 0xC6, 0xC6, 0xFC, 0xD8, 0xCC, 0xC6, 0x00},
    /* S (83) */
    {0x7C, 0xC6, 0xC0, 0x7C, 0x06, 0xC6, 0x7C, 0x00},
    /* T (84) */
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    /* U (85) */
    {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    /* V (86) */
    {0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00},
    /* W (87) */
    {0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00},
    /* X (88) */
    {0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00},
    /* Y (89) */
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00},
    /* Z (90) */
    {0xFE, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xFE, 0x00},
    /* [ (91) */
    {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00},
    /* \ (92) */
    {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00},
    /* ] (93) */
    {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00},
    /* ^ (94) */
    {0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00},
    /* _ (95) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE},
    /* ` (96) */
    {0x18, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* a (97) */
    {0x00, 0x00, 0x7C, 0x06, 0x7E, 0xC6, 0x7E, 0x00},
    /* b (98) */
    {0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xFC, 0x00},
    /* c (99) */
    {0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00},
    /* d (100) */
    {0x06, 0x06, 0x7E, 0xC6, 0xC6, 0xC6, 0x7E, 0x00},
    /* e (101) */
    {0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00},
    /* f (102) */
    {0x1C, 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x00},
    /* g (103) */
    {0x00, 0x00, 0x7E, 0xC6, 0xC6, 0x7E, 0x06, 0x7C},
    /* h (104) */
    {0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0x00},
    /* i (105) */
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    /* j (106) */
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x70},
    /* k (107) */
    {0xC0, 0xC0, 0xC6, 0xCC, 0xF8, 0xCC, 0xC6, 0x00},
    /* l (108) */
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    /* m (109) */
    {0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xC6, 0xC6, 0x00},
    /* n (110) */
    {0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0x00},
    /* o (111) */
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    /* p (112) */
    {0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0},
    /* q (113) */
    {0x00, 0x00, 0x7E, 0xC6, 0xC6, 0x7E, 0x06, 0x06},
    /* r (114) */
    {0x00, 0x00, 0xDC, 0xE6, 0xC0, 0xC0, 0xC0, 0x00},
    /* s (115) */
    {0x00, 0x00, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x00},
    /* t (116) */
    {0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x1C, 0x00},
    /* u (117) */
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x00},
    /* v (118) */
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00},
    /* w (119) */
    {0x00, 0x00, 0xC6, 0xC6, 0xD6, 0xFE, 0x6C, 0x00},
    /* x (120) */
    {0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00},
    /* y (121) */
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x7C},
    /* z (122) */
    {0x00, 0x00, 0xFE, 0x0C, 0x38, 0x60, 0xFE, 0x00},
    /* { (123) */
    {0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00},
    /* | (124) */
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    /* } (125) */
    {0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00},
    /* ~ (126) */
    {0x72, 0x9C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};


/* =============================================================================
 * 8x16 BITMAP FONT (ASCII 32-126)
 * =============================================================================
 */

static const uint8_t font_8x16[95][16] = {
    /* Space (32) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* ! (33) */
    {0x00, 0x00, 0x18, 0x3C, 0x3C, 0x3C, 0x18, 0x18,
     0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00},
    /* " (34) */
    {0x00, 0x66, 0x66, 0x66, 0x24, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* # (35) */
    {0x00, 0x00, 0x00, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C,
     0x6C, 0xFE, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00},
    /* $ (36) */
    {0x18, 0x18, 0x7C, 0xC6, 0xC2, 0xC0, 0x7C, 0x06,
     0x06, 0x86, 0xC6, 0x7C, 0x18, 0x18, 0x00, 0x00},
    /* % (37) */
    {0x00, 0x00, 0x00, 0x00, 0xC2, 0xC6, 0x0C, 0x18,
     0x30, 0x60, 0xC6, 0x86, 0x00, 0x00, 0x00, 0x00},
    /* & (38) */
    {0x00, 0x00, 0x38, 0x6C, 0x6C, 0x38, 0x76, 0xDC,
     0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00},
    /* ' (39) */
    {0x00, 0x30, 0x30, 0x30, 0x60, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* ( (40) */
    {0x00, 0x00, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x30,
     0x30, 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00},
    /* ) (41) */
    {0x00, 0x00, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x0C,
     0x0C, 0x0C, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00},
    /* * (42) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x3C, 0xFF,
     0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* + (43) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x7E,
     0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* , (44) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00},
    /* - (45) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* . (46) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00},
    /* / (47) */
    {0x00, 0x00, 0x00, 0x00, 0x02, 0x06, 0x0C, 0x18,
     0x30, 0x60, 0xC0, 0x80, 0x00, 0x00, 0x00, 0x00},
    /* 0 (48) */
    {0x00, 0x00, 0x38, 0x6C, 0xC6, 0xC6, 0xD6, 0xD6,
     0xC6, 0xC6, 0x6C, 0x38, 0x00, 0x00, 0x00, 0x00},
    /* 1 (49) */
    {0x00, 0x00, 0x18, 0x38, 0x78, 0x18, 0x18, 0x18,
     0x18, 0x18, 0x18, 0x7E, 0x00, 0x00, 0x00, 0x00},
    /* 2 (50) */
    {0x00, 0x00, 0x7C, 0xC6, 0x06, 0x0C, 0x18, 0x30,
     0x60, 0xC0, 0xC6, 0xFE, 0x00, 0x00, 0x00, 0x00},
    /* 3 (51) */
    {0x00, 0x00, 0x7C, 0xC6, 0x06, 0x06, 0x3C, 0x06,
     0x06, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* 4 (52) */
    {0x00, 0x00, 0x0C, 0x1C, 0x3C, 0x6C, 0xCC, 0xFE,
     0x0C, 0x0C, 0x0C, 0x1E, 0x00, 0x00, 0x00, 0x00},
    /* 5 (53) */
    {0x00, 0x00, 0xFE, 0xC0, 0xC0, 0xC0, 0xFC, 0x06,
     0x06, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* 6 (54) */
    {0x00, 0x00, 0x38, 0x60, 0xC0, 0xC0, 0xFC, 0xC6,
     0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* 7 (55) */
    {0x00, 0x00, 0xFE, 0xC6, 0x06, 0x06, 0x0C, 0x18,
     0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00},
    /* 8 (56) */
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0xC6,
     0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* 9 (57) */
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7E, 0x06,
     0x06, 0x06, 0x0C, 0x78, 0x00, 0x00, 0x00, 0x00},
    /* : (58) */
    {0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00,
     0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* ; (59) */
    {0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00,
     0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00},
    /* < (60) */
    {0x00, 0x00, 0x00, 0x06, 0x0C, 0x18, 0x30, 0x60,
     0x30, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00},
    /* = (61) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00,
     0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* > (62) */
    {0x00, 0x00, 0x00, 0x60, 0x30, 0x18, 0x0C, 0x06,
     0x0C, 0x18, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00},
    /* ? (63) */
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0x0C, 0x18, 0x18,
     0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00},
    /* @ (64) */
    {0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xDE, 0xDE,
     0xDE, 0xDC, 0xC0, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* A (65) */
    {0x00, 0x00, 0x10, 0x38, 0x6C, 0xC6, 0xC6, 0xFE,
     0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00},
    /* B (66) */
    {0x00, 0x00, 0xFC, 0x66, 0x66, 0x66, 0x7C, 0x66,
     0x66, 0x66, 0x66, 0xFC, 0x00, 0x00, 0x00, 0x00},
    /* C (67) */
    {0x00, 0x00, 0x3C, 0x66, 0xC2, 0xC0, 0xC0, 0xC0,
     0xC0, 0xC2, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00},
    /* D (68) */
    {0x00, 0x00, 0xF8, 0x6C, 0x66, 0x66, 0x66, 0x66,
     0x66, 0x66, 0x6C, 0xF8, 0x00, 0x00, 0x00, 0x00},
    /* E (69) */
    {0x00, 0x00, 0xFE, 0x66, 0x62, 0x68, 0x78, 0x68,
     0x60, 0x62, 0x66, 0xFE, 0x00, 0x00, 0x00, 0x00},
    /* F (70) */
    {0x00, 0x00, 0xFE, 0x66, 0x62, 0x68, 0x78, 0x68,
     0x60, 0x60, 0x60, 0xF0, 0x00, 0x00, 0x00, 0x00},
    /* G (71) */
    {0x00, 0x00, 0x3C, 0x66, 0xC2, 0xC0, 0xC0, 0xDE,
     0xC6, 0xC6, 0x66, 0x3A, 0x00, 0x00, 0x00, 0x00},
    /* H (72) */
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xFE, 0xC6,
     0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00},
    /* I (73) */
    {0x00, 0x00, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18,
     0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00},
    /* J (74) */
    {0x00, 0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
     0xCC, 0xCC, 0xCC, 0x78, 0x00, 0x00, 0x00, 0x00},
    /* K (75) */
    {0x00, 0x00, 0xE6, 0x66, 0x66, 0x6C, 0x78, 0x78,
     0x6C, 0x66, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00},
    /* L (76) */
    {0x00, 0x00, 0xF0, 0x60, 0x60, 0x60, 0x60, 0x60,
     0x60, 0x62, 0x66, 0xFE, 0x00, 0x00, 0x00, 0x00},
    /* M (77) */
    {0x00, 0x00, 0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6,
     0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00},
    /* N (78) */
    {0x00, 0x00, 0xC6, 0xE6, 0xF6, 0xFE, 0xDE, 0xCE,
     0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00},
    /* O (79) */
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6,
     0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* P (80) */
    {0x00, 0x00, 0xFC, 0x66, 0x66, 0x66, 0x7C, 0x60,
     0x60, 0x60, 0x60, 0xF0, 0x00, 0x00, 0x00, 0x00},
    /* Q (81) */
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6,
     0xC6, 0xD6, 0xDE, 0x7C, 0x0C, 0x0E, 0x00, 0x00},
    /* R (82) */
    {0x00, 0x00, 0xFC, 0x66, 0x66, 0x66, 0x7C, 0x6C,
     0x66, 0x66, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00},
    /* S (83) */
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0x60, 0x38, 0x0C,
     0x06, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* T (84) */
    {0x00, 0x00, 0x7E, 0x7E, 0x5A, 0x18, 0x18, 0x18,
     0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00},
    /* U (85) */
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6,
     0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* V (86) */
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6,
     0xC6, 0x6C, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00},
    /* W (87) */
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xD6, 0xD6,
     0xD6, 0xFE, 0xEE, 0x6C, 0x00, 0x00, 0x00, 0x00},
    /* X (88) */
    {0x00, 0x00, 0xC6, 0xC6, 0x6C, 0x7C, 0x38, 0x38,
     0x7C, 0x6C, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00},
    /* Y (89) */
    {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18,
     0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00},
    /* Z (90) */
    {0x00, 0x00, 0xFE, 0xC6, 0x86, 0x0C, 0x18, 0x30,
     0x60, 0xC2, 0xC6, 0xFE, 0x00, 0x00, 0x00, 0x00},
    /* [ (91) */
    {0x00, 0x00, 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30,
     0x30, 0x30, 0x30, 0x3C, 0x00, 0x00, 0x00, 0x00},
    /* \ (92) */
    {0x00, 0x00, 0x00, 0x80, 0xC0, 0x60, 0x30, 0x18,
     0x0C, 0x06, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* ] (93) */
    {0x00, 0x00, 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
     0x0C, 0x0C, 0x0C, 0x3C, 0x00, 0x00, 0x00, 0x00},
    /* ^ (94) */
    {0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* _ (95) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00},
    /* ` (96) */
    {0x00, 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* a (97) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x0C, 0x7C,
     0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00},
    /* b (98) */
    {0x00, 0x00, 0xE0, 0x60, 0x60, 0x78, 0x6C, 0x66,
     0x66, 0x66, 0x66, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* c (99) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC0,
     0xC0, 0xC0, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* d (100) */
    {0x00, 0x00, 0x1C, 0x0C, 0x0C, 0x3C, 0x6C, 0xCC,
     0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00},
    /* e (101) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xFE,
     0xC0, 0xC0, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* f (102) */
    {0x00, 0x00, 0x1C, 0x36, 0x32, 0x30, 0x78, 0x30,
     0x30, 0x30, 0x30, 0x78, 0x00, 0x00, 0x00, 0x00},
    /* g (103) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xCC, 0xCC,
     0xCC, 0xCC, 0xCC, 0x7C, 0x0C, 0xCC, 0x78, 0x00},
    /* h (104) */
    {0x00, 0x00, 0xE0, 0x60, 0x60, 0x6C, 0x76, 0x66,
     0x66, 0x66, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00},
    /* i (105) */
    {0x00, 0x00, 0x18, 0x18, 0x00, 0x38, 0x18, 0x18,
     0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00},
    /* j (106) */
    {0x00, 0x00, 0x06, 0x06, 0x00, 0x0E, 0x06, 0x06,
     0x06, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3C, 0x00},
    /* k (107) */
    {0x00, 0x00, 0xE0, 0x60, 0x60, 0x66, 0x6C, 0x78,
     0x78, 0x6C, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00},
    /* l (108) */
    {0x00, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18,
     0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00},
    /* m (109) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xEC, 0xFE, 0xD6,
     0xD6, 0xD6, 0xD6, 0xC6, 0x00, 0x00, 0x00, 0x00},
    /* n (110) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x66, 0x66,
     0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00},
    /* o (111) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6,
     0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* p (112) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x66, 0x66,
     0x66, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00},
    /* q (113) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xCC, 0xCC,
     0xCC, 0xCC, 0xCC, 0x7C, 0x0C, 0x0C, 0x1E, 0x00},
    /* r (114) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x76, 0x66,
     0x60, 0x60, 0x60, 0xF0, 0x00, 0x00, 0x00, 0x00},
    /* s (115) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0x60,
     0x38, 0x0C, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00},
    /* t (116) */
    {0x00, 0x00, 0x10, 0x30, 0x30, 0xFC, 0x30, 0x30,
     0x30, 0x30, 0x36, 0x1C, 0x00, 0x00, 0x00, 0x00},
    /* u (117) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC,
     0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00},
    /* v (118) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66,
     0x66, 0x66, 0x3C, 0x18, 0x00, 0x00, 0x00, 0x00},
    /* w (119) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xD6,
     0xD6, 0xD6, 0xFE, 0x6C, 0x00, 0x00, 0x00, 0x00},
    /* x (120) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x6C, 0x38,
     0x38, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00},
    /* y (121) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6,
     0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0xF8, 0x00},
    /* z (122) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0xCC, 0x18,
     0x30, 0x60, 0xC6, 0xFE, 0x00, 0x00, 0x00, 0x00},
    /* { (123) */
    {0x00, 0x00, 0x0E, 0x18, 0x18, 0x18, 0x70, 0x18,
     0x18, 0x18, 0x18, 0x0E, 0x00, 0x00, 0x00, 0x00},
    /* | (124) */
    {0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18,
     0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00},
    /* } (125) */
    {0x00, 0x00, 0x70, 0x18, 0x18, 0x18, 0x0E, 0x18,
     0x18, 0x18, 0x18, 0x70, 0x00, 0x00, 0x00, 0x00},
    /* ~ (126) */
    {0x00, 0x00, 0x76, 0xDC, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};


/* =============================================================================
 * INTERNAL HELPERS
 * =============================================================================
 */

static inline int32_t min_i32(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t max_i32(int32_t a, int32_t b) { return a > b ? a : b; }
static inline uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }
static inline int32_t abs_i32(int32_t x) { return x < 0 ? -x : x; }

static inline bool is_clipped(const framebuffer_t *fb, uint32_t x, uint32_t y)
{
    const fb_clip_t *clip = &fb->clip_stack[fb->clip_depth];
    return x < clip->x || y < clip->y ||
           x >= clip->x + clip->w || y >= clip->y + clip->h;
}

static inline size_t strlen_local(const char *s)
{
    size_t len = 0;
    while (*s++) len++;
    return len;
}


/* =============================================================================
 * MATH HELPERS
 * =============================================================================
 */

uint32_t fb_isqrt(uint32_t n)
{
    if (n == 0) return 0;
    uint32_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

void fb_sin_cos_deg(uint32_t deg, int32_t *sin_out, int32_t *cos_out)
{
    deg = deg % 360;

    int32_t sin_sign = 1, cos_sign = 1;
    uint32_t lookup_deg;

    if (deg <= 90) {
        lookup_deg = deg;
    } else if (deg <= 180) {
        lookup_deg = 180 - deg;
        cos_sign = -1;
    } else if (deg <= 270) {
        lookup_deg = deg - 180;
        sin_sign = -1;
        cos_sign = -1;
    } else {
        lookup_deg = 360 - deg;
        sin_sign = -1;
    }

    *sin_out = sin_table[lookup_deg] * sin_sign;
    *cos_out = sin_table[90 - lookup_deg] * cos_sign;
}


/* =============================================================================
 * COLOR UTILITIES
 * =============================================================================
 */

uint32_t fb_color_lerp(uint32_t c1, uint32_t c2, uint8_t t)
{
    uint32_t inv_t = 255 - t;

    uint32_t a = (FB_ALPHA(c1) * inv_t + FB_ALPHA(c2) * t) / 255;
    uint32_t r = (FB_RED(c1) * inv_t + FB_RED(c2) * t) / 255;
    uint32_t g = (FB_GREEN(c1) * inv_t + FB_GREEN(c2) * t) / 255;
    uint32_t b = (FB_BLUE(c1) * inv_t + FB_BLUE(c2) * t) / 255;

    return FB_ARGB(a, r, g, b);
}

uint32_t fb_blend_alpha(uint32_t src, uint32_t dst)
{
    uint32_t sa = FB_ALPHA(src);
    if (sa == 0) return dst;
    if (sa == 255) return src;

    uint32_t inv_sa = 255 - sa;

    uint32_t sr = FB_RED(src);
    uint32_t sg = FB_GREEN(src);
    uint32_t sb = FB_BLUE(src);

    uint32_t dr = FB_RED(dst);
    uint32_t dg = FB_GREEN(dst);
    uint32_t db = FB_BLUE(dst);
    uint32_t da = FB_ALPHA(dst);

    uint32_t r = (sr * sa + dr * inv_sa) / 255;
    uint32_t g = (sg * sa + dg * inv_sa) / 255;
    uint32_t b = (sb * sa + db * inv_sa) / 255;
    uint32_t a = (sa * 255 + da * inv_sa) / 255;

    return FB_ARGB(a, r, g, b);
}

uint32_t fb_blend_additive(uint32_t src, uint32_t dst)
{
    uint32_t r = min_u32(FB_RED(src) + FB_RED(dst), 255);
    uint32_t g = min_u32(FB_GREEN(src) + FB_GREEN(dst), 255);
    uint32_t b = min_u32(FB_BLUE(src) + FB_BLUE(dst), 255);
    return FB_RGB(r, g, b);
}

uint32_t fb_blend_multiply(uint32_t src, uint32_t dst)
{
    uint32_t r = (FB_RED(src) * FB_RED(dst)) / 255;
    uint32_t g = (FB_GREEN(src) * FB_GREEN(dst)) / 255;
    uint32_t b = (FB_BLUE(src) * FB_BLUE(dst)) / 255;
    return FB_RGB(r, g, b);
}


/* =============================================================================
 * INITIALIZATION
 * =============================================================================
 */

#if HAS_BCM_MAILBOX

/* BCM platforms: use VideoCore mailbox for framebuffer allocation */

bool fb_init(framebuffer_t *fb)
{
    return fb_init_with_size(fb, FB_DEFAULT_WIDTH, FB_DEFAULT_HEIGHT);
}

bool fb_init_with_size(framebuffer_t *fb, uint32_t width, uint32_t height)
{
    bcm_mailbox_buffer_t mbox __attribute__((aligned(16))) = { .data = {0} };
    uint32_t virtual_height = height * FB_BUFFER_COUNT;

    uint32_t i = 0;
    mbox.data[i++] = 0;
    mbox.data[i++] = BCM_MBOX_REQUEST;

    mbox.data[i++] = BCM_TAG_SET_PHYSICAL_SIZE;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    mbox.data[i++] = width;
    mbox.data[i++] = height;

    mbox.data[i++] = BCM_TAG_SET_VIRTUAL_SIZE;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    mbox.data[i++] = width;
    mbox.data[i++] = virtual_height;

    mbox.data[i++] = BCM_TAG_SET_VIRTUAL_OFFSET;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    mbox.data[i++] = 0;
    mbox.data[i++] = 0;

    mbox.data[i++] = BCM_TAG_SET_DEPTH;
    mbox.data[i++] = 4;
    mbox.data[i++] = 0;
    mbox.data[i++] = FB_BITS_PER_PIXEL;

    mbox.data[i++] = BCM_TAG_SET_PIXEL_ORDER;
    mbox.data[i++] = 4;
    mbox.data[i++] = 0;
    mbox.data[i++] = 0;  /* BGR */

    mbox.data[i++] = BCM_TAG_ALLOCATE_BUFFER;
    mbox.data[i++] = 8;
    mbox.data[i++] = 0;
    uint32_t alloc_idx = i;
    mbox.data[i++] = 16;
    mbox.data[i++] = 0;

    mbox.data[i++] = BCM_TAG_GET_PITCH;
    mbox.data[i++] = 4;
    mbox.data[i++] = 0;
    uint32_t pitch_idx = i;
    mbox.data[i++] = 0;

    mbox.data[i++] = BCM_TAG_END;
    mbox.data[0] = i * 4;

    if (!bcm_mailbox_call(&mbox, BCM_MBOX_CH_PROP)) {
        return false;
    }

    uint32_t fb_addr = mbox.data[alloc_idx];
    uint32_t fb_size_bytes = mbox.data[alloc_idx + 1];
    uint32_t pitch = mbox.data[pitch_idx];

    if (fb_addr == 0 || fb_size_bytes == 0) {
        return false;
    }

    fb_addr = BCM_BUS_TO_ARM(fb_addr);

    fb->width = width;
    fb->height = height;
    fb->pitch = pitch;
    fb->virtual_height = virtual_height;
    fb->buffer_size = pitch * height;

    fb->buffers[0] = (uint32_t *)(uintptr_t)fb_addr;
    if (FB_BUFFER_COUNT > 1) {
        fb->buffers[1] = (uint32_t *)(uintptr_t)(fb_addr + fb->buffer_size);
    }

    fb->front_buffer = 0;
    fb->back_buffer = (FB_BUFFER_COUNT > 1) ? 1 : 0;
    fb->addr = fb->buffers[fb->back_buffer];

    fb->clip_depth = 0;
    fb->clip_stack[0].x = 0;
    fb->clip_stack[0].y = 0;
    fb->clip_stack[0].w = width;
    fb->clip_stack[0].h = height;

    fb->dirty_count = 0;
    fb->full_dirty = true;
    fb->frame_count = 0;
    fb->vsync_enabled = true;
    fb->initialized = true;

    return true;
}

#else /* !HAS_BCM_MAILBOX */

/*
 * Non-BCM platforms: weak stub implementations
 * The real implementations are in the SoC-specific display_*.c files
 */

__attribute__((weak)) bool fb_init(framebuffer_t *fb)
{
    (void)fb;
    return false;  /* Override in display_*.c */
}

__attribute__((weak)) bool fb_init_with_size(framebuffer_t *fb, uint32_t width, uint32_t height)
{
    (void)fb;
    (void)width;
    (void)height;
    return false;  /* Override in display_*.c */
}

#endif /* HAS_BCM_MAILBOX */


/* =============================================================================
 * DISPLAY CONTROL
 * =============================================================================
 */

void fb_set_vsync(framebuffer_t *fb, bool enabled)
{
    fb->vsync_enabled = enabled;
}

static void fb_swap_buffers(framebuffer_t *fb)
{
    if (FB_BUFFER_COUNT < 2) return;

    uint32_t temp = fb->front_buffer;
    fb->front_buffer = fb->back_buffer;
    fb->back_buffer = temp;

    fb->addr = fb->buffers[fb->back_buffer];
}

#if HAS_BCM_MAILBOX

static void fb_flip_display(framebuffer_t *fb)
{
    uint32_t y_offset = fb->front_buffer * fb->height;
    bcm_mailbox_set_virtual_offset(0, y_offset);
}

static void fb_wait_vsync(framebuffer_t *fb)
{
    (void)fb;  /* Unused parameter */

    /* Use the convenience function - falls back to delay if not supported */
    if (!bcm_mailbox_wait_vsync()) {
        /* Fallback: ~16ms delay */
        for (volatile uint32_t i = 0; i < 50000; i++) {
            __asm__ volatile("nop");
        }
    }
}

#else /* !HAS_BCM_MAILBOX */

static void fb_flip_display(framebuffer_t *fb)
{
    (void)fb;  /* Non-BCM: no-op, handled by display_*.c */
}

static void fb_wait_vsync(framebuffer_t *fb)
{
    (void)fb;
    /* Non-BCM: simple delay fallback */
    for (volatile uint32_t i = 0; i < 50000; i++) {
        __asm__ volatile("nop");
    }
}

#endif /* HAS_BCM_MAILBOX */

void fb_present(framebuffer_t *fb)
{
    if (!fb->initialized) return;

    dsb();
    clean_dcache_range((uintptr_t)fb->addr, fb_size(fb));
    dsb();

    fb_swap_buffers(fb);
    fb_flip_display(fb);

    if (fb->vsync_enabled) {
        fb_wait_vsync(fb);
    }

    fb_clear_dirty(fb);
    fb->frame_count++;
}

void fb_present_immediate(framebuffer_t *fb)
{
    if (!fb->initialized) return;

    dsb();
    clean_dcache_range((uintptr_t)fb->addr, fb_size(fb));
    dsb();
}


/* =============================================================================
 * CLIPPING
 * =============================================================================
 */

bool fb_push_clip(framebuffer_t *fb, fb_rect_t rect)
{
    if (fb->clip_depth >= FB_MAX_CLIP_DEPTH - 1) return false;

    fb_clip_t *current = &fb->clip_stack[fb->clip_depth];
    int32_t x1 = max_i32(rect.x, (int32_t)current->x);
    int32_t y1 = max_i32(rect.y, (int32_t)current->y);
    int32_t x2 = min_i32(rect.x + (int32_t)rect.w, (int32_t)(current->x + current->w));
    int32_t y2 = min_i32(rect.y + (int32_t)rect.h, (int32_t)(current->y + current->h));

    fb->clip_depth++;
    fb_clip_t *new_clip = &fb->clip_stack[fb->clip_depth];

    if (x2 <= x1 || y2 <= y1) {
        new_clip->x = new_clip->y = new_clip->w = new_clip->h = 0;
    } else {
        new_clip->x = (uint32_t)x1;
        new_clip->y = (uint32_t)y1;
        new_clip->w = (uint32_t)(x2 - x1);
        new_clip->h = (uint32_t)(y2 - y1);
    }
    return true;
}

void fb_pop_clip(framebuffer_t *fb)
{
    if (fb->clip_depth > 0) fb->clip_depth--;
}

void fb_reset_clip(framebuffer_t *fb)
{
    fb->clip_depth = 0;
    fb->clip_stack[0].x = 0;
    fb->clip_stack[0].y = 0;
    fb->clip_stack[0].w = fb->width;
    fb->clip_stack[0].h = fb->height;
}

fb_clip_t fb_get_clip(const framebuffer_t *fb)
{
    return fb->clip_stack[fb->clip_depth];
}


/* =============================================================================
 * DIRTY TRACKING
 * =============================================================================
 */

void fb_mark_dirty(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (fb->full_dirty) return;
    if (fb->dirty_count >= FB_MAX_DIRTY_RECTS) { fb->full_dirty = true; return; }
    fb->dirty_rects[fb->dirty_count].x = x;
    fb->dirty_rects[fb->dirty_count].y = y;
    fb->dirty_rects[fb->dirty_count].w = w;
    fb->dirty_rects[fb->dirty_count].h = h;
    fb->dirty_count++;
}

void fb_mark_all_dirty(framebuffer_t *fb)
{
    fb->full_dirty = true;
}

void fb_clear_dirty(framebuffer_t *fb)
{
    fb->dirty_count = 0;
    fb->full_dirty = false;
}

bool fb_is_dirty(const framebuffer_t *fb)
{
    return fb->full_dirty || fb->dirty_count > 0;
}


/* =============================================================================
 * BASIC DRAWING
 * =============================================================================
 */

void fb_clear(framebuffer_t *fb, uint32_t color)
{
    uint32_t pitch_words = fb->pitch / 4;
    for (uint32_t y = 0; y < fb->height; y++) {
        uint32_t *row = fb->addr + y * pitch_words;
        for (uint32_t x = 0; x < fb->width; x++) {
            WRITE_VOLATILE(&row[x], color);
        }
    }
    fb_mark_all_dirty(fb);
}

void fb_put_pixel(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= fb->width || y >= fb->height || is_clipped(fb, x, y)) return;
    uint32_t *row = fb->addr + y * (fb->pitch / 4);
    WRITE_VOLATILE(&row[x], color);
}

void fb_put_pixel_blend(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= fb->width || y >= fb->height || is_clipped(fb, x, y)) return;
    uint32_t *row = fb->addr + y * (fb->pitch / 4);
    uint32_t dst = READ_VOLATILE(&row[x]);
    WRITE_VOLATILE(&row[x], fb_blend_alpha(color, dst));
}

void fb_put_pixel_unchecked(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color)
{
    uint32_t *row = fb->addr + y * (fb->pitch / 4);
    WRITE_VOLATILE(&row[x], color);
}

uint32_t fb_get_pixel(const framebuffer_t *fb, uint32_t x, uint32_t y)
{
    if (x >= fb->width || y >= fb->height) return FB_COLOR_TRANSPARENT;
    uint32_t *row = fb->addr + y * (fb->pitch / 4);
    return READ_VOLATILE(&row[x]);
}

void fb_fill_rect(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    const fb_clip_t *clip = &fb->clip_stack[fb->clip_depth];
    uint32_t x1 = max_u32(x, clip->x);
    uint32_t y1 = max_u32(y, clip->y);
    uint32_t x2 = min_u32(x + w, clip->x + clip->w);
    uint32_t y2 = min_u32(y + h, clip->y + clip->h);

    if (x2 <= x1 || y2 <= y1) return;

    uint32_t pitch_words = fb->pitch / 4;
    for (uint32_t py = y1; py < y2; py++) {
        uint32_t *row = fb->addr + py * pitch_words;
        for (uint32_t px = x1; px < x2; px++) {
            WRITE_VOLATILE(&row[px], color);
        }
    }
    fb_mark_dirty(fb, x1, y1, x2 - x1, y2 - y1);
}

void fb_fill_rect_blend(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    const fb_clip_t *clip = &fb->clip_stack[fb->clip_depth];
    uint32_t x1 = max_u32(x, clip->x);
    uint32_t y1 = max_u32(y, clip->y);
    uint32_t x2 = min_u32(x + w, clip->x + clip->w);
    uint32_t y2 = min_u32(y + h, clip->y + clip->h);

    if (x2 <= x1 || y2 <= y1) return;

    uint32_t pitch_words = fb->pitch / 4;
    for (uint32_t py = y1; py < y2; py++) {
        uint32_t *row = fb->addr + py * pitch_words;
        for (uint32_t px = x1; px < x2; px++) {
            uint32_t dst = READ_VOLATILE(&row[px]);
            WRITE_VOLATILE(&row[px], fb_blend_alpha(color, dst));
        }
    }
    fb_mark_dirty(fb, x1, y1, x2 - x1, y2 - y1);
}

void fb_draw_rect(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (w == 0 || h == 0) return;
    fb_draw_hline(fb, x, y, w, color);
    fb_draw_hline(fb, x, y + h - 1, w, color);
    fb_draw_vline(fb, x, y, h, color);
    fb_draw_vline(fb, x + w - 1, y, h, color);
}

void fb_draw_hline(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t len, uint32_t color)
{
    const fb_clip_t *clip = &fb->clip_stack[fb->clip_depth];
    if (y < clip->y || y >= clip->y + clip->h) return;

    uint32_t x1 = max_u32(x, clip->x);
    uint32_t x2 = min_u32(x + len, clip->x + clip->w);
    if (x2 <= x1) return;

    uint32_t *row = fb->addr + y * (fb->pitch / 4);
    for (uint32_t px = x1; px < x2; px++) {
        WRITE_VOLATILE(&row[px], color);
    }
}

/* Helper that handles negative coordinates safely */
static void fb_draw_hline_safe(framebuffer_t *fb, int32_t x, int32_t y, int32_t len, uint32_t color)
{
    /* Clip to screen bounds */
    if (y < 0 || y >= (int32_t)fb->height) return;
    if (x >= (int32_t)fb->width) return;
    if (x + len <= 0) return;

    if (x < 0) {
        len += x;  /* Reduce length by off-screen amount */
        x = 0;
    }
    if (x + len > (int32_t)fb->width) {
        len = (int32_t)fb->width - x;
    }

    if (len > 0) {
        fb_draw_hline(fb, (uint32_t)x, (uint32_t)y, (uint32_t)len, color);
    }
}

void fb_draw_vline(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t len, uint32_t color)
{
    const fb_clip_t *clip = &fb->clip_stack[fb->clip_depth];
    if (x < clip->x || x >= clip->x + clip->w) return;

    uint32_t y1 = max_u32(y, clip->y);
    uint32_t y2 = min_u32(y + len, clip->y + clip->h);
    if (y2 <= y1) return;

    uint32_t pitch_words = fb->pitch / 4;
    for (uint32_t py = y1; py < y2; py++) {
        uint32_t *row = fb->addr + py * pitch_words;
        WRITE_VOLATILE(&row[x], color);
    }
}

void fb_draw_line(framebuffer_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color)
{
    int32_t dx = abs_i32(x1 - x0);
    int32_t dy = -abs_i32(y1 - y0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;

    while (1) {
        if (x0 >= 0 && y0 >= 0) {
            fb_put_pixel(fb, (uint32_t)x0, (uint32_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void fb_draw_line_thick(framebuffer_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t thickness, uint32_t color)
{
    if (thickness <= 1) {
        fb_draw_line(fb, x0, y0, x1, y1, color);
        return;
    }

    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    int32_t len_sq = dx * dx + dy * dy;

    if (len_sq == 0) {
        fb_fill_circle(fb, x0, y0, thickness / 2, color);
        return;
    }

    int32_t len = (int32_t)fb_isqrt((uint32_t)len_sq);
    if (len == 0) {
        fb_fill_circle(fb, x0, y0, thickness / 2, color);
        return;
    }

    int32_t half_thick = (int32_t)thickness / 2;

    for (int32_t offset = -half_thick; offset <= half_thick; offset++) {
        int32_t ox = (-dy * offset) / len;
        int32_t oy = (dx * offset) / len;
        fb_draw_line(fb, x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
    }
}

/* =============================================================================
 * ADVANCED DRAWING
 * =============================================================================
 */

void fb_fill_circle(framebuffer_t *fb, int32_t cx, int32_t cy, uint32_t radius, uint32_t color)
{
    if (radius == 0) {
        if (cx >= 0 && cy >= 0 && cx < (int32_t)fb->width && cy < (int32_t)fb->height) {
            fb_put_pixel(fb, (uint32_t)cx, (uint32_t)cy, color);
        }
        return;
    }

    int32_t r = (int32_t)radius;
    int32_t x = 0;
    int32_t y = r;
    int32_t d = 1 - r;

    while (x <= y) {
        /* Draw horizontal spans for all 4 quadrants */
        fb_draw_hline_safe(fb, cx - x, cy + y, 2 * x + 1, color);
        fb_draw_hline_safe(fb, cx - x, cy - y, 2 * x + 1, color);
        fb_draw_hline_safe(fb, cx - y, cy + x, 2 * y + 1, color);
        fb_draw_hline_safe(fb, cx - y, cy - x, 2 * y + 1, color);

        /* Midpoint circle algorithm */
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

void fb_draw_circle(framebuffer_t *fb, int32_t cx, int32_t cy, uint32_t radius, uint32_t color)
{
    if (radius == 0) {
        if (cx >= 0 && cy >= 0) fb_put_pixel(fb, (uint32_t)cx, (uint32_t)cy, color);
        return;
    }

    int32_t x = 0, y = (int32_t)radius, d = 1 - (int32_t)radius;

    while (x <= y) {
        fb_put_pixel(fb, (uint32_t)(cx + x), (uint32_t)(cy + y), color);
        fb_put_pixel(fb, (uint32_t)(cx - x), (uint32_t)(cy + y), color);
        fb_put_pixel(fb, (uint32_t)(cx + x), (uint32_t)(cy - y), color);
        fb_put_pixel(fb, (uint32_t)(cx - x), (uint32_t)(cy - y), color);
        fb_put_pixel(fb, (uint32_t)(cx + y), (uint32_t)(cy + x), color);
        fb_put_pixel(fb, (uint32_t)(cx - y), (uint32_t)(cy + x), color);
        fb_put_pixel(fb, (uint32_t)(cx + y), (uint32_t)(cy - x), color);
        fb_put_pixel(fb, (uint32_t)(cx - y), (uint32_t)(cy - x), color);

        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

void fb_draw_arc(framebuffer_t *fb, uint32_t cx, uint32_t cy, uint32_t radius,
                 uint32_t start_deg, uint32_t end_deg, uint32_t color)
{
    if (radius == 0) {
        fb_put_pixel(fb, cx, cy, color);
        return;
    }

    uint32_t start = start_deg % 360;
    uint32_t end = (end_deg <= start_deg) ? end_deg + 360 : end_deg;

    int32_t prev_x = -1, prev_y = -1;

    for (uint32_t deg = start; deg <= end; deg++) {
        uint32_t angle = deg % 360;
        int32_t sin_val, cos_val;
        fb_sin_cos_deg(angle, &sin_val, &cos_val);

        int32_t px = (int32_t)cx + ((cos_val * (int32_t)radius) >> 8);
        int32_t py = (int32_t)cy + ((sin_val * (int32_t)radius) >> 8);

        if (prev_x >= 0 && prev_y >= 0 && (px != prev_x || py != prev_y)) {
            fb_draw_line(fb, prev_x, prev_y, px, py, color);
        }

        prev_x = px;
        prev_y = py;
    }
}

void fb_fill_rounded_rect(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t radius, uint32_t color)
{
    if (radius == 0) { fb_fill_rect(fb, x, y, w, h, color); return; }
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    /* Center rectangles */
    fb_fill_rect(fb, x + radius, y, w - 2 * radius, h, color);
    fb_fill_rect(fb, x, y + radius, radius, h - 2 * radius, color);
    fb_fill_rect(fb, x + w - radius, y + radius, radius, h - 2 * radius, color);

    /* Corner circles using the filled circle approach for quarter circles */
    int32_t r = (int32_t)radius;
    for (int32_t dy = 0; dy <= r; dy++) {
        for (int32_t dx = 0; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                /* Top-left */
                fb_put_pixel(fb, x + radius - dx, y + radius - dy, color);
                /* Top-right */
                fb_put_pixel(fb, x + w - 1 - radius + dx, y + radius - dy, color);
                /* Bottom-left */
                fb_put_pixel(fb, x + radius - dx, y + h - 1 - radius + dy, color);
                /* Bottom-right */
                fb_put_pixel(fb, x + w - 1 - radius + dx, y + h - 1 - radius + dy, color);
            }
        }
    }
}

void fb_draw_rounded_rect(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t radius, uint32_t color)
{
    if (radius == 0) { fb_draw_rect(fb, x, y, w, h, color); return; }
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    /* Straight edges */
    fb_draw_hline(fb, x + radius, y, w - 2 * radius, color);
    fb_draw_hline(fb, x + radius, y + h - 1, w - 2 * radius, color);
    fb_draw_vline(fb, x, y + radius, h - 2 * radius, color);
    fb_draw_vline(fb, x + w - 1, y + radius, h - 2 * radius, color);

    /* Corner arcs using midpoint circle algorithm */
    int32_t r = (int32_t)radius;
    int32_t px = 0, py = r, d = 1 - r;

    while (px <= py) {
        /* Top-left */
        fb_put_pixel(fb, x + radius - px, y + radius - py, color);
        fb_put_pixel(fb, x + radius - py, y + radius - px, color);
        /* Top-right */
        fb_put_pixel(fb, x + w - 1 - radius + px, y + radius - py, color);
        fb_put_pixel(fb, x + w - 1 - radius + py, y + radius - px, color);
        /* Bottom-left */
        fb_put_pixel(fb, x + radius - px, y + h - 1 - radius + py, color);
        fb_put_pixel(fb, x + radius - py, y + h - 1 - radius + px, color);
        /* Bottom-right */
        fb_put_pixel(fb, x + w - 1 - radius + px, y + h - 1 - radius + py, color);
        fb_put_pixel(fb, x + w - 1 - radius + py, y + h - 1 - radius + px, color);

        if (d < 0) {
            d += 2 * px + 3;
        } else {
            d += 2 * (px - py) + 5;
            py--;
        }
        px++;
    }
}

void fb_draw_triangle(framebuffer_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint32_t color)
{
    fb_draw_line(fb, x0, y0, x1, y1, color);
    fb_draw_line(fb, x1, y1, x2, y2, color);
    fb_draw_line(fb, x2, y2, x0, y0, color);
}

static void fb_fill_flat_bottom_triangle(framebuffer_t *fb, int32_t x0, int32_t y0,
                                         int32_t x1, int32_t y1, int32_t x2, uint32_t color)
{
    if (y1 == y0) return;

    int32_t dy = y1 - y0;
    int32_t dx1 = x1 - x0;
    int32_t dx2 = x2 - x0;

    for (int32_t y = y0; y <= y1; y++) {
        int32_t t = y - y0;
        int32_t cx1 = x0 + (dx1 * t) / dy;
        int32_t cx2 = x0 + (dx2 * t) / dy;
        if (cx1 > cx2) { int32_t tmp = cx1; cx1 = cx2; cx2 = tmp; }
        if (y >= 0) {
            fb_draw_hline(fb, (uint32_t)max_i32(cx1, 0), (uint32_t)y,
                         (uint32_t)(cx2 - cx1 + 1), color);
        }
    }
}

static void fb_fill_flat_top_triangle(framebuffer_t *fb, int32_t x0, int32_t x1,
                                      int32_t y0, int32_t x2, int32_t y2, uint32_t color)
{
    if (y2 == y0) return;

    int32_t dy = y2 - y0;
    int32_t dx1 = x2 - x0;
    int32_t dx2 = x2 - x1;

    for (int32_t y = y2; y >= y0; y--) {
        int32_t t = y2 - y;
        int32_t cx1 = x2 - (dx1 * t) / dy;
        int32_t cx2 = x2 - (dx2 * t) / dy;
        if (cx1 > cx2) { int32_t tmp = cx1; cx1 = cx2; cx2 = tmp; }
        if (y >= 0) {
            fb_draw_hline(fb, (uint32_t)max_i32(cx1, 0), (uint32_t)y,
                         (uint32_t)(cx2 - cx1 + 1), color);
        }
    }
}

void fb_fill_triangle(framebuffer_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint32_t color)
{
    /* Sort vertices by y coordinate */
    if (y0 > y1) { int32_t t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y1 > y2) { int32_t t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (y0 > y1) { int32_t t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }

    if (y0 == y2) return;  /* Degenerate */

    if (y1 == y2) {
        fb_fill_flat_bottom_triangle(fb, x0, y0, x1, y1, x2, color);
    } else if (y0 == y1) {
        fb_fill_flat_top_triangle(fb, x0, x1, y0, x2, y2, color);
    } else {
        int32_t x3 = x0 + ((y1 - y0) * (x2 - x0)) / (y2 - y0);
        fb_fill_flat_bottom_triangle(fb, x0, y0, x1, y1, x3, color);
        fb_fill_flat_top_triangle(fb, x1, x3, y1, x2, y2, color);
    }
}

void fb_fill_rect_gradient_v(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t top_color, uint32_t bottom_color)
{
    for (uint32_t row = 0; row < h; row++) {
        uint8_t t = (h > 1) ? (row * 255) / (h - 1) : 0;
        uint32_t c = fb_color_lerp(top_color, bottom_color, t);
        fb_draw_hline(fb, x, y + row, w, c);
    }
}

void fb_fill_rect_gradient_h(framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t left_color, uint32_t right_color)
{
    for (uint32_t col = 0; col < w; col++) {
        uint8_t t = (w > 1) ? (col * 255) / (w - 1) : 0;
        uint32_t c = fb_color_lerp(left_color, right_color, t);
        fb_draw_vline(fb, x + col, y, h, c);
    }
}

void fb_fade(framebuffer_t *fb, uint8_t amount)
{
    uint32_t fade_color = FB_WITH_ALPHA(FB_COLOR_BLACK, amount);
    fb_fill_rect_blend(fb, 0, 0, fb->width, fb->height, fade_color);
}


/* =============================================================================
 * TEXT RENDERING
 * =============================================================================
 */

void fb_draw_char(framebuffer_t *fb, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg)
{
    uint8_t ch = (uint8_t)c;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *glyph = font_8x8[ch - 32];

    for (uint32_t row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            fb_put_pixel(fb, x + col, y + row, color);
        }
    }
}

void fb_draw_char_transparent(framebuffer_t *fb, uint32_t x, uint32_t y, char c, uint32_t fg)
{
    uint8_t ch = (uint8_t)c;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *glyph = font_8x8[ch - 32];

    for (uint32_t row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                fb_put_pixel(fb, x + col, y + row, fg);
            }
        }
    }
}

void fb_draw_char_large(framebuffer_t *fb, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg)
{
    uint8_t ch = (uint8_t)c;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *glyph = font_8x16[ch - 32];

    for (uint32_t row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            fb_put_pixel(fb, x + col, y + row, color);
        }
    }
}

void fb_draw_char_large_transparent(framebuffer_t *fb, uint32_t x, uint32_t y, char c, uint32_t fg)
{
    uint8_t ch = (uint8_t)c;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *glyph = font_8x16[ch - 32];

    for (uint32_t row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                fb_put_pixel(fb, x + col, y + row, fg);
            }
        }
    }
}

void fb_draw_char_scaled(framebuffer_t *fb, uint32_t x, uint32_t y, char c,
                         uint32_t fg, uint32_t bg, uint32_t scale)
{
    uint8_t ch = (uint8_t)c;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *glyph = font_8x8[ch - 32];

    for (uint32_t row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            for (uint32_t sy = 0; sy < scale; sy++) {
                for (uint32_t sx = 0; sx < scale; sx++) {
                    fb_put_pixel(fb, x + col * scale + sx, y + row * scale + sy, color);
                }
            }
        }
    }
}

void fb_draw_string(framebuffer_t *fb, uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg)
{
    while (*str) {
        if (x + FB_CHAR_WIDTH > fb->width) break;
        fb_draw_char(fb, x, y, *str++, fg, bg);
        x += FB_CHAR_WIDTH;
    }
}

void fb_draw_string_transparent(framebuffer_t *fb, uint32_t x, uint32_t y, const char *str, uint32_t fg)
{
    while (*str) {
        if (x + FB_CHAR_WIDTH > fb->width) break;
        fb_draw_char_transparent(fb, x, y, *str++, fg);
        x += FB_CHAR_WIDTH;
    }
}

void fb_draw_string_large(framebuffer_t *fb, uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg)
{
    while (*str) {
        if (x + FB_CHAR_WIDTH_LG > fb->width) break;
        fb_draw_char_large(fb, x, y, *str++, fg, bg);
        x += FB_CHAR_WIDTH_LG;
    }
}

void fb_draw_string_large_transparent(framebuffer_t *fb, uint32_t x, uint32_t y, const char *str, uint32_t fg)
{
    while (*str) {
        if (x + FB_CHAR_WIDTH_LG > fb->width) break;
        fb_draw_char_large_transparent(fb, x, y, *str++, fg);
        x += FB_CHAR_WIDTH_LG;
    }
}

void fb_draw_string_scaled(framebuffer_t *fb, uint32_t x, uint32_t y, const char *str,
                           uint32_t fg, uint32_t bg, uint32_t scale)
{
    uint32_t char_w = FB_CHAR_WIDTH * scale;
    while (*str) {
        if (x + char_w > fb->width) break;
        fb_draw_char_scaled(fb, x, y, *str++, fg, bg, scale);
        x += char_w;
    }
}

void fb_draw_string_centered(framebuffer_t *fb, uint32_t y, const char *str, uint32_t fg, uint32_t bg)
{
    uint32_t text_width = fb_text_width(str);
    uint32_t x = (text_width < fb->width) ? (fb->width - text_width) / 2 : 0;
    fb_draw_string(fb, x, y, str, fg, bg);
}

void fb_draw_string_center(framebuffer_t *fb, const char *str, uint32_t fg, uint32_t bg)
{
    uint32_t text_width = fb_text_width(str);
    uint32_t x = (text_width < fb->width) ? (fb->width - text_width) / 2 : 0;
    uint32_t y = (fb->height - FB_CHAR_HEIGHT) / 2;
    fb_draw_string(fb, x, y, str, fg, bg);
}

uint32_t fb_measure_string(const char *str, bool large)
{
    return (uint32_t)strlen_local(str) * (large ? FB_CHAR_WIDTH_LG : FB_CHAR_WIDTH);
}

uint32_t fb_text_width(const char *str)
{
    return (uint32_t)strlen_local(str) * FB_CHAR_WIDTH;
}

uint32_t fb_text_width_large(const char *str)
{
    return (uint32_t)strlen_local(str) * FB_CHAR_WIDTH_LG;
}


/* =============================================================================
 * BITMAP BLITTING
 * =============================================================================
 */

void fb_blit_bitmap(framebuffer_t *fb, int32_t x, int32_t y, const fb_bitmap_t *bitmap)
{
    fb_blit_bitmap_blend(fb, x, y, bitmap, FB_BLEND_OPAQUE);
}

void fb_blit_bitmap_alpha(framebuffer_t *fb, int32_t x, int32_t y, const fb_bitmap_t *bitmap)
{
    fb_blit_bitmap_blend(fb, x, y, bitmap, FB_BLEND_ALPHA);
}

void fb_blit_bitmap_blend(framebuffer_t *fb, int32_t x, int32_t y, const fb_bitmap_t *bitmap,
                          fb_blend_mode_t blend)
{
    if (!bitmap || !bitmap->data) return;

    const fb_clip_t *clip = &fb->clip_stack[fb->clip_depth];

    uint32_t src_x = (x < (int32_t)clip->x) ? ((int32_t)clip->x - x) : 0;
    uint32_t src_y = (y < (int32_t)clip->y) ? ((int32_t)clip->y - y) : 0;

    uint32_t dst_x = (uint32_t)max_i32(x, (int32_t)clip->x);
    uint32_t dst_y = (uint32_t)max_i32(y, (int32_t)clip->y);

    uint32_t w = bitmap->width - src_x;
    if (dst_x + w > clip->x + clip->w) w = clip->x + clip->w - dst_x;
    if (dst_x + w > fb->width) w = fb->width - dst_x;

    uint32_t h = bitmap->height - src_y;
    if (dst_y + h > clip->y + clip->h) h = clip->y + clip->h - dst_y;
    if (dst_y + h > fb->height) h = fb->height - dst_y;

    if (w == 0 || h == 0) return;

    uint32_t pitch_words = fb->pitch / 4;

    for (uint32_t row = 0; row < h; row++) {
        uint32_t src_row = (src_y + row) * bitmap->width;
        uint32_t *dst_row = fb->addr + (dst_y + row) * pitch_words + dst_x;

        for (uint32_t col = 0; col < w; col++) {
            uint32_t src_pixel = bitmap->data[src_row + src_x + col];

            switch (blend) {
                case FB_BLEND_OPAQUE:
                    WRITE_VOLATILE(&dst_row[col], src_pixel);
                    break;
                case FB_BLEND_ALPHA: {
                    uint32_t dst_pixel = READ_VOLATILE(&dst_row[col]);
                    WRITE_VOLATILE(&dst_row[col], fb_blend_alpha(src_pixel, dst_pixel));
                    break;
                }
                case FB_BLEND_ADDITIVE: {
                    uint32_t dst_pixel = READ_VOLATILE(&dst_row[col]);
                    WRITE_VOLATILE(&dst_row[col], fb_blend_additive(src_pixel, dst_pixel));
                    break;
                }
                case FB_BLEND_MULTIPLY: {
                    uint32_t dst_pixel = READ_VOLATILE(&dst_row[col]);
                    WRITE_VOLATILE(&dst_row[col], fb_blend_multiply(src_pixel, dst_pixel));
                    break;
                }
            }
        }
    }

    fb_mark_dirty(fb, dst_x, dst_y, w, h);
}

void fb_blit_bitmap_scaled(framebuffer_t *fb, int32_t x, int32_t y, const fb_bitmap_t *bitmap,
                           uint32_t scale_x, uint32_t scale_y)
{
    if (!bitmap || !bitmap->data || scale_x == 0 || scale_y == 0) return;

    uint32_t dst_w = bitmap->width * scale_x;
    uint32_t dst_h = bitmap->height * scale_y;

    for (uint32_t dy = 0; dy < dst_h; dy++) {
        uint32_t src_y = dy / scale_y;
        for (uint32_t dx = 0; dx < dst_w; dx++) {
            uint32_t src_x = dx / scale_x;
            uint32_t pixel = bitmap->data[src_y * bitmap->width + src_x];
            int32_t px = x + (int32_t)dx;
            int32_t py = y + (int32_t)dy;
            if (px >= 0 && py >= 0) {
                fb_put_pixel(fb, (uint32_t)px, (uint32_t)py, pixel);
            }
        }
    }
}

void fb_blit_bitmap_region(framebuffer_t *fb, const fb_bitmap_t *bitmap,
                           uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h,
                           int32_t dst_x, int32_t dst_y)
{
    if (!bitmap || !bitmap->data) return;

    for (uint32_t row = 0; row < src_h; row++) {
        for (uint32_t col = 0; col < src_w; col++) {
            uint32_t pixel = bitmap->data[(src_y + row) * bitmap->width + src_x + col];
            int32_t px = dst_x + (int32_t)col;
            int32_t py = dst_y + (int32_t)row;
            if (px >= 0 && py >= 0 && FB_ALPHA(pixel) > 0) {
                fb_put_pixel_blend(fb, (uint32_t)px, (uint32_t)py, pixel);
            }
        }
    }
}


/* =============================================================================
 * SCREEN OPERATIONS
 * =============================================================================
 */

void fb_copy_rect(framebuffer_t *fb, uint32_t src_x, uint32_t src_y,
                  uint32_t dst_x, uint32_t dst_y, uint32_t w, uint32_t h)
{
    uint32_t pitch_words = fb->pitch / 4;

    /* Handle overlapping regions by copying in the right order */
    if (src_y < dst_y || (src_y == dst_y && src_x < dst_x)) {
        /* Copy bottom-to-top, right-to-left */
        for (uint32_t row = h; row > 0; row--) {
            for (uint32_t col = w; col > 0; col--) {
                uint32_t src_idx = (src_y + row - 1) * pitch_words + (src_x + col - 1);
                uint32_t dst_idx = (dst_y + row - 1) * pitch_words + (dst_x + col - 1);
                uint32_t pixel = READ_VOLATILE(&fb->addr[src_idx]);
                WRITE_VOLATILE(&fb->addr[dst_idx], pixel);
            }
        }
    } else {
        /* Copy top-to-bottom, left-to-right */
        for (uint32_t row = 0; row < h; row++) {
            for (uint32_t col = 0; col < w; col++) {
                uint32_t src_idx = (src_y + row) * pitch_words + (src_x + col);
                uint32_t dst_idx = (dst_y + row) * pitch_words + (dst_x + col);
                uint32_t pixel = READ_VOLATILE(&fb->addr[src_idx]);
                WRITE_VOLATILE(&fb->addr[dst_idx], pixel);
            }
        }
    }

    fb_mark_dirty(fb, dst_x, dst_y, w, h);
}

void fb_scroll_v(framebuffer_t *fb, int32_t pixels, uint32_t fill_color)
{
    if (pixels == 0) return;

    uint32_t abs_pixels = (uint32_t)abs_i32(pixels);
    if (abs_pixels >= fb->height) {
        fb_clear(fb, fill_color);
        return;
    }

    if (pixels > 0) {
        /* Scroll down */
        fb_copy_rect(fb, 0, 0, 0, abs_pixels, fb->width, fb->height - abs_pixels);
        fb_fill_rect(fb, 0, 0, fb->width, abs_pixels, fill_color);
    } else {
        /* Scroll up */
        fb_copy_rect(fb, 0, abs_pixels, 0, 0, fb->width, fb->height - abs_pixels);
        fb_fill_rect(fb, 0, fb->height - abs_pixels, fb->width, abs_pixels, fill_color);
    }
}

void fb_scroll_h(framebuffer_t *fb, int32_t pixels, uint32_t fill_color)
{
    if (pixels == 0) return;

    uint32_t abs_pixels = (uint32_t)abs_i32(pixels);
    if (abs_pixels >= fb->width) {
        fb_clear(fb, fill_color);
        return;
    }

    if (pixels > 0) {
        /* Scroll right */
        fb_copy_rect(fb, 0, 0, abs_pixels, 0, fb->width - abs_pixels, fb->height);
        fb_fill_rect(fb, 0, 0, abs_pixels, fb->height, fill_color);
    } else {
        /* Scroll left */
        fb_copy_rect(fb, abs_pixels, 0, 0, 0, fb->width - abs_pixels, fb->height);
        fb_fill_rect(fb, fb->width - abs_pixels, 0, abs_pixels, fb->height, fill_color);
    }
}


/* =============================================================================
 * GAMEBOY SCREEN BLITTING
 * =============================================================================
 */

void fb_blit_gb_screen_dmg(framebuffer_t *fb, const uint8_t *pal_data)
{
    fb_blit_gb_screen_dmg_palette(fb, pal_data, gb_palette);
}

void fb_blit_gb_screen_dmg_palette(framebuffer_t *fb, const uint8_t *pal_data, const uint32_t *palette)
{
    if (!pal_data) return;
    if (!palette) palette = gb_palette;

    uint32_t scanline[GB_WIDTH * GB_SCALE];
    uint32_t pitch_words = fb->pitch / 4;

    for (uint32_t y = 0; y < GB_HEIGHT; y++) {
        uint32_t src_row = y * GB_WIDTH;

        for (uint32_t x = 0; x < GB_WIDTH; x++) {
            uint8_t pal_idx = pal_data[src_row + x];
            uint32_t color = (pal_idx < 4) ? palette[pal_idx] : FB_COLOR_BLACK;
            scanline[x * 2] = color;
            scanline[x * 2 + 1] = color;
        }

        uint32_t dst_y = GB_OFFSET_Y + y * GB_SCALE;
        uint32_t *row0 = fb->addr + dst_y * pitch_words + GB_OFFSET_X;
        uint32_t *row1 = fb->addr + (dst_y + 1) * pitch_words + GB_OFFSET_X;

        for (uint32_t i = 0; i < GB_SCALED_W; i++) {
            WRITE_VOLATILE(&row0[i], scanline[i]);
            WRITE_VOLATILE(&row1[i], scanline[i]);
        }
    }

    dsb();
}

void fb_blit_gb_screen_gbc(framebuffer_t *fb, const uint8_t *rgb_data)
{
    if (!rgb_data) return;

    uint32_t scanline[GB_WIDTH * GB_SCALE];
    uint32_t pitch_words = fb->pitch / 4;

    for (uint32_t y = 0; y < GB_HEIGHT; y++) {
        uint32_t src_row = y * GB_WIDTH * 3;

        for (uint32_t x = 0; x < GB_WIDTH; x++) {
            uint32_t idx = src_row + x * 3;
            uint32_t color = 0xFF000000 |
                            ((uint32_t)rgb_data[idx] << 16) |
                            ((uint32_t)rgb_data[idx + 1] << 8) |
                            (uint32_t)rgb_data[idx + 2];
            scanline[x * 2] = color;
            scanline[x * 2 + 1] = color;
        }

        uint32_t dst_y = GB_OFFSET_Y + y * GB_SCALE;
        uint32_t *row0 = fb->addr + dst_y * pitch_words + GB_OFFSET_X;
        uint32_t *row1 = fb->addr + (dst_y + 1) * pitch_words + GB_OFFSET_X;

        for (uint32_t i = 0; i < GB_SCALED_W; i++) {
            WRITE_VOLATILE(&row0[i], scanline[i]);
            WRITE_VOLATILE(&row1[i], scanline[i]);
        }
    }

    dsb();
}

void fb_draw_gb_border(framebuffer_t *fb, uint32_t color)
{
    uint32_t border = 4;
    uint32_t x = GB_OFFSET_X - border;
    uint32_t y = GB_OFFSET_Y - border;
    uint32_t w = GB_SCALED_W + border * 2;
    uint32_t h = GB_SCALED_H + border * 2;

    fb_fill_rect(fb, x, y, w, border, color);
    fb_fill_rect(fb, x, y + h - border, w, border, color);
    fb_fill_rect(fb, x, y, border, h, color);
    fb_fill_rect(fb, x + w - border, y, border, h, color);
}