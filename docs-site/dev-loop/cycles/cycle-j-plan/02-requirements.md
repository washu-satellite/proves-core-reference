# 02 — Requirement rows `HWC-1..2` (Unit / Unit Test)

`docs-site/requirements/tooling.md` is hash-pinned by Cycle I's review and must not change, so these rows live in a new file.
`req.py add` cannot create a group (`scripts/req.py:125-126`, `:273-278`), so the group is seeded by hand with its header and
first row, in the tool's exact column format; the second row goes in with the tool. Both `req.py` (`:134`) and
`generate_rtm.py` (`:140-142`) pick up every `*.md` under `docs-site/requirements/` and every `## ` table in it.

## Step 1 — seed (create `docs-site/requirements/hardware-consistency.md` with exactly this content)
```markdown
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
```
Check: `$PY scripts/req.py show HWC-1` prints the row with group `Hardware Consistency (HWC)`.

## Step 2 — the second row, verbatim
```
$PY scripts/req.py add --group "Hardware Consistency (HWC)" --id HWC-2 --method "Unit Test" --level Unit \
  --description "check_hardware_consistency.py checks that the declared size of &flash0 covers the end of every fixed partition" \
  --criteria "Line HWC-2 flash-size: reg R bytes, partitions end E bytes — S with R from the reg size cell (DT_SIZE_M, DT_SIZE_K, hex or decimal) and E the largest offset+size in the partitions block; a --dts copy with DT_SIZE_M(16) gives OK; a copy where a partition ends one byte past R gives FAIL and exit 1; a copy with no partitions block exits 2; the RESULT line is last and its shape matches check_docs.py (ok, warn, skip, fail counts); on the current tree the line is well-formed with status OK and exit 0"
```
Check: `$PY scripts/req.py show HWC-2`; `$PY scripts/req.py list` shows the group with two rows.
