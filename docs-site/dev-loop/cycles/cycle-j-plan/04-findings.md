# 04 — Findings (verified facts, file:line) — 2026-09-26, tree e1eced14 + Cycle I working set

Claims in the defect records or earlier notes found wrong or imprecise are marked **[record]**.

1. **C-08's effect is a wrong health check, not wrong I2C routing.** `Drv2605Manager` stores the mux device
   (`Drv2605Manager.cpp:37-39`) and uses it only in `initializeDevice`: `device_is_ready(this->m_mux)` → `MuxUnhealthy`
   (`:153-157`). I2C traffic goes through `m_dev`, and each `faceN_drv2605` handle is `DEVICE_DT_GET(DT_NODELABEL(faceN_drv2605))`
   (`Main.cpp:56-60`), whose devicetree parent is its own `mux_channel_N` (`v5.dtsi:231, 266, 301, 336, 407`); Zephyr's
   i2c-mux binding selects the channel for that parent. **[record]** `conflicts.md` C-08 ("four of the five coil drivers
   are being addressed through the wrong channel") and this session's earlier "coils 1, 2, 3, 5 have never been addressed"
   overstate it. The model repo's C-08 text should be corrected to: readiness of channel 0 is reported for every face; a
   dead segment 1/2/3/5 is not reported as `MuxUnhealthy` for its face.
2. **Managers checked and their channels today.** tmp112 Face0/1/2/3/5 → mux 0/1/2/3/5 (`ReferenceDeploymentTopology.cpp:160-164`,
   correct); veml6031 Face0/1/2/3/5/6/7 → 0/1/2/3/5/6/7 (`:171-177`, correct); drv2605 Face0/1/2/3/5 → 0/0/0/0/0
   (`:180-184`, four wrong). Devicetree labels: `faceN_temp_sens`, `faceN_light_sens`, `faceN_drv2605` inside
   `mux_channel_N` (`v5.dtsi:210-470`); channel 4 holds only `batt_cell1..4` (`:351-384`), channel 7 only a light sensor
   (`:458-470`). `face6_drv2605` and `face6_temp_sens` exist in the devicetree (`:429, :443`) with no manager (C-07).
3. **C-11 numbers.** `v5.dtsi:66` `reg = <0x10000000 DT_SIZE_M(4)>`; partitions `:73-95`: mcuboot `0x0+0x100000`,
   current `0x100000+0x100000`, golden `0x200000+0x100000`, test `0x300000+0x100000`, storage `0x400000+0xC00000` → end
   `0x1000000` = 16 MiB exactly. `flash0` itself is the SoC node (`lib/zephyr-workspace/zephyr/dts/vendor/raspberrypi/rpi_pico/rp2350.dtsi:204`);
   the board only sets `reg`. No other board file under `boards/bronco_space/` mentions `flash0` or `DT_SIZE_M`. No
   `FLASH_SIZE` Kconfig in the board dirs or `prj.conf`. The last build's generated tree (`~/scalar-build/.../build-fprime-automatic-zephyr/zephyr/zephyr.dts:252`)
   carries the node; its `reg` is the observable for R2.
4. **Nothing mounts `storage_partition`.** The only `fstab` is `zephyr,fstab,fatfs` (`v5.dtsi:18-21`), for the SD NAND.
   `FsFormat` exists (`format_filesystem_test.py`), so a format path exists once a filesystem type is chosen. Out of scope.
5. **Comment typo.** `ReferenceDeploymentTopologyDefs.hpp:139` documents `muxChannel6Device` as "Multiplexer channel 5
   device". Not touched.
6. **Tree state.** `main` @ e1eced14 (2026-09-20 10:03). Cycle I's implementation is untracked and its gate was green
   on 2026-09-26 (`cycle-i-review.md` §Gate); last file touched 16:11 on 2026-09-26; no build or agent process on the
   tree at 21:50. `scripts/verify.sh` (uncommitted) has a `docs` stage and `VERIFY_UT_DIR`; the `audit` stage runs only
   `check_capacity.py` (`:84`). AUDIT-9's test asserts the audit stage *contains* every `check_capacity.py` line
   (`test_verify_sh.py:90-118`), so adding a second script to that stage later does not break it.
7. **`req.py` cannot create a group** (`scripts/req.py:125-126`, `:273-278`; Cycle H `02-requirements.md:3-5`); groups and
   files are discovered by glob (`req.py:134`, `generate_rtm.py:140-142`), so a new `docs-site/requirements/*.md` is picked
   up with no registration.
