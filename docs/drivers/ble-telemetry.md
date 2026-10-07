# ble-telemetry 0.1.0

Generic experimental `bluetooth.telemetry@1` provider. Available fields and every
value come solely from its explicitly bound `sensor.telemetry@1` dependency.
It does not look up boards, GPIOs, registers, runtime health or hardware tables.
A deployment supplies one source, which may aggregate its own authorized
capabilities. The supplied battery adapter is one small usable source.

## Explicit sharing

Activation, enumeration and reading perform no RF operations. `publish` requires
an explicit nonempty selection of up to 16 field IDs and `public_broadcast=true`.
The caller must obtain approval for those readings and explain that nearby
receivers can read them without pairing. Do not auto-start publication because a
provider is installed. The API has no default “share everything” path.

This increment emits nonconnectable open BTHome v2 advertisements. It does not
claim confidential sharing, encrypted GATT, pairing or peer authorization.
Sensitive sources must not be supplied to an open advertisement without the
appropriate explicit data-sharing approval. No real radio was used in software
testing. The controller chooses entropy through HCI LE Rand; each session uses a
fresh random-static address. There is no local name, public MAC, sensor label or
field ID on air. Rotating that session address does not make readings untrackable.
Receivers' address-based aliases therefore do not follow a new session address.

## Capability and wire contract

`RiscTelemetryV1.h` defines copied descriptors, stable source IDs and seven scalar
metrics in explicit integer units. `enumerate` must prove end-of-list within 16
fields, with unique nonzero IDs and bounded labels. `read` distinguishes current,
unavailable and failed values. The driver preserves that distinction.

Only selected IDs are sampled. All selected readings must be current and valid;
unknown types, disappeared IDs, changed types, unavailable readings, range errors
or a payload exceeding legacy advertising's 31-byte budget fail rather than
silently truncate. Source order does not dictate BTHome object order. Encoding
sorts objects numerically, preserves equal-object selection order, includes flags
and never copies source labels. Standard battery/temperature/humidity/pressure/
light/voltage/charging fields use [the BTHome format](https://bthome.io/format/).

The same exclusive raw-HCI lease as HID and the passive sensor scanner protects
reset, random-address setup and advertising commands. No second NimBLE, ATT/GATT
or security stack is introduced. There is one acknowledged HCI command at a time,
a two-second whole-transaction progress bound, and a five-second cooperative
refresh. Data is resampled before writing the advertisement. Source/transport
failure attempts immediate controller release; failed release retains ownership
for retry. There is no asynchronous task or callback. Poll regularly; an app that
stops polling cannot receive autonomous freshness or teardown guarantees.

`close=false` and `quiesce=false` retain the provider and dependencies. Failed
`publish` may return a cleanup token. A safe prior-power restore failure is
reported separately. The driver does not override product radio preferences;
callers must obey the existing Bluetooth/Airplane controls before publication.

## Evidence and remaining work

Production host tests verify exact advertising bytes, scoped field selection,
capability-only enumeration/read, zero/unknown distinction, entropy failures,
claim exclusion, refresh, changed/missing fields, timeouts, stale tokens and
retained teardown. Normal and ASan/UBSan paths pass. Target compilation validates
the isolated ABI-v2 ELF and bounded imports. Hardware, phone/Home Assistant
interop, current and advertising timing are unqualified. There is no telemetry
settings application or installation profile in this driver-only increment.

The HCI command layouts follow the Bluetooth Core [Host Controller Interface specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host-controller-interface/host-controller-interface-functional-specification.html), sections 7.8.4–7.8.9 and 7.8.23.
