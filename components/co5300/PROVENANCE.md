# CO5300 extraction and provenance

The owner selected RiscRTE-Drivers for the reusable implementation and the Badge
board slice. The extraction starts from the preserved, reviewed but unpublished
candidate `0161c20852797f8e587640a53c4e8f3094e324d6` in
RiscRTE-Wavshare-S3-Touch-AMOLED-2.06, whose published parent was
`098b1bd432be76a87e887509db84bf905de76013`. This does not publish or modify that
product or claim it now consumes this component.

`source-map.json` records original source blob and SHA-256 identities, exact
relocation paths and adaptations. These are provenance receipts, not a new
admission system or exemption from existing source/release parity checks.
The original component qualification remains historical evidence; qualification
of this extracted tree is separate and must use its actual source bytes.

## Source boundaries and adaptations

- The existing loadable facade is reused, with its independent source-only
  identity `co5300-display` 0.1.0 instead of the product identity `waveshare-panel`.
  Its framebuffer, lifecycle, sleep/reset custody and shared Watch ABI are retained.
- The native IDF6 implementation is reused without the unrelated Arduino fallback.
  The small wrapper exports the same Runtime hook; a retained link anchor has a
  component-neutral name. No code is moved into generic Runtime.
- The profile header and board profile retain the tested geometry, wire protocol
  and checked init contract. The board profile gains source attribution comments.
- Existing support headers are copied exactly. `RiscDisplayBackendV1.h`,
  `RiscDisplaySpiV2.h` and `RiscI2cFunctionV1.h` are byte-identical to the pinned
  Runtime descriptors. `RiscPowerRailV1.h` retains the existing product contract;
  it is not presented as a new generic Runtime capability.
- The DCDC1 observer and touch-only startup adapter retain their source behavior.
  They use existing preparation and CST9217 sources from this Drivers tree.
- Tests and staging/build scripts are adapted to this repository layout. The old
  product's consolidation, qualification extractors and release fixtures are not
  imported. No historical inventory, SDK, release byte or guard is altered.

## Primary protocol and license sources

- [Waveshare 2.06 reference bundle](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06/tree/b099739ad0e33b34e5fbaae77f02bd84805d79a3/examples/arduino/libraries/Arduino_GFX)
  and [its root license](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06/blob/b099739ad0e33b34e5fbaae77f02bd84805d79a3/LICENSE).
  All six reference hashes in the native provenance file were checked against
  the exact vendor checkout. This is protocol provenance, not a build dependency.
- [Arduino_GFX v1.6.0 BSD license](https://github.com/moononournation/Arduino_GFX/blob/e7f29f12b247015c0cf99f59a525e30cd1b1dab1/license.txt).
  Upstream blob `7492e93a134119acae08098a6f7fe1e81d1b3122` is retained verbatim.
  The vendored `Arduino_ESP32QSPI.h` and `Arduino_TFT.cpp` match that upstream
  revision exactly; other vendor files differ, and `Arduino_OLED.cpp` is absent
  upstream at that tag. The bundle is not claimed to be an unchanged upstream copy.
- [Waveshare 1.75C BSP init](https://github.com/waveshareteam/Waveshare-ESP32-components/blob/010c5fb39ea7f0e996efd1f8e162cc70e569e57f/bsp/esp32_s3_touch_amoled_1_75c/esp32_s3_touch_amoled_1_75c.c)
  SHA-256 `a2bc2256ffd18beddf6b651cdfc1a9f82350f9c24073feea641e253179bff4c8`,
  [pin definitions](https://github.com/waveshareteam/Waveshare-ESP32-components/blob/010c5fb39ea7f0e996efd1f8e162cc70e569e57f/bsp/esp32_s3_touch_amoled_1_75c/include/bsp/esp32_s3_touch_amoled_1_75c.h)
  SHA-256 `c99b2489c052b82c12e2d3c38d38313caf74947c35dd4abc125365e1de14ac09`,
  and [component Apache license](https://github.com/waveshareteam/Waveshare-ESP32-components/blob/010c5fb39ea7f0e996efd1f8e162cc70e569e57f/bsp/esp32_s3_touch_amoled_1_75c/LICENSE).
- [Espressif CO5300 SPI implementation](https://github.com/espressif/esp-iot-solution/blob/2d289eb1f14431e345b345c3fd6fc299a555d770/components/display/lcd/esp_lcd_co5300/esp_lcd_co5300_spi.c)
  SHA-256 `823bda75000ebe4236ef06911cf26fc873e6507c8cc5de17a5104d89d19a9c34`,
  and [component Apache license](https://github.com/espressif/esp-iot-solution/blob/2d289eb1f14431e345b345c3fd6fc299a555d770/components/display/lcd/esp_lcd_co5300/license.txt).
  Retained attribution: 2023 Espressif Systems (Shanghai) CO LTD.

The two component Apache license files have the same Git blob
`d645695673349e3947e8e5ae42332d0ac3164cd7` and SHA-256
`cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`.
One retained copy suffices. No relevant separate NOTICE file was found in these
checked component roots. The previously recorded Apache-only bundle notice was
incomplete; the upstream BSD notice is now retained as well. No repository-wide
license is inferred or assigned to the owner's RiscRTE code.

The supply observer uses datasheet register facts, not copied XPowersLib code.
Its formula and rail ownership references are in the board's DISPLAY.md.
Neither primary source agreement nor software tests measure voltage under load,
prove physical orientation or qualify installable firmware.
