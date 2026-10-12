# Independent generic capabilities

Capabilities describe functions; chip and product identity belong to providers and board configuration. `board.battery` remains the existing generic gauge contract. `power.sleep`, `input.power-key` and `display.power` are independent generic interfaces. They are not watch-specific APIs and can be implemented on different hardware without changing the client.

The three new public headers in `sdk/driver` are shared with the Watch SDK snapshot. CI compares the copies byte-for-byte. They carry no pin numbers, chip names, PMIC registers or private battery/framebuffer prefix. App clients validate the public interface's version and extent, and use its declared context.

## Transitional providers

`legacy_power_sleep`, `legacy_power_key` and `legacy_display_power` translate the deployed combined driver tables to the independent interfaces. Their private `LegacyPowerAbiV1.h` is not an application contract. These adapters can be used for the inherited T-Watch and initial Waveshare providers; applications do not know which implementation supplies the capability.

The adapters are deliberately selected only for providers with the known old layout. A base-only battery gauge is valid for `board.battery` but cannot supply a compatibility power adapter. Adapter startup rejects it without reading past the published extent. No function is invented for an absent optional suffix. Fresh hardware implementations can provide the new generic interface directly and need not imitate an old battery or display table.

Each adapter has one Runtime dependency, owns no physical resource and installs no callbacks/tasks. The normal graph retains its physical dependency for the adapter's lifetime. Real physical cleanup/refusal still occurs in that dependency. This is not a replacement for driver lifecycle handling.

## Integration

A device build stages the three adapter ELFs and manifests, adds distinct provider instances and grants `power.sleep`, `input.power-key` and `display.power` to Home/default and the return-clock role. The physical battery/display instances remain selected by the board's normal dependency resolution. A board with multiple outputs or power domains needs explicit selection; Home does not embed those physical instance IDs.

These are component artifacts, not flashable images or a completed board migration. The app/source-manifest requirements and per-device grants must be assembled together before generating paired firmware metadata. The existing 30-provider Watch image would need capacity for 33 providers after adding these adapters; do not omit existing providers merely to fit the old limit.

Test coverage includes exact-size base-only rejection, optional suffix extents, different contexts, API versions, invalid dependency identity, retained return propagation and restart. CI builds all three ESP32-S3 shared ELFs and validates their loader format/imports/exports. Physical device behavior and complete product image admission remain separate integration tests.
