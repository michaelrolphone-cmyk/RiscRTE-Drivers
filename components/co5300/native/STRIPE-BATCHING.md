# Configurable owned CO5300 stripes

`RISC_CO5300_STRIPE_ROWS` is a native transport batching selection, default **2**.
It must be even, at least2, no larger than the panel height, and fit the SDK's
32-bit transaction bit-length. The selected buffer is one fixed-size owned internal
DMA allocation; allocation failure uses the existing checked cleanup path. There
is no second queued buffer, caller-buffer borrowing, asynchronous DMA overlap or
change to the40MHz bus clock. Existing profiles remain unchanged until explicitly
selecting a larger stripe in their build profile.

## Supported contract

The pinned Runtime `sdk/driver/RiscDisplayBackendV1.h` describes a provider-only
synchronous backend but does not promise per-row visibility. Its existing native
implementation already accepts/copies the first row without submitting it. True
means that row was accepted; transfer errors are reported when the selected stripe
is submitted. The ordinary CO5300 facade submits sequential complete frames both
in its poll path and its sleep-resume restoration path. These consumers support
larger bounded stripes without changing their application completion contract.
Default2 retains the prior pair behavior, including isolated arbitrary even-start
pairs. Larger selections accept even-start partial stripes but may hold rows until
the stripe completes; they do not promise historical two-row visibility timing.
Closing an incomplete stripe cancels its buffered work without claiming a complete
frame, as the original backend did for an unmatched first row.

The SDK `components/esp_driver_spi/include/driver/spi_common.h` defines
`max_transfer_sz` in bytes (4092 is a DMA default, not a fixed universal maximum).
The bridge sets it explicitly to its owned stripe capacity. SDK `spi_master.c`
checks transaction bit length against that bus capacity. Each submitted controller
window is exactly the accepted rows; inclusive start/end parity remains even/odd.
For180° rotation, both rows and columns reverse within the *actual* stripe length,
including the final shorter stripe. Commands, color order, controller offsets,
brightness gating, owner checks and retained transfer cleanup are unchanged.

## Badge selection and source-proven counts

At466×466, selecting4 rows uses3,728 bytes rather than1,864 bytes. The last stripe
contains2 rows/1,864 bytes. There are117 windows rather than233:351 synchronous
SDK transfers instead of699 for this profile's two address commands plus pixels.
Pixel payload remains434,312 bytes. Manual CS transitions fall1,398→702. Four rows
fit the usual4,092-byte DMA descriptor payload. This reduces transaction/setup
work; it does not reduce payload or establish a physical elapsed/FPS improvement.
The observed115ms transfer duration also contains other costs and scheduling gaps;
it must not be divided by two and presented as a predicted measured result.

## Owning qualification

`tests/test_co5300_stripes.py --commands DIR --output OUT` reuses actual-SDK command
receipts from `tests/test_co5300_native.py`. It compiles the actual bridge for
2/4/6 rows across default502-high, Badge466-high, independent offset and rotated
profiles, with trace on/off. Default2 runs the original full lifecycle suite.
Larger stripe lanes check flattened frame/window/pixel coverage, final remainders,
partial even-start stripes, caller mutation after acceptance, invalid order and
owner, dark/full-frame brightness gating, setup failures, timeout at each window
transaction, final-stripe timeout, completed DMA error and irreversible unknown
custody. Existing bounded performance aggregates still count rows/windows and
preserve total=work+gap accounting. `--sanitize` adds ASan/UBSan (leak detection is
disabled in this executor); `--target` compiles against real target declarations.
No device command or physical validation is performed by these tests.

## Selected prototype integration

The Badge prototype native product profile selects four rows. The generic
component and non-prototype board profile retain two. This macro controls only
the native owned DMA stripe; it adds no provider ABI field or framebuffer layout.
The selected CO5300 facade still presents complete frames, including partial
damage requests and power-resume reconstruction. Its COMPLETE boundary follows
the final row, which flushes the short remainder. Sleep refuses active frames;
cancellation/failure never reports a pending stripe as completed.

This selection is composition-bound, not a universal partial-pair compatibility
claim. New raw-row consumers must retain two rows or establish explicit flush
semantics. Exact packaged boot graph/provider checks and target native compile
commands are required before this successor is qualified.

At40 MHz QIO, payload ideal is434312*8/(40000000*4)=21.7156ms. The pinned
ESP-IDF spi_master.c maps SPI_TRANS_MODE_QIO to four data lines. This excludes
commands, gaps, scheduling and controller overhead, and is not measured device
throughput. A mistaken single-lane86.86ms estimate was corrected before any
release evidence used it.
