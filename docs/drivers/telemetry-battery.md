# telemetry-battery 0.1.0

Experimental `sensor.telemetry@1` adapter requiring only `board.battery@1`.
It exposes three stable channels supported by that capability: battery percent,
voltage in millivolts and charging. It contains no board identity, firmware hook,
register access, radio I/O, cache, persistence or fabricated readings.

A percent outside 0..100, including the capability's unknown 255 sentinel, is
unavailable. Zero percent is a valid measured value. Zero voltage is unavailable.
A failed gauge read makes the requested value unavailable, including charging.
Each read delegates to the bound gauge. The channel enumeration describes
supported channels, not a promise that every sample is currently measurable.
No temperature channel is invented when the source capability lacks it.

Deployments can bind this source to `ble-telemetry`, or supply another
`sensor.telemetry@1` source for additional sensor metrics. This does not select or
publish readings by itself. Host tests cover lifecycle, unsupported dependencies,
unknown values, zero/range boundaries, units and gauge failure. The isolated
Xtensa ELF builds with the same checks as the BLE providers. Physical gauge and
BLE behavior are untested.
