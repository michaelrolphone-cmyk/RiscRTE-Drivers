# CO5300 protocol and board-profile references

The native CO5300 implementation and its default profile follow the register
sequence and QSPI protocol in Waveshare ESP32-S3-Touch-AMOLED-2.06 revision
`b099739ad0e33b34e5fbaae77f02bd84805d79a3`, whose bundled Arduino_GFX identifies
itself as version 1.6.0. Exact reference paths and hashes are recorded in
`native/co5300-native-provenance.json`.

Arduino_GFX is maintained by Moon On Our Nation. Its upstream v1.6.0 license
contains: Copyright (c) 2012 Adafruit Industries. All rights reserved.
The complete BSD license is retained in `licenses/Arduino_GFX-BSD.txt`.
The Waveshare bundle also contains an Apache-2.0 repository license, retained
in `licenses/Apache-2.0.txt`. These notices do not relicense the entire component.

The 1.75C board profile adapts the Waveshare ESP32 components BSP at revision
`010c5fb39ea7f0e996efd1f8e162cc70e569e57f` and references the Espressif CO5300
SPI component at revision `2d289eb1f14431e345b345c3fd6fc299a555d770`.
Both components provide Apache-2.0 license texts. Espressif source attribution:

SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD

SPDX-License-Identifier: Apache-2.0

Adaptation changes include checked ESP-IDF transport and cleanup custody,
shared compile-time profiles, deferred brightness until a complete checked
frame, and the 1.75C BSP initialization table with brightness changed from
0xff to 0x00. No Arduino_GFX library is compiled into the native backend.

The RiscRTE facade, lifecycle code, local support headers and native adaptation
were relocated from the preserved, unpublished RiscRTE-Wavshare-S3-Touch-AMOLED-2.06
candidate `0161c20852797f8e587640a53c4e8f3094e324d6`. See `PROVENANCE.md` and
`source-map.json` for exact source paths, identities and primary source links.
