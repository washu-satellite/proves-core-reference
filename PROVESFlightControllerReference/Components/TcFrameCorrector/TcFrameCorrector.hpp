// ======================================================================
// \title  TcFrameCorrector.hpp
// \brief  hpp file for TcFrameCorrector component implementation class
// ======================================================================

#ifndef Components_TcFrameCorrector_HPP
#define Components_TcFrameCorrector_HPP

#include "PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrectorComponentAc.hpp"

namespace Components {

class TcFrameCorrector final : public TcFrameCorrectorComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct TcFrameCorrector object
    TcFrameCorrector(const char* const compName  //!< The component name
    );

    //! Destroy TcFrameCorrector object
    ~TcFrameCorrector();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for dataIn
    //!
    //! Receives one raw LoRa packet, which on this link is exactly one CCSDS TC
    //! transfer frame. With CORRECTION_ENABLED false (or the parameter invalid)
    //! the buffer is forwarded untouched. With it true, a single-bit error is
    //! repaired in place. Every buffer is forwarded on dataOut exactly once,
    //! whatever the outcome: an uncorrectable frame is passed on unchanged so
    //! the frame accumulator drops it exactly as it does without this component.
    void dataIn_handler(FwIndexType portNum,                 //!< The port number
                        Fw::Buffer& data,                    //!< The buffer
                        const ComCfg::FrameContext& context  //!< The frame context
                        ) override;

    //! Handler implementation for dataReturnIn
    //!
    //! Passes buffer ownership straight back upstream, unchanged.
    void dataReturnIn_handler(FwIndexType portNum,                 //!< The port number
                              Fw::Buffer& data,                    //!< The buffer
                              const ComCfg::FrameContext& context  //!< The frame context
                              ) override;

  private:
    //! Count of frames repaired since boot
    U32 m_correctedFrames;
    //! Count of frames whose errors could not be repaired since boot
    U32 m_uncorrectableFrames;
};

}  // namespace Components

#endif
