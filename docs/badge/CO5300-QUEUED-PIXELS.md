# Opt-in queued CO5300 pixels

Generic `RISC_CO5300_QUEUED_PIXELS=0` and stripe2 remain unchanged. The complete-frame Badge prototype can explicitly select `--queued-pixels --native-stripe-rows 16`; absent arguments preserve synchronous stripe4. Supported prototype stripe choices are4/8/16/32. Build proof verifies exact single definitions (rejecting duplicate, contradictory and undefine flags), alongside existing performance/reuse selection.

## Ownership and completion

One stable transaction descriptor and two internal DMA staging buffers are used. Address commands stay synchronous. The previous pixel result is reaped before changing the panel address window; only the other buffer can be packed while a queued transfer owns its source. Last panel row drains the final pixel before returning, so existing facade COMPLETE/input authority remains valid without a backend ABI change. Brightness and close also drain; first-frame brightness stays dark until a whole checked frame.

Pinned IDF `fff9895c82d744c7237be8847347bdd1b07c6643`: `spi_device_get_trans_result` timeout retains descriptor/DMA; successful return identifies exact descriptor; DMA-error INVALID_STATE returns that descriptor with failure flags. Unexpected identity or unknown error latches uncertain custody and prevents cleanup/reuse. Queue rejection is before transfer of ownership: the only post-enqueue return is `spi_bus_lock_bg_request`, whose pinned implementation returns ESP_OK unconditionally. Do not assume this contract for an unreviewed SDK revision.

Shadow bytes update only after matching completion and successful CS release. Timeout stops later row writes; later checked cleanup can reap known completion before release. Unknown completion cannot be retried away. Fill invalidates shadow authority before attempting a drain, even when drain fails. Full sequential-frame composition is required: generic partial-pair clients keep queued mode disabled. This is not an ABI-equivalence claim for callers ending arbitrary partial windows.

## Memory and behavior tradeoffs

At466wide/16rows, single staging is14,912B and double staging29,824B of internal DMA memory, compared with public4-row staging3,728B. Failed double allocation retries single16-row synchronous transport, then8/4/2-row windows (7,456/3,728/1,864B). The selected window is fixed for the open epoch; rotation, final remainder and shadow addressing use that actual size. If even the original two-row allocation fails, startup is rejected with normal retained-resource cleanup; there is no claim of allocation-neutrality or proven device heap headroom. The optional434,312B PSRAM shadow remains independently optional. No new per-pixel Runtime callbacks are added. One bounded PANEL_TRANSPORT startup record reports actual stripe_rows/queued selection; decode phase36 using that actual profile, not merely configured maximum16. Live target heap headroom remains unmeasured.

Larger stripes coarsen exact dirty detection but preserve every output byte. Actual existing Home renderer50ms pairs: default face stripe4 sends12rows/3windows; stripe16 sends16–32rows/1–2windows. An8px picker move sends260rows/65windows at4 versus272rows/17windows at16. Full changed466frame falls117→30windows and351→90SPI transactions. These are renderer bytes/transaction counts, not device timings.

## Timing interpretation

Queued phase35 is **submitted-window through reap processing**: compare, commands, queued interval (including overlapped packing/scheduling), completion and shadow copy. It is neither pure wire time nor CPU busy time and is not directly comparable to the old synchronous phase35 as a pure SPI metric. Phase36 still counts all processed stripes, including unchanged ones; total phase35+phase36 accounts frame elapsed without double counting overlapped packing. Skipped work remains in gap accounting. Trace is disabled by its unchanged profile gate.

An explicit sensitivity model retaining the selected facade deadline loop assumes8us/row processing,50us per transaction setup,40MHzQIO payload and nominal1ms poll waits. Full changed frame: oldcap32/stripe4 sync57.649ms; cap128/stripe4 sync49.649ms; cap128/stripe16 sync34.106ms; queued16 27.652ms. Modeled CPU occupied time drops43.649→8.378ms when queued waiting yields instead of polling. These assumed costs are not measured Xtensa CPU costs or physical FPS; interrupt overhead/real callback contention require device evidence. The architecture removes serial waits/transactions but does not establish40–60FPS.

## Qualification

Actual-source host matrix covers four geometries including180-degree rotation, stripes2/4/8/16/32 and trace on/off, exact complete panel stream, unchanged frames, mixed pending-changed/equal stripes, first-frame brightness, fresh reopen, double-allocation fallback, every smaller window and total OOM, every command/submission/completion failure, every queued-window timeout, completed DMA faults, wrong returned descriptor, unknown completion, and no writes after retention. ASan/UBSan uses explicit leak-check exclusion because terminal custody intentionally retains memory, and a16MiB quarantine to bound host resource use. A prior default-quarantine run exited by SIGKILL without a sanitizer report; its receipt is retained. Existing synchronous/default partial-pair lanes and actual facade/native Light/Deep/rollback lanes remain separate. Target compile is not full native link or physical acceptance.

## Pinned DMA alignment

IDF spi_common.c423–460 consults GDMA's TX internal alignment; gdma.c437–497 initializes it to1 and raises it for weighted arbitration or RX-specific constraints. Selected ESP32-S3 uses AHB GDMAv1 with weighted arbitration unavailable/unselected; the RX adjustment does not apply to TX. Internal DMA buffers therefore meet the selected TX alignment without bounce copies. Selected16-row alternate offset14,912 is also16/32-aligned. Two-row alternate offset may be8mod16 but meets this exact SDK TX contract; smaller-window memory fallback uses only a single16-byte-aligned base. Different SDK/SoC/arbitration configurations require a new alignment audit, not inference from this one.
