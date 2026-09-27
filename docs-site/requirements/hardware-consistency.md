# Hardware Consistency Requirements

Requirements on the host-side check that the flight software's static view of the hardware agrees with the devicetree:
which I2C mux channel each face manager is configured with, and whether the declared flash size covers the partition table.
Rows are added and edited only with `scripts/req.py` (`req.py add --group "Hardware Consistency (HWC)" ...`) and appear in
the [Requirements Matrix](../requirements-matrix.md). Tests live in `scripts/tests/` and link to these IDs with
`@pytest.mark.verifies("HWC-n")`.

## Hardware Consistency (HWC)

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|HWC-1|check_hardware_consistency.py checks that every tmp112/veml6031/drv2605 face manager is configured with the mux channel the devicetree places its device on|Unit Test|Unit|Line HWC-1 mux-channels: N managers checked, M mismatched — S with one indented offender line per mismatch naming the instance, the channel passed and the channel in the devicetree; a --topology-cpp copy that moves one tmp112 or veml6031 manager to another channel raises M by one and gives FAIL and exit 1; a copy that fixes one drv2605 manager lowers M by one; a --main-cpp copy missing the inputs.<field> assignment for a checked manager exits 2; on the current tree the line is well-formed with status OK and exit 0|||
|HWC-2|check_hardware_consistency.py checks that the declared size of &flash0 covers the end of every fixed partition|Unit Test|Unit|Line HWC-2 flash-size: reg R bytes, partitions end E bytes — S with R from the reg size cell (DT_SIZE_M, DT_SIZE_K, hex or decimal) and E the largest offset+size in the partitions block; a --dts copy with DT_SIZE_M(16) gives OK; a copy where a partition ends one byte past R gives FAIL and exit 1; a copy with no partitions block exits 2; the RESULT line is last and its shape matches check_docs.py (ok, warn, skip, fail counts); on the current tree the line is well-formed with status OK and exit 0|||
