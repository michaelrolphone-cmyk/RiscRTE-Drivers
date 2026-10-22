# USB serial monitor: source qualification and implementation continuation

## Result

Twenty targeted host checks pass normally and with ASan/UBSan (40 successful
invocations). Existing serial protocol drivers and the historical Serial Monitor
are reusable. They are not a connected native X4 monitor candidate. No production
source, SDK, package manifest, product selection or accepted byte changed.

The important new result is an executable compatibility check: all four current
Drivers class libraries are skipped by the actual historical installed-serial
inventory, publish zero device records, and lack provider stream endpoints.
Their own protocol fixtures passing does not establish compatibility with that
consumer, let alone native X4. This is a source-composition gap, not a measured
physical USB failure.

## Exact source identities

- Drivers main: `a7ae39994603c4545a3c014f09051ecfc12084d2`.
- X4 main: `6b7d1abe9b2dcc236f6086d07c0f619244c907cb`.
- Historical migration Reader: `34d8e694d89a1e72d8854403d8592c289fae3ddc`,
  tree `e7dc2a45c3553f0102a7c2e2c2c9fe5bc51b00a2`.
- Native 0.1.123 Runtime: `6a3b3d46f5b2857eacc6e9863799dec1f466b036`.

Both named main heads were checked read-only during this pass. Historical Reader
and native Runtime were fetched at their exact source pins into isolated clean
checkouts. X4 README explicitly separates the historical migration build from
its native candidate; migration-source.json alone is not the native composition.
[Machine-readable evidence](evidence/usb-serial-monitor-20261021/source-composition.json)
records commits, trees, selected blobs/lengths/hashes, exact native inputs and the
recorded native file inventory. Those committed inventory receipts were inspected;
this pass did not extract the factory image or establish missing historical
composition/build provenance.

## Reuse inventory and mismatches

Drivers provides CDC `usb-cdc-acm-v2` 0.1.0, CH34x 0.1.0, CP210x 0.1.0 and FTDI
0.1.0. Their ABI exposes raw open/configure/control/read/write/close. Current
Drivers' selected serial source/manifest/header bytes are unchanged from native
0.1.123's Drivers pin `5890721073ebabd781d5d278e447c761947a5c58`.

Historical Reader has Serial Monitor 1.2.7, canonical CDC `usb-cdc-acm` 0.1.8,
CH34x 0.1.7 and CP210x 0.1.8. These three class drivers add checked claim cleanup,
provider-originated inventory and provider-owned streams. They require the
matching host discovery/claim-control/release API, not only the old base host
prefix. CDC's historical canonical identity must not be confused with the old
`usb-cdc-acm-v2` fork. FTDI 0.1.0 source and its fixture remain identical across
these two snapshots and lack those extensions.

The new `legacy_inventory_probe.cpp` loads actual host-built Drivers libraries,
then calls Reader's real InstalledSerialInventory and InstalledSerialSession
with graph-acquisition/release stubs. It never starts drivers or acquires a real
USB device. All four are safely skipped and expose no endpoints. This confirms
why copying only old class ELFs is not a monitor integration.

Native .123's locked System `0651a55513b0aa0dff1b6a9072303def08a97378` has no
serial-named client source. Its Runtime includes a synthetic serial witness
integration script requiring `lib/PortableApps/src/PortableSerialClient.c` from a
matching System checkout; that file is absent in this locked System. No synthetic
witness is an installed transport or production monitor.

## Native product boundary

X4 .123 source-inputs also pins Reader `c98e4222048244b1e729869ed47aa99130025fd3`
and Utilities `e80172353fcb9e6519ca9d9b2a4d43c69c1bc91d`; these are distinct from
the historical migration Reader above. Its documented replacement changes only
six baseline internal files (Home pair, catalog-service pair, boot.json, cohort).

The committed .122 baseline records 107 internal files and 43 ELFs, with BLE
serial and USB device MSC/SD-transfer entries but no selected USB host serial
stack or Serial Monitor. Its .123 generic RTEF1 serial feature is file transport
between X4 and a computer, not X4 monitoring an attached USB serial peripheral.
The historical nine-provider X4 profile also selects no USB host chain.

No verified X4 `board.power.vbus` root exists in these inputs. GPIO21 is charging
state only; the board documentation explicitly says VBUS detection is not
conclusively identified. GPIO1 peripheral readiness is not host VBUS authority.
This blocks device admission, not independent software development. Do not copy
T5S3 BQ25896 power wiring onto X4 or change accepted pins to make tests pass.

## Concrete source-only continuation

The native session and serial request ABIs already exist. A first reuse increment could synchronize the
historical CDC/CP210x/CH34x class inventory, checked cleanup and SerialStreamPump,
plus their matching usb-host discovery/claim-control/release contracts, into an
isolated Drivers candidate with truthful package identities/versions and exact
upstream provenance. Run the existing class, host/class, inventory and stream
fixtures before adding a native session adapter. Preserve published old bytes.

The deeper deadline check below shows why this cannot yet be advertised as a
CDC-only native port. It does not establish that the lower transport can meet
the contract without separate host/controller changes, nor authorize a new ABI.

It is not a mechanical direct drop-in to native Runtime:

1. Reader drivers export `risc_driver_poll_v2`. Native Runtime already supports
   `risc_driver_stream_sessions_v2` with open/call/close callbacks; the concrete
   serial request ABI already exists in RiscSerialStreamSessionV1.h.
2. That callback contract requires a total deadline of at most 1000 ms across
   all lower operations. Historical class requests use fixed 1000 ms per control
   request; open/configure may issue multiple requests. The legacy host claim and
   checked-release callbacks have no supplied deadline. A wrapper must establish
   a real budget/time source and bound lower work, not just pass the same timeout
   repeatedly or declare the old calls compliant.
3. Native close must stop pumping and revoke use while retaining uncertain
   physical custody and queues until checked close. Historical close_endpoints
   destroys queues before the physical close, so adapter teardown needs deliberate
   retention and failure tests, not blind source copying.
4. Recover or implement a matching portable consumer using the existing native
   stream-session interface, then connect a monitor UI using existing behavior.
   The current locked System is not the matching client input for Runtime's test.

Source development can use fake host/clock/queue boundaries for deadlines,
failed open, failed physical close, detach/reconnect, stale generations,
backpressure and retained cleanup. No VBUS assumption or product-pin change is
needed. A real-controller candidate still separately requires verified X4 power
and role custody, exact package selection and later authorized hardware checks.
FTDI needs its own inventory/stream/session adaptation; protocol-only parity is
not evidence it already has those features.

## Native-slice isolation decision

The requested smallest coherent slice is not only a CDC class plus a thin native
adapter. Current Drivers `usb_host_v2/driver.c:238-250` exposes a void release;
failed physical release is hidden in its retained closing claim. It does not
expose the checked-release/claim-control suffix required by historical CDC.
Preserving this host unchanged cannot give a native adapter proof of closure.

The controller also cannot currently promise the native total callback budget:

- `usb_controller_esp32s3/driver_base.cpp:123-156` drains a timed-out bulk
  transfer with a separate fixed 500 ms loop. It deliberately retains DMA if
  callback ownership has not returned.
- `wait_completion` at lines 158-172 first consumes its supplied timeout and
  then calls that drain. Passing a remaining-time value is not sufficient.
- `release_interface` at lines 301-343 calls the same drain but receives no
  deadline argument. A platform-clock check after returning cannot retroactively
  bound this work. The controller's cached configuration-copy path is not the
  blocking problem identified here.

The new `test/usb_serial_native_deadline_probe.py` compiles the actual unchanged
controller pump, drain, wait and release function bodies against deterministic
fake IDF callbacks, with one fake tick per millisecond. Its outcomes are:

1. Requested bulk timeout 1 ms, observed fake elapsed time 501 ms. DMA remains
   live, and no interface release occurs.
2. Checked physical release with a callback that never arrives takes 500 fake
   ms, returns false, and retains the exact claim. It has no caller-budget input.
3. Callback arrival at fake time 3 ms permits successful physical release, but
   never before DMA custody returns.
4. Endpoint-zero timeout preserves the in-flight control transfer and does not
   pretend the bulk cancellation path can release it.

Both plain and ASan/UBSan runs pass this prerequisite reproduction. This is a
counterexample to a naive native wrapper, **not** a native integration pass or
an electrical timing measurement. IDF calls are fake and instantaneous except
the explicit event-pump ticks; these results are not worst-case hardware bounds.
The script records full source and extracted-body SHA-256 hashes. See
[plain evidence](evidence/usb-serial-monitor-20261021/native-deadline-plain.log)
and [sanitized evidence](evidence/usb-serial-monitor-20261021/native-deadline-sanitized.log).

The real lower-source audit makes the deadline problem stronger than the fake
500 ms reproduction. Drivers' IDF 6.1 builder pins esp-usb commit
`0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a`. Before the drain timer even begins,
endpoint halt follows [usb_host.c:1892-1902](https://github.com/espressif/esp-usb/blob/0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a/host/usb/src/usb_host.c#L1892-L1902),
[usbh.c:1790-1796](https://github.com/espressif/esp-usb/blob/0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a/host/usb/src/usbh.c#L1790-L1796),
and [hcd_dwc.c:2020-2036](https://github.com/espressif/esp-usb/blob/0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a/host/usb/src/hcd_dwc.c#L2020-L2036)
to the internal event wait at
[hcd_dwc.c:840-853](https://github.com/espressif/esp-usb/blob/0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a/host/usb/src/hcd_dwc.c#L840-L853),
which uses `ulTaskNotifyTake` with `portMAX_DELAY`. The existing
`scripts/native_usb_idf61_sources.py` adaptation changes IRQ/port lifecycle, not
this halt path. Reserving a nominal 500 ms in CDC is therefore not a solution to
the real total deadline. This is pinned source analysis, not an executed target
test or a measured hardware hang.

The smallest coherent implementation boundary therefore includes a separately
scoped host/controller prerequisite: expose the existing checked custody
contract and establish genuinely bounded lower transfer/cleanup behavior. Its
solution may reuse or refine existing contracts; this pass neither invents nor
approves a new ABI. After that prerequisite, a single CDC candidate needs the
existing native stream-session suffix, total-budget propagation, retained queues
on uncertain or partial close, and a matching portable client. Other serial
providers, product pins and accepted historical bytes must remain unchanged.
The matching Runtime test then provides 22 synthetic client cases (11 modes
times eager/demand activation); those are not executable with the locked System
source because its required PortableSerialClient.c is absent.

No partial production port was created that would violate its advertised
deadline or resource-lifetime contract. This source-level prerequisite is
independent of the separate unresolved X4 VBUS/device-admission boundary.

```sh
python3 test/usb_serial_native_deadline_probe.py
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 python3 test/usb_serial_native_deadline_probe.py
```

## Reproduce targeted qualification

With the exact historical Reader checkout supplied separately:

```sh
bash test/run_usb_serial_source_qualification.sh /path/to/reader plain /tmp/serial-plain
bash test/run_usb_serial_source_qualification.sh /path/to/reader sanitized /tmp/serial-sanitized
bash -n test/run_usb_serial_source_qualification.sh
git diff --check
```

Each output directory must be new. The runner uses C11/C++17,
`-Wall -Wextra -Werror`; sanitized mode adds ASan/UBSan and disables only
LeakSanitizer. [Plain log](evidence/usb-serial-monitor-20261021/qualified-plain.log)
and [sanitized log](evidence/usb-serial-monitor-20261021/qualified-sanitized.log)
cover:

- Four class fixtures in each source composition, plus old CDC descriptor logic.
- Monitor RX/TX/reconnect, absent-device retry, baud-change fallback and launch UI.
- Provider inventory/resolver/diagnostics, partial-acquire quarantine, exact
  installed session and installed bridge lifecycle.
- Actual old class rejection by the historical consumer (four class cases).

These are 20 invocations per mode, not 20 independent protocol implementations:
the FTDI body is identical across the two compositions. Source launch checking
is distinct from runtime UI behavior; mocked boundaries are not end-to-end USB.

An exploratory aggregate CDC script stopped in nested LeakSanitizer because this
executor is ptraced. A second aggregate attempt still reached a nested script
that force-enabled LSan. Both logs are retained, and neither aggregate pass is
claimed. The targeted runner preserves ASan/UBSan without LSan. Its first draft
ran the source launch fixture from the wrong working directory; the runner was
corrected to Reader's root and both complete modes then passed. The failed draft
logs are retained as `final-plain.log` / `final-sanitized.log` (not final evidence).

No Xtensa compiler was available on the inspected PATH; target/package builds,
byte parity, full native integration, full CI, device enumeration, USB role/VBUS,
physical serial transfer and product acceptance were not performed or inferred.
No push, PR, merge, release, catalog update, device/USB/power action or frozen-byte
replacement occurred. Source work remains unfinished and can continue independently;
this evidence checkpoint alone does not complete the monitor outcome.
