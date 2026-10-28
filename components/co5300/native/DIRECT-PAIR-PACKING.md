# Direct owned pair packing

The native display packs each accepted RGB565 row directly into its existing internally allocated DMA pair buffer, applying the same byte order and selected 0/180-degree pair reversal. It no longer copies rows into an intermediate PSRAM pair and then rereads them.

For a 466×466 full frame this removes 434,312 PSRAM write bytes and 434,312 PSRAM read bytes. This is an exact operation-count reduction, not a device timing result. Window commands, 233 pair transfers, SPI frequency, synchronous completion, facade poll budget, startup clear and brightness gate remain unchanged.

The first row only fills its assigned half; no transfer occurs until its matching second row. The DMA buffer remains internally owned. Any transfer failure latches the existing failed state; rejected calls cannot mutate outstanding DMA storage. Unknown completion retains the existing irreversible custody state. Tests compare exact packet bytes for all selected profiles and explicitly preserve DMA bytes on timeout/unknown completion and rejected subsequent row calls.

Physical render/transfer measurements for prior build7800 were431ms/115ms on one Home reentry. This optimization addresses transport staging only and is not evidence that overall UI latency is solved.
