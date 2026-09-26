# RiscRTE-Drivers

Independent source, build, test, release, and versioning repository for RiscRTE runtime drivers.

## Migration source

Until cutover is complete, `michaelrolphone-cmyk/T5S3-Reader` is the read-only upstream parity source. This repository must never write to, branch, or otherwise modify T5S3-Reader.

The target state is:
- every canonical RiscRTE driver is maintained here;
- each driver builds independently from sources in this repository;
- CI validates package manifests, ELF ABI/architecture, and release parity;
- releases are versioned per driver;
- `manifest/released-drivers.json` records the released driver inventory;
- ongoing upstream changes are synchronized here until official source cutover.

Migration work is tracked through normal branches and pull requests in this repository.
