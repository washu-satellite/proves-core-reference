/*
 * TlmPacketizerComponentImplCfg.hpp
 *
 *  Created on: Dec 10, 2017
 *      Author: tim
 */

// \copyright
// Copyright 2009-2015, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.

#ifndef SVC_TLMPACKETIZER_TLMPACKETIZERCOMPONENTIMPLCFG_HPP_
#define SVC_TLMPACKETIZER_TLMPACKETIZERCOMPONENTIMPLCFG_HPP_

#include <Fw/FPrimeBasicTypes.hpp>

namespace Svc {
static const FwChanIdType MAX_PACKETIZER_PACKETS = 24;

static const FwChanIdType MAX_PACKETIZER_CHANNELS =
    256;  // !< Must be >= the number of distinct channels named in ReferenceDeploymentPackets.fppi,
          // packets AND the omit block: setPacketList inserts both lists into one
          // RedBlackTreeMap<FwChanIdType, FwSizeType, MAX_PACKETIZER_CHANNELS>
          // (Svc/TlmPacketizer/TlmPacketizer.cpp:86-87,148-149) and asserts at boot when it is full.
          // Checked by scripts/check_packet_set.py in scripts/verify.sh; the target build does not.

static const FwChanIdType TLMPACKETIZER_MAX_MISSING_TLM_CHECK =
    25;  // !< Maximum number of missing telemetry channel checks
}  // namespace Svc

#endif /* SVC_TLMPACKETIZER_TLMPACKETIZERCOMPONENTIMPLCFG_HPP_ */
