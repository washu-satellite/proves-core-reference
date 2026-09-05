"""
tc_frame_corrector_test.py:

Board procedure (NOT evidence) for TcFrameCorrector, the single-bit corrector
on the LoRa uplink (CH-L2-20).

This file is deliberately skipped and carries NO ``verifies`` marker: a skipped
test is a written procedure, not proof, and linking it would put a green mark
on the traceability matrix for something no machine has run. It exists so the
board run has an exact script, and so the decision to flip
``CORRECTION_ENABLED`` to true has a named gate.

WHAT IS MISSING: injecting a *single* bit error into an otherwise valid TC
frame needs a ground framer that corrupts one bit after computing the FECF.
``Framing/src/authenticate_plugin.py`` adds the auth header and trailer only
(:129-167) and has no corruption hook, and the LoRa packetisation itself is
done by the CircuitPython passthrough board (``lora_passthrough_test.py``),
outside this repo. Building that hook is the prerequisite for un-skipping.

PROCEDURE once the corrupting framer exists:
  1. ``ReferenceDeployment.tcFrameCorrector.CORRECTION_ENABLED_PRM_SET true``
     (RAM-only unless followed by ``FileHandling.prmDb.PRM_SAVE_FILE``; leave
     it unsaved so a reset restores the pass-through default).
  2. Uplink a valid command frame with exactly one bit flipped after the FECF
     was computed.
  3. Expect ``tcFrameCorrector.FrameCorrected(bitIndex, frameLength)`` with
     bitIndex equal to the injected position, and the command itself to ack —
     the repaired frame must reach the dispatcher.
  4. Uplink a frame with two bits flipped: expect
     ``tcFrameCorrector.FrameUncorrectable(frameLength)`` and NO command ack.
  5. ``CORRECTION_ENABLED_PRM_SET false`` and confirm a single-bit-flipped
     frame produces neither event and no ack (pass-through, dropped by the
     accumulator exactly as before this component existed).
"""

import pytest
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

tcFrameCorrector = "ReferenceDeployment.tcFrameCorrector"


@pytest.mark.skip(reason="needs a ground framer that can inject a single-bit error")
def test_01_single_bit_flip_is_corrected_and_the_command_acks(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Placeholder for the procedure documented in the module docstring."""
    raise NotImplementedError(
        f"See the module docstring: {tcFrameCorrector} needs a corrupting "
        "ground framer before this can run on hardware."
    )
