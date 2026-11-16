#pragma once

/* Build-selected CO5300 geometry shared by the provider and native backend.
 * Override these macros consistently for both translation units. Defaults keep
 * the deployed 2.06-inch profile; there is no board-ID switch in the driver.
 * The two-row transport requires even physical starts and inclusive odd ends.
 * Reset timings are the active-low pulse and the subsequent high recovery time.
 */
#ifndef RISC_CO5300_WIDTH
#define RISC_CO5300_WIDTH 410
#endif
#ifndef RISC_CO5300_HEIGHT
#define RISC_CO5300_HEIGHT 502
#endif
#ifndef RISC_CO5300_OFFSET_X
#define RISC_CO5300_OFFSET_X 22
#endif
#ifndef RISC_CO5300_OFFSET_Y
#define RISC_CO5300_OFFSET_Y 0
#endif
#ifndef RISC_CO5300_RESET_LOW_MS
#define RISC_CO5300_RESET_LOW_MS 200
#endif
#ifndef RISC_CO5300_RESET_RECOVERY_MS
#define RISC_CO5300_RESET_RECOVERY_MS 200
#endif
#ifndef RISC_CO5300_PIXEL_WRITE_ADDRESS
#define RISC_CO5300_PIXEL_WRITE_ADDRESS 0x003c00
#endif
#ifndef RISC_CO5300_RAMWR_BEFORE_PIXELS
#define RISC_CO5300_RAMWR_BEFORE_PIXELS 1
#endif
#ifndef RISC_CO5300_SAFE_TOP
#define RISC_CO5300_SAFE_TOP 0
#endif
#ifndef RISC_CO5300_SAFE_RIGHT
#define RISC_CO5300_SAFE_RIGHT 0
#endif
#ifndef RISC_CO5300_SAFE_BOTTOM
#define RISC_CO5300_SAFE_BOTTOM 0
#endif
#ifndef RISC_CO5300_SAFE_LEFT
#define RISC_CO5300_SAFE_LEFT 0
#endif

/* X(register, byte_count, byte0, byte1, byte2, byte3, post_delay_ms).
 * Board settings may replace the whole sequence before including this header.
 * Keep normal brightness zero before display-on: only a checked application
 * frame may release deferred brightness. The backend validates that invariant.
 */
#ifndef RISC_CO5300_INIT_COMMANDS
#define RISC_CO5300_INIT_COMMANDS(X) \
 X(0x11,0,0,0,0,0,120) \
 X(0xfe,1,0x00,0,0,0,0) \
 X(0xc4,1,0x80,0,0,0,0) \
 X(0x3a,1,0x55,0,0,0,0) \
 X(0x53,1,0x20,0,0,0,0) \
 X(0x63,1,0xff,0,0,0,0) \
 X(0x51,1,0x00,0,0,0,0) \
 X(0x29,0,0,0,0,0,0) \
 X(0x51,1,0x00,0,0,0,0) \
 X(0x58,1,0x00,0,0,0,10) \
 X(0x20,0,0,0,0,0,0) \
 X(0x36,1,0x00,0,0,0,0)
#endif

#if RISC_CO5300_WIDTH <= 0 || RISC_CO5300_WIDTH > 65535 || (RISC_CO5300_WIDTH % 2) != 0
#error "CO5300 width must be positive, even and fit the display ABI"
#endif
#if RISC_CO5300_HEIGHT <= 0 || RISC_CO5300_HEIGHT > 65535 || (RISC_CO5300_HEIGHT % 2) != 0
#error "CO5300 height must be positive, even and fit the display ABI"
#endif
#if RISC_CO5300_OFFSET_X < 0 || RISC_CO5300_OFFSET_X > 65535 || (RISC_CO5300_OFFSET_X % 2) != 0
#error "CO5300 X offset must be nonnegative, even and fit the display ABI"
#endif
#if RISC_CO5300_OFFSET_Y < 0 || RISC_CO5300_OFFSET_Y > 65535 || (RISC_CO5300_OFFSET_Y % 2) != 0
#error "CO5300 Y offset must be nonnegative, even and fit the display ABI"
#endif
#if RISC_CO5300_OFFSET_X + RISC_CO5300_WIDTH > 65536 || RISC_CO5300_OFFSET_Y + RISC_CO5300_HEIGHT > 65536
#error "CO5300 inclusive window addresses must fit 16 bits"
#endif
#if RISC_CO5300_RESET_LOW_MS <= 0 || RISC_CO5300_RESET_LOW_MS > 500 || RISC_CO5300_RESET_RECOVERY_MS <= 0 || RISC_CO5300_RESET_RECOVERY_MS > 500
#error "CO5300 reset pulse and recovery must be 1..500ms per the display ABI"
#endif
#if RISC_CO5300_RAMWR_BEFORE_PIXELS != 0 && RISC_CO5300_RAMWR_BEFORE_PIXELS != 1
#error "CO5300 RAMWR-before-pixels selector must be zero or one"
#endif
#if RISC_CO5300_PIXEL_WRITE_ADDRESS < 0 || RISC_CO5300_PIXEL_WRITE_ADDRESS > 0xffffff || (RISC_CO5300_PIXEL_WRITE_ADDRESS & 0xff) != 0
#error "CO5300 pixel-write address must be a register in the 24-bit address phase"
#endif
#if RISC_CO5300_SAFE_TOP < 0 || RISC_CO5300_SAFE_RIGHT < 0 || RISC_CO5300_SAFE_BOTTOM < 0 || RISC_CO5300_SAFE_LEFT < 0 || RISC_CO5300_SAFE_TOP >= RISC_CO5300_HEIGHT || RISC_CO5300_SAFE_BOTTOM >= RISC_CO5300_HEIGHT || RISC_CO5300_SAFE_LEFT >= RISC_CO5300_WIDTH || RISC_CO5300_SAFE_RIGHT >= RISC_CO5300_WIDTH
#error "CO5300 safe insets must be nonnegative and fit the selected geometry"
#endif
#if RISC_CO5300_SAFE_TOP + RISC_CO5300_SAFE_BOTTOM >= RISC_CO5300_HEIGHT || RISC_CO5300_SAFE_LEFT + RISC_CO5300_SAFE_RIGHT >= RISC_CO5300_WIDTH
#error "CO5300 safe insets must leave a nonempty display area"
#endif
