// ======================================================================
// \title  TcFrameCorrectorComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated TcFrameCorrector component base.
//
// Declares the exact base-class surface TcFrameCorrector.cpp/.hpp use, and
// records every outgoing effect (dataOut / dataReturnOut port calls, telemetry
// writes, events) into public members so host tests can assert on them — the
// same role the autocoded TesterBase plays in a full F Prime UT build.
// Handlers are public virtuals here so tests can invoke the component's
// private overrides through a base reference, as real autocode does.
//
// Minimal stand-ins for Fw::Buffer (lib/fprime/Fw/Buffer/Buffer.hpp:61,156-186)
// and ComCfg::FrameContext (project/config/ComCfg.fpp:38-49) live here because
// the real ones drag in the serialization framework. The recorders keep the
// raw pointer and size, never a copy of the bytes, so a test can prove that
// the SAME buffer object was forwarded.
// ======================================================================

#ifndef UnitTestSupport_TcFrameCorrectorComponentAc_HPP
#define UnitTestSupport_TcFrameCorrectorComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"

namespace Fw {

//! Mirrors the Fw::Buffer surface TcFrameCorrector touches: a non-owning
//! (pointer, size) pair. getData() is non-const-returning on the real type
//! too, which is what makes the in-place bit flip legal.
class Buffer {
  public:
    using SizeType = FwSizeType;

    Buffer() : m_data(nullptr), m_size(0) {}
    Buffer(U8* data, SizeType size) : m_data(data), m_size(size) {}

    U8* getData() const { return this->m_data; }
    SizeType getSize() const { return this->m_size; }
    void setData(U8* data) { this->m_data = data; }
    void setSize(SizeType size) { this->m_size = size; }
    bool isValid() const { return this->m_data != nullptr; }

  private:
    U8* m_data;
    SizeType m_size;
};

}  // namespace Fw

namespace ComCfg {

//! Mirrors the ComCfg.FrameContext struct (ComCfg.fpp:38-49) as a POD. apid is
//! kept as its underlying FwPacketDescriptorType (U16) rather than the full
//! generated enum: TcFrameCorrector only ever copies the context through.
struct FrameContext {
    FwIndexType comQueueIndex = 0;
    U16 apid = 0x00FF;  //!< ComCfg.Apid.FW_PACKET_UNKNOWN
    U16 sequenceCount = 0;
    U8 vcId = 1;
    bool authenticated = false;

    bool operator==(const FrameContext& other) const {
        return this->comQueueIndex == other.comQueueIndex && this->apid == other.apid &&
               this->sequenceCount == other.sequenceCount && this->vcId == other.vcId &&
               this->authenticated == other.authenticated;
    }
};

}  // namespace ComCfg

namespace Components {

class TcFrameCorrectorComponentBase {
  public:
    //! One recorded port call: the buffer identity (pointer and size, never a
    //! copy of the bytes) plus the context that travelled with it.
    struct BufferCallRecord {
        U8* data;
        FwSizeType size;
        ComCfg::FrameContext context;
    };

    //! One recorded FrameCorrected event.
    struct FrameCorrectedRecord {
        U16 bitIndex;
        U16 frameLength;
    };

    explicit TcFrameCorrectorComponentBase(const char* const compName) : compName(compName) {}
    virtual ~TcFrameCorrectorComponentBase() {}

    // ---- handlers implemented by the component (private overrides there) ----
    virtual void dataIn_handler(FwIndexType portNum, Fw::Buffer& data, const ComCfg::FrameContext& context) = 0;
    virtual void dataReturnIn_handler(FwIndexType portNum, Fw::Buffer& data, const ComCfg::FrameContext& context) = 0;

    // ---- test-controlled parameter values (defaults from TcFrameCorrector.fpp) ----
    bool correctionEnabled = false;
    Fw::ParamValid paramValidity = Fw::ParamValid::VALID;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    std::vector<BufferCallRecord> dataOutCalls;
    std::vector<BufferCallRecord> dataReturnOutCalls;
    std::vector<U32> tlmCorrectedFrames;
    std::vector<U32> tlmUncorrectableFrames;
    std::vector<FrameCorrectedRecord> eventsFrameCorrected;
    std::vector<U16> eventsFrameUncorrectable;

  protected:
    // ---- base-class services the component implementation calls ----
    void dataOut_out(FwIndexType portNum, Fw::Buffer& data, const ComCfg::FrameContext& context) {
        (void)portNum;
        this->dataOutCalls.push_back(BufferCallRecord{data.getData(), data.getSize(), context});
    }

    void dataReturnOut_out(FwIndexType portNum, Fw::Buffer& data, const ComCfg::FrameContext& context) {
        (void)portNum;
        this->dataReturnOutCalls.push_back(BufferCallRecord{data.getData(), data.getSize(), context});
    }

    bool paramGet_CORRECTION_ENABLED(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->correctionEnabled;
    }

    void tlmWrite_CorrectedFrames(U32 count) { this->tlmCorrectedFrames.push_back(count); }

    void tlmWrite_UncorrectableFrames(U32 count) { this->tlmUncorrectableFrames.push_back(count); }

    void log_ACTIVITY_HI_FrameCorrected(U16 bitIndex, U16 frameLength) {
        this->eventsFrameCorrected.push_back(FrameCorrectedRecord{bitIndex, frameLength});
    }

    void log_WARNING_LO_FrameUncorrectable(U16 frameLength) { this->eventsFrameUncorrectable.push_back(frameLength); }
};

}  // namespace Components

#endif
