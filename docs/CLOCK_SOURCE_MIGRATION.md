# Clock source qualification and historical replay

`build_platform_clock_v1.py` remains the single clock builder. With no arguments,
it builds historical 0.1.0 source from Git tree594a3affe76fb840c81edc7fa9f09eca645a894f
(the f208 source), preserving the original output directory and compiler selection.
This is historical *source* compatibility, not automatically accepted-byte parity.

`--current-source` qualifies the working 0.1.1 source in
`dist/platform-clock-v1-current`. Its added assertions concern the ordinary
loadable ELF's exported clock_gettime time32 contract, not native ESP-IDF time
layouts. The existing SDK overlay remains ordinary-ELF-only. No runtime clock
algorithm, capability API or native firmware/provider time representation changes.

`--require-byte-parity` is historical-only. It selects the two exact SDK headers
from historical producer a1982161c4e584b1fa047030fde60836980694fb, checks their
hashes and the unchanged normalizer, then requires accepted ELF size29544 and
SHA256a5231d3f21931261b10ae82a3e16248d60a678aa6ebc67b61cb702491d6a0ece.
This lock comes from Reader release-index19bb3dcb5cb763c4eacd884ad5fc0bcab57ff2ed,
not from the newly compiled artifact. Native GCC15 can differ; it must not be
relabeled as accepted-byte replay. CI separately uses the existing pinned GCC8
installation for this check and the existing native compiler for current source.

`--output` selects a qualification directory. Current mode rejects the historical
output directory and descendants, including aliases resolved to that location.
Other explicitly supplied directory aliases resolve normally. All builds occur
in temporary staging. Before copying, any differing retained file, symlink file,
or file/directory collision is rejected; no accepted output is overwritten.
Historical mismatch prints a CLOCK_BUILD_FAILURE JSON receipt to stderr before
staging cleanup, retaining actual/expected bytes and source/compiler provenance.

Success emits hash-named run provenance including source, selected headers,
helper files, compiler binary/version, libgcc, readelf and command. Temporary
paths vary; this is a run receipt, not a deterministic build identity. Implicit
compiler subprocess/system-library closure is not fully enumerated. Existing
export/ELF validation remains. Hardware/runtime qualification is not implied.

Neither source/release inventory baseline nor historical accepted artifacts are
refreshed by this change. Current source intentionally changes the upstream
comment to avoid inaccurately characterizing all firmware as IDF4; exact tree
comparison may consequently report a documented local variant, not source equality.
