"""
pico_temp_test.py:

Integration tests for the RP2350 built-in temperature sensor.
"""

from datetime import datetime

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.data_types.event_data import EventData
from fprime_gds.common.models.serialize.numerical_types import F32Type
from fprime_gds.common.models.serialize.time_type import TimeType
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

picoTempManager = "ReferenceDeployment.picoTempManager"


@pytest.mark.verifies("CDH-2")
def test_01_get_pico_temperature(fprime_test_api: IntegrationTestAPI, start_gds):
    """Test that we can get pico temperature.

    CDH-2 clause claimed here: the picoTempManager.GetPicoTemperature clause
    (a PicoTemperature event within 5 s, value inside the rated range). The
    tmp112Face0Manager.GetTemperature clause is covered by
    tmp112_test.py::test_01_get_temperature.
    """
    start: TimeType = TimeType().set_datetime(
        datetime.now(), time_base=TimeType.TimeBase("TB_DONT_CARE")
    )

    # Send command to get pico temperature
    proves_send_and_assert_command(
        fprime_test_api,
        f"{picoTempManager}.GetPicoTemperature",
    )
    result: EventData = fprime_test_api.assert_event(
        f"{picoTempManager}.PicoTemperature", start=start, timeout=2
    )

    assert result is not None
    assert len(result.get_args()) == 1
    temperature: F32Type = result.args[0]
    # Oracle is the rated sensor range (-40..125 C) named in the CDH-2 pass
    # criteria, not "non-negative" (TP-3).
    assert -40.0 <= temperature.val <= 125.0, (
        f"Pico temperature {temperature.val} C outside the range -40..125 C"
    )
