#pragma once

/* Adapted from Waveshare BSP and Espressif CO5300 Apache-2.0 references.
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 * Source-specific notices/licenses: components/co5300/THIRD_PARTY_NOTICES.md.
 * This adaptation changes brightness to defer illumination until a checked frame.
 */

/* Compile the provider and native backend with this same selected profile.
 * Vendor-native reference: Waveshare BSP 010c5fb and ESP-IOT 2d289eb.
 * Retain the BSP's orientation/gap, not later application mirror/gap changes.
 */
#define RISC_CO5300_WIDTH 466
#define RISC_CO5300_HEIGHT 466
#define RISC_CO5300_OFFSET_X 6
#define RISC_CO5300_OFFSET_Y 0
#define RISC_CO5300_RESET_LOW_MS 10
#define RISC_CO5300_RESET_RECOVERY_MS 150
#define RISC_CO5300_PIXEL_WRITE_ADDRESS 0x002c00
#define RISC_CO5300_RAMWR_BEFORE_PIXELS 0

/* Conservative software bring-up insets for the round panel, not measured
 * hardware bounds. The default rectangular 2.06 profile keeps zero insets.
 */
#define RISC_CO5300_SAFE_TOP 69
#define RISC_CO5300_SAFE_RIGHT 69
#define RISC_CO5300_SAFE_BOTTOM 69
#define RISC_CO5300_SAFE_LEFT 69

/* Deliberate safety deviation: normal brightness 0x51 is 0x00, not the BSP's
 * 0xff. Only a complete, checked application frame may restore brightness.
 * Both the panel-init DISPON and the subsequent BSP DISPON are retained.
 */
#define RISC_CO5300_INIT_COMMANDS(X) \
 X(0x36,1,0x00,0,0,0,0) \
 X(0x3a,1,0x55,0,0,0,0) \
 X(0xfe,1,0x20,0,0,0,0) \
 X(0x19,1,0x10,0,0,0,0) \
 X(0x1c,1,0xa0,0,0,0,0) \
 X(0xfe,1,0x00,0,0,0,0) \
 X(0xc4,1,0x80,0,0,0,0) \
 X(0x3a,1,0x55,0,0,0,0) \
 X(0x35,1,0x00,0,0,0,0) \
 X(0x53,1,0x20,0,0,0,0) \
 X(0x51,1,0x00,0,0,0,0) \
 X(0x63,1,0xff,0,0,0,0) \
 X(0x2a,4,(RISC_CO5300_OFFSET_X>>8),(RISC_CO5300_OFFSET_X&0xff),((RISC_CO5300_OFFSET_X+RISC_CO5300_WIDTH-1)>>8),((RISC_CO5300_OFFSET_X+RISC_CO5300_WIDTH-1)&0xff),0) \
 X(0x2b,4,(RISC_CO5300_OFFSET_Y>>8),(RISC_CO5300_OFFSET_Y&0xff),((RISC_CO5300_OFFSET_Y+RISC_CO5300_HEIGHT-1)>>8),((RISC_CO5300_OFFSET_Y+RISC_CO5300_HEIGHT-1)&0xff),600) \
 X(0x11,0,0,0,0,0,600) \
 X(0x29,0,0,0,0,0,0) \
 X(0x29,0,0,0,0,0,0)

#include "RiscCo5300Profile.h"
