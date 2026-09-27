"""
data_recorder_test.py:

Board-level integration tests for the DataRecorder component (Cycle M, row
A8-5): the recorder taps the telemetry and event splitters, keeps a RAM ring
per stream, and flushes CRC-framed segment files under /rec/tlm and /rec/evt,
with retention by age and capacity and retrieval through file downlink.

Claims (docs-site/requirements/cdh.md): DH-L2-05 (configuration commanded and
persisted), DH-L2-08 (age retention), DH-L2-12 (stored events retrievable, and
still there after a reset), CDH-16 (an event raised before a COLD_RESET is
retrievable after reboot). The unit-level logic behind each is in
test/unit-tests/test_DataRecorder_Component.cpp (DataRecorder-3..11).

Interface: docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md sections 3-5
(commands, events, file formats) and 10 (tools/recorder_reader.py).

Files downlinked by FileHandling.fileDownlink land in the GDS downlink store
(``<file-storage-directory>/fprime-downlink``) under the destination name with
every '/' replaced by '_' (fprime_gds FileDownlinker.sanitize). The store is
taken from RECORDER_DOWNLINK_DIR if set, else from the test pipeline's
``down_store``, else the GDS default ``/tmp/<user>/fprime-downlink``.

MARKER CHOICE: tests that COLD_RESET the board sever every link while it
reboots and are ``uart_only`` (as reset_manager_test.py). Tests that pull a
segment file down are also ``uart_only``: their windows are derived for the
UART file-downlink rate; a 16 KiB EVT segment does not fit them over LoRa.
"""

import csv
import getpass
import io
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

import pytest
from common import cmdDispatch, proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

dataRecorder = "ReferenceDeployment.dataRecorder"
fileDownlink = "FileHandling.fileDownlink"
fileManager = "FileHandling.fileManager"
resetManager = "ReferenceDeployment.resetManager"

REPO = Path(__file__).resolve().parents[3]
READER = REPO / "tools" / "recorder_reader.py"

# ---- configuration values (01-normative sections 3 and 6) ----
TLM_RETENTION_DEFAULT_S = 604800
EVT_RETENTION_DEFAULT_S = 2592000
TLM_FLUSH_RECORDS_DEFAULT = 16
TLM_FLUSH_INTERVAL_DEFAULT_S = 60
RETENTION_UNDER_TEST_S = 3600  # DH-L2-05's accepted value
RETENTION_BELOW_MIN_S = 10  # DH-L2-05's rejected value (minimum is 60)
RETENTION_MAX_S = 2592000  # ConfigRejected reports this as max for RETENTION_S
RETENTION_AGE_TEST_S = 60  # DH-L2-08: the minimum, so the test takes about a minute

# ---- bounded windows (derivations) ----
# A sync command and its events: the recorder answers in the command handler;
# 5 s is the DH-L2-05 criterion's own bound and covers the UART round trip.
CMD_WINDOW_S = 5

# Telemetry reaches the recorder only when tlmSend runs: telemetryDelay divides
# the 1 Hz tick by 29 (telemetry_gate_test.py), so one period is ~30 s. One
# period plus 5 s guarantees the TLM ring holds records.
TELEMETRY_PERIOD_S = 30
RING_FILL_WAIT_S = TELEMETRY_PERIOD_S + 5

# DH-L2-08: SegmentDeleted(AGE) within 5 s after t1 + retention, where t1 is
# the FSW time the newer segment was opened. Retention runs on the 1 Hz tick,
# so the first eligible tick is at most 1 s after t1 + retention; the 5 s bound
# is the criterion's. The ground wait adds CMD_WINDOW_S for the event's trip.
AGE_DELETE_BOUND_S = 5
AGE_WAIT_S = RETENTION_AGE_TEST_S + AGE_DELETE_BOUND_S + CMD_WINDOW_S

# Before the DH-L2-08 measurement, older TLM segments are aged out at one
# deletion per 1 Hz tick. The TLM capacity (8 MiB) holds at most 256 full
# 32 KiB segments; 256 ticks plus margin bounds the drain.
DRAIN_TIMEOUT_S = 300
DRAIN_POLL_S = 10

# DH-L2-12 / CDH-16: the segment must be on the ground within 60 s of the
# event (the criterion's bound). An EVT segment is at most 16 KiB; at the UART
# file-downlink rate that is well inside 60 s.
DELIVERY_WINDOW_S = 60

# The GDS closes the downlinked file after the END packet; allow a few seconds
# after FileSent for the ground file to be complete.
GROUND_FILE_WAIT_S = 10

# COLD_RESET: FrameworkVersion within 15 s of the command (reset_manager_test.py).
RESET_WINDOW_S = 15

# After reboot the EVT boot scan reads at most 32 directory entries per 1 Hz
# tick; 20 s covers 640 entries, far more than the 2 MiB EVT capacity holds
# at the segment sizes this board writes. LIST_SEGMENTS answers BUSY until
# then, and proves_send_and_assert_command retries.
SCAN_SETTLE_S = 20


def _downlink_store(fprime_test_api: IntegrationTestAPI) -> Path:
    """Directory the GDS writes downlinked files into."""
    override = os.environ.get("RECORDER_DOWNLINK_DIR")
    if override:
        return Path(override)
    store = getattr(getattr(fprime_test_api, "pipeline", None), "down_store", None)
    if store:
        return Path(store)
    return Path("/tmp") / getpass.getuser() / "fprime-downlink"


def _ground_file(fprime_test_api: IntegrationTestAPI, dest: str, size: int) -> Path:
    """Wait for the downlinked copy of ``dest`` to reach ``size`` bytes and return it."""
    path = _downlink_store(fprime_test_api) / dest.replace("/", "_")
    deadline = time.time() + GROUND_FILE_WAIT_S
    while time.time() < deadline:
        if path.exists() and path.stat().st_size == size:
            return path
        time.sleep(0.5)
    raise AssertionError(
        f"downlinked file {path} not complete ({size} bytes) within {GROUND_FILE_WAIT_S}s"
    )


def _send_expect_error(
    fprime_test_api: IntegrationTestAPI, command: str, args: list, error: str
) -> None:
    """Send ``command`` and assert the dispatcher reports ``error`` for it."""
    fprime_test_api.clear_histories()
    fprime_test_api.send_command(command, args)
    result = fprime_test_api.await_event(
        f"{cmdDispatch}.OpCodeError", timeout=CMD_WINDOW_S
    )
    assert result is not None, f"{command} {args} did not fail within {CMD_WINDOW_S}s"
    assert str(result.get_args()[1].val) == error, (
        f"{command} {args} failed with {result.get_args()[1].val}, expected {error}"
    )


def _set_retention(fprime_test_api: IntegrationTestAPI, stream: str, seconds: int):
    """SET_RETENTION_S and assert ConfigApplied(stream, RETENTION_S, seconds)."""
    proves_send_and_assert_command(
        fprime_test_api,
        f"{dataRecorder}.SET_RETENTION_S",
        [stream, str(seconds)],
    )
    fprime_test_api.assert_event(
        f"{dataRecorder}.ConfigApplied",
        args=[stream, "RETENTION_S", seconds],
        timeout=CMD_WINDOW_S,
    )


def _restore_defaults(fprime_test_api: IntegrationTestAPI) -> None:
    """Put back every value these tests change, and persist the defaults."""
    _set_retention(fprime_test_api, "TLM", TLM_RETENTION_DEFAULT_S)
    _set_retention(fprime_test_api, "EVT", EVT_RETENTION_DEFAULT_S)
    proves_send_and_assert_command(
        fprime_test_api,
        f"{dataRecorder}.SET_FLUSH",
        ["TLM", str(TLM_FLUSH_RECORDS_DEFAULT), str(TLM_FLUSH_INTERVAL_DEFAULT_S)],
    )
    proves_send_and_assert_command(fprime_test_api, f"{dataRecorder}.SAVE_CONFIG")


@pytest.fixture(autouse=True)
def restore_recorder_config(fprime_test_api: IntegrationTestAPI, start_gds):
    """Leave the recorder at its default retention and flush settings, saved,
    before and after each test, so a failure cannot leave a board deleting its
    records after a minute."""
    _restore_defaults(fprime_test_api)
    yield
    _restore_defaults(fprime_test_api)


def _decode_config_retention_tlm(blob: bytes) -> int:
    """TLM retentionS from a /rec/config.bin PersistedRecord (01-normative 5.5)."""
    assert blob[:4] == b"DRC1", f"bad magic {blob[:4]!r}"
    (length,) = struct.unpack_from("<H", blob, 5)
    payload = blob[7 : 7 + length]
    assert length == 26, f"payload length {length}, expected 26"
    assert payload[0] == 1 and payload[1] == 2, "layout/streamCount"
    (retention,) = struct.unpack_from("<I", payload, 2 + 8)
    return retention


def _raise_warning_hi(fprime_test_api: IntegrationTestAPI):
    """Raise a WARNING_HI event on the board and return the live event.

    fileManager.RemoveFile of an absent file with ignoreErrors false emits
    FileRemoveError, severity WARNING_HI (lib/fprime/Svc/FileManager/Events.fppi:30-36).
    """
    name = f"/rec/absent-{int(time.time())}.none"
    fprime_test_api.clear_histories()
    fprime_test_api.send_command(f"{fileManager}.RemoveFile", [name, "false"])
    live = fprime_test_api.await_event(
        f"{fileManager}.FileRemoveError", timeout=CMD_WINDOW_S
    )
    assert live is not None, f"no FileRemoveError within {CMD_WINDOW_S}s"
    return live


def _reader_rows(dictionary: str, segment: Path) -> list[dict]:
    """Run tools/recorder_reader.py on one segment and return its CSV rows."""
    proc = subprocess.run(
        [sys.executable, str(READER), "--dictionary", dictionary, str(segment)],
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert proc.returncode == 0, f"reader exit {proc.returncode}: {proc.stderr}"
    return list(csv.DictReader(io.StringIO(proc.stdout)))


def _assert_event_in_segment(
    fprime_test_api: IntegrationTestAPI, segment: Path, live
) -> None:
    """The live event is in the segment with the same id and time tag."""
    rows = _reader_rows(fprime_test_api.dictionaries.dictionary_path, segment)
    live_time = live.get_time()
    matches = [
        r
        for r in rows
        if r["kind"] == "evt"
        and r["id"] == str(live.get_id())
        and r["seconds"] == str(live_time.seconds)
        and r["useconds"] == str(live_time.useconds)
    ]
    assert matches, (
        f"event id {live.get_id()} at {live_time.seconds}.{live_time.useconds:06d} "
        f"not found by the reader in {segment} ({len(rows)} rows)"
    )


def _downlink_newest_evt(fprime_test_api: IntegrationTestAPI):
    """DOWNLINK_NEWEST EVT; return (seq, bytes, ground file) once it is on the ground."""
    proves_send_and_assert_command(
        fprime_test_api, f"{dataRecorder}.DOWNLINK_NEWEST", ["EVT"]
    )
    info = fprime_test_api.assert_event(
        f"{dataRecorder}.SegmentInfo",
        args=["EVT", None, None, None],
        timeout=CMD_WINDOW_S,
    )
    seq = int(info.get_args()[1].val)
    size = int(info.get_args()[2].val)
    path = f"/rec/evt/{seq:08d}.bin"
    sent = fprime_test_api.await_event(
        f"{fileDownlink}.FileSent", args=[path, path], timeout=DELIVERY_WINDOW_S
    )
    assert sent is not None, f"{path} not sent within {DELIVERY_WINDOW_S}s"
    return seq, size, _ground_file(fprime_test_api, path, size)


@pytest.mark.verifies("DH-L2-05")
def test_01_retention_is_commanded_validated_and_persisted(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """SET_RETENTION_S TLM 3600 is applied, TLM 10 is rejected, and SAVE_CONFIG
    persists 3600 in /rec/config.bin as read back through file downlink."""
    _set_retention(fprime_test_api, "TLM", RETENTION_UNDER_TEST_S)

    _send_expect_error(
        fprime_test_api,
        f"{dataRecorder}.SET_RETENTION_S",
        ["TLM", str(RETENTION_BELOW_MIN_S)],
        "VALIDATION_ERROR",
    )
    fprime_test_api.assert_event(
        f"{dataRecorder}.ConfigRejected",
        args=["RETENTION_S", RETENTION_BELOW_MIN_S, RETENTION_MAX_S],
        timeout=CMD_WINDOW_S,
    )

    proves_send_and_assert_command(fprime_test_api, f"{dataRecorder}.SAVE_CONFIG")
    dest = f"dh_l2_05_config_{int(time.time())}.bin"
    fprime_test_api.clear_histories()
    proves_send_and_assert_command(
        fprime_test_api, f"{fileDownlink}.SendFile", ["/rec/config.bin", dest]
    )
    assert (
        fprime_test_api.await_event(
            f"{fileDownlink}.FileSent", timeout=DELIVERY_WINDOW_S
        )
        is not None
    ), f"/rec/config.bin not sent within {DELIVERY_WINDOW_S}s"
    # PersistedRecord overhead 11 bytes + the 26-byte payload.
    blob = _ground_file(fprime_test_api, dest, 37).read_bytes()
    assert _decode_config_retention_tlm(blob) == RETENTION_UNDER_TEST_S


def _drain_old_tlm_segments(fprime_test_api: IntegrationTestAPI) -> None:
    """Wait until LIST_SEGMENTS TLM shows at most two segments (the newest and the open one)."""
    deadline = time.time() + DRAIN_TIMEOUT_S
    while True:
        fprime_test_api.clear_histories()
        proves_send_and_assert_command(
            fprime_test_api, f"{dataRecorder}.LIST_SEGMENTS", ["TLM", "0"]
        )
        time.sleep(CMD_WINDOW_S)
        listed = fprime_test_api.get_event_test_history().retrieve()
        infos = [
            e
            for e in listed
            if e.get_template().get_full_name() == f"{dataRecorder}.SegmentInfo"
        ]
        if len(infos) <= 2:
            return
        assert time.time() < deadline, (
            f"{len(infos)} TLM segments still listed after {DRAIN_TIMEOUT_S}s of 60 s retention"
        )
        time.sleep(DRAIN_POLL_S)


def _close_tlm_segment_with_records(fprime_test_api: IntegrationTestAPI):
    """Let the TLM ring fill, then CLOSE_SEGMENT: the batch opens a new segment,
    which is closed at once. Return its SegmentOpened event."""
    time.sleep(RING_FILL_WAIT_S)
    fprime_test_api.clear_histories()
    proves_send_and_assert_command(
        fprime_test_api, f"{dataRecorder}.CLOSE_SEGMENT", ["TLM"]
    )
    opened = fprime_test_api.assert_event(
        f"{dataRecorder}.SegmentOpened", args=["TLM", None], timeout=CMD_WINDOW_S
    )
    return opened


def _fsw_seconds(fsw_time) -> float:
    """Flight-software timestamp in seconds."""
    if hasattr(fsw_time, "get_float"):
        return float(fsw_time.get_float())
    return float(fsw_time.seconds) + float(getattr(fsw_time, "useconds", 0)) / 1e6


@pytest.mark.slow
@pytest.mark.verifies("DH-L2-08")
def test_02_closed_segment_is_deleted_for_age_after_the_next_opens(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """With TLM retention 60 s, closed segment A is deleted for AGE within 5 s
    after t1 + 60 s (t1 = when newer segment B opened), and B is still listed."""
    # Hold the automatic flush back (RAM only; restored by the fixture) so the
    # only TLM segments opened during the measurement are the two CLOSE_SEGMENT
    # makes: a third segment opened early would age B out right after A.
    proves_send_and_assert_command(
        fprime_test_api, f"{dataRecorder}.SET_FLUSH", ["TLM", "32", "3600"]
    )
    _set_retention(fprime_test_api, "TLM", RETENTION_AGE_TEST_S)
    proves_send_and_assert_command(
        fprime_test_api, f"{dataRecorder}.CLOSE_SEGMENT", ["TLM"]
    )
    _drain_old_tlm_segments(fprime_test_api)

    seg_a = int(_close_tlm_segment_with_records(fprime_test_api).get_args()[1].val)
    opened_b = _close_tlm_segment_with_records(fprime_test_api)
    seg_b = int(opened_b.get_args()[1].val)
    assert seg_b > seg_a
    t1 = _fsw_seconds(opened_b.get_time())

    fprime_test_api.clear_histories()
    deleted = fprime_test_api.await_event(
        f"{dataRecorder}.SegmentDeleted",
        args=["TLM", seg_a, "AGE"],
        timeout=AGE_WAIT_S,
    )
    assert deleted is not None, (
        f"TLM segment {seg_a} not deleted for AGE within {AGE_WAIT_S}s"
    )
    t_del = _fsw_seconds(deleted.get_time())
    assert (
        t1 + RETENTION_AGE_TEST_S
        < t_del
        <= t1 + RETENTION_AGE_TEST_S + AGE_DELETE_BOUND_S
    ), (
        f"deleted at {t_del:.1f}, expected in ({t1 + RETENTION_AGE_TEST_S:.1f}, "
        f"{t1 + RETENTION_AGE_TEST_S + AGE_DELETE_BOUND_S:.1f}]"
    )

    fprime_test_api.clear_histories()
    proves_send_and_assert_command(
        fprime_test_api, f"{dataRecorder}.LIST_SEGMENTS", ["TLM", str(seg_a)]
    )
    time.sleep(CMD_WINDOW_S)
    listed = [
        int(e.get_args()[1].val)
        for e in fprime_test_api.get_event_test_history().retrieve()
        if e.get_template().get_full_name() == f"{dataRecorder}.SegmentInfo"
    ]
    assert seg_a not in listed, f"deleted segment {seg_a} still listed: {listed}"
    assert seg_b in listed, f"newer segment {seg_b} not listed: {listed}"


@pytest.mark.uart_only(reason="file-downlink window derived for UART")
@pytest.mark.verifies("DH-L2-12")
def test_03_warning_event_is_in_the_downlinked_evt_segment(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """A WARNING_HI event is found, same id and time tag, by the reader in the
    EVT segment DOWNLINK_NEWEST delivers within 60 s of the event."""
    live = _raise_warning_hi(fprime_test_api)
    raised_at = time.time()
    _, _, ground = _downlink_newest_evt(fprime_test_api)
    assert time.time() - raised_at <= DELIVERY_WINDOW_S, (
        f"segment on the ground {time.time() - raised_at:.1f}s after the event"
    )
    _assert_event_in_segment(fprime_test_api, ground, live)


@pytest.mark.slow
@pytest.mark.uart_only(reason="COLD_RESET severs every link while the board reboots")
@pytest.mark.verifies("DH-L2-12", "CDH-16")
def test_04_evt_segment_survives_cold_reset_and_is_retrievable(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """An EVT segment holding a WARNING_HI event is still listed after a
    COLD_RESET, and fileDownlink.SendFile delivers it within 60 s with the
    event decoded by the reader (same id and time tag)."""
    live = _raise_warning_hi(fprime_test_api)
    seq, size, _ = _downlink_newest_evt(fprime_test_api)
    path = f"/rec/evt/{seq:08d}.bin"

    fprime_test_api.clear_histories()
    fprime_test_api.send_command(f"{resetManager}.COLD_RESET")
    assert (
        fprime_test_api.await_event(
            "CdhCore.version.FrameworkVersion", timeout=RESET_WINDOW_S
        )
        is not None
    ), f"board did not restart within {RESET_WINDOW_S}s"
    time.sleep(SCAN_SETTLE_S)

    proves_send_and_assert_command(
        fprime_test_api,
        f"{dataRecorder}.LIST_SEGMENTS",
        ["EVT", str(seq)],
        events=[
            fprime_test_api.get_event_pred(
                f"{dataRecorder}.SegmentInfo", args=["EVT", seq, size, None]
            )
        ],
    )

    dest = f"cdh_16_{seq:08d}_{int(time.time())}.bin"
    fprime_test_api.clear_histories()
    requested_at = time.time()
    proves_send_and_assert_command(
        fprime_test_api, f"{fileDownlink}.SendFile", [path, dest]
    )
    assert (
        fprime_test_api.await_event(
            f"{fileDownlink}.FileSent", args=[path, dest], timeout=DELIVERY_WINDOW_S
        )
        is not None
    ), f"{path} not sent within {DELIVERY_WINDOW_S}s"
    ground = _ground_file(fprime_test_api, dest, size)
    assert time.time() - requested_at <= DELIVERY_WINDOW_S + GROUND_FILE_WAIT_S
    _assert_event_in_segment(fprime_test_api, ground, live)
