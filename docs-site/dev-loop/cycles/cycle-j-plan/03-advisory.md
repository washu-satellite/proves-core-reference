# 03 — Advisory: design and method (the coder may deviate if every result in 01/02 holds)

## 1. Module layout — a separate script, importing what `check_capacity.py` already has
Same reasoning as Cycle H §1: `check_capacity.py` audits table sizes; devicetree↔topology agreement is a different job with a
different line grammar, so it gets its own file, `scripts/check_hardware_consistency.py`. Reuse by import, as
`check_capacity.py` imports `check_packet_set` (`scripts/check_capacity.py:56-59`, `sys.path` insert + `# noqa: E402`):
`DASH`, `InputError` (with its `.message()`), `read_text` from `check_capacity`. Do not import `Line` — its `used/limit`
render is the wrong shape; a small local dataclass per rule with a `render()` is enough. `main()` mirrors
`check_capacity.main` (`:675-696`): `InputError` → message to stderr, return 2; otherwise print lines, tally, `RESULT:`,
return `1 if fail else 0`.

## 2. Parsing (regex only)
- Topology configure calls (`ReferenceDeploymentTopology.cpp:160-184`):
  `^\s*((?:tmp112|veml6031|drv2605)Face\d+Manager)\.configure\(\s*state\.tca9548aDevice\s*,\s*state\.muxChannel(\d)Device\s*,\s*state\.(\w+)\s*(?:,[^)]*)?\)\s*;` — the tmp112 form has a fourth `true` argument (`:160`).
- `Main.cpp`: `^\s*inputs\.(\w+)\s*=\s*(\w+)\s*;` (`:85-121`) gives field → C identifier; `^const struct device\*\s+(\w+)\s*=\s*DEVICE_DT_GET\(DT_NODELABEL\((\w+)\)\)\s*;` (`:31-60`) gives C identifier → node label. Today they are the same string, but resolve through both so a renamed variable still checks.
- DTS mux blocks: find each `(mux_channel_(\d+))\s*:\s*i2c_mux@\d+\s*\{` (`v5.dtsi:210, 245, 280, 315, 351, 386, 422, 458`), then brace-match from that `{` (count `{`/`}`; strip `//` and `/* */` comments first — the file has both) to get the block body; inside it, labels are `^\s*(\w+)\s*:\s*\w+@[0-9a-fA-F]+\s*\{`. Build `label → channel`.
- Flash: locate `&flash0\s*\{` (`v5.dtsi:65`), brace-match; inside, `reg\s*=\s*<\s*0x10000000\s+([^>]+?)\s*>` for the size cell, then `DT_SIZE_M\((\d+)\)`, `DT_SIZE_K\((\d+)\)`, `0x[0-9a-fA-F]+` or `\d+`; the `partitions\s*\{` block likewise, and every `reg\s*=\s*<\s*(0x[0-9a-fA-F]+|\d+)\s+(0x[0-9a-fA-F]+|\d+)\s*>` inside it.

## 3. Tests — `scripts/tests/test_check_hardware_consistency.py`
Follow `test_check_capacity.py`'s shape (`@pytest.mark.verifies`, one criterion clause per test, docstring = the clause), but
the shared `conftest.py` fixtures (`run`, `doctor`, `tree_run`) are bound to `check_capacity.py`'s flags, so this file
carries its own tiny helpers: `copy(name) -> Path` (default input → `tmp_path`), `run(**flags) -> CompletedProcess` via
`subprocess.run([sys.executable, "scripts/check_hardware_consistency.py", ...], cwd=REPO)`, and a `line(prefix)` parser
for the two grammar lines and `RESULT:`. Every fixture in `01-normative.md` §1.5 is one test; the two current-tree tests
assert status OK and exit 0 and are expected red until Stage 4. Run with `$PY -m pytest scripts/tests/test_check_hardware_consistency.py -q`
and `$PY -m ruff check scripts/tests scripts/check_hardware_consistency.py` (the author lints the test file only).

## 4. Stage 4 — the two flight edits
- `ReferenceDeploymentTopology.cpp:181-184`: `muxChannel0Device` → `muxChannel1Device`, `muxChannel2Device`,
  `muxChannel3Device`, `muxChannel5Device` respectively (line 180, face 0, is already right). The state fields exist
  (`ReferenceDeploymentTopologyDefs.hpp:133-140`).
- `B/proves_flight_control_board_v5/proves_flight_control_board_v5.dtsi:66`: `DT_SIZE_M(4)` → `DT_SIZE_M(16)`. v5c/v5d/v5e
  include this file and none overrides `&flash0` (grep of `B/` for `flash0`/`DT_SIZE_M` hits only this line).
- Target build (procedure `CLAUDE.md:46-55`): rsync to `~/scalar-build/proves-core-reference` with the listed excludes, then
  `fprime-util build` in the copy with the venv on PATH. No `.fpp` changed, so no `generate --force`; if the generated
  `build-fprime-automatic-zephyr/zephyr/zephyr.dts` still shows `0x400000` for `flash0` `reg`, the devicetree was not
  re-processed — run `fprime-util generate --force` once and build again. Record the `Memory region` lines the build
  prints (FLASH/RAM used/total) and, for the harm table, the dictionary's `commands`/`channels`/`events` counts from
  `build-artifacts/zephyr/fprime-zephyr-deployment/dict/ReferenceDeploymentTopologyDictionary.json`.

## 5. Open risks
- The Zephyr flash driver for RP2350 may also derive limits from `CONFIG_FLASH_SIZE`-style Kconfig; a grep of `B/*/Kconfig*`
  and the defconfigs for `FLASH_SIZE` found nothing, so the devicetree is the only declaration. If the build or the runtime
  disagrees, that is a finding, not a plan change.
- HP-15 §B7 and the coil steps of HP-11 read `MuxUnhealthy` only indirectly; the observable flight change of R1 is
  visible on a bench only by disconnecting one face's mux segment. Deferred to the bench milestone (ROADMAP row 3).
