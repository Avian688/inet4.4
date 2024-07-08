//
// Copyright (C) 2013 Maria Fernandez, Carlos Calafate, Juan-Carlos Cano and Pietro Manzoni
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//

#ifndef __INET_TCPVEGAS_H
#define __INET_TCPVEGAS_H

#include "inet/transportlayer/tcp/flavours/TcpTahoeRenoFamily.h"
#include "inet/transportlayer/tcp/flavours/TcpVegasState_m.h"

namespace inet {
namespace tcp {

/**
 * State variables for TcpVegas.
 */

class INET_API TcpVegas : public TcpTahoeRenoFamily
{
  protected:
    TcpVegasStateVariables *& state;


    static simsignal_t minRTTSignal;
    static simsignal_t baseRTTSignal;
    static simsignal_t targetCwndSignal;

    /** Create and return a TCPvegasStateVariables object. */
    virtual TcpStateVariables *createStateVariables() override
    {
        return new TcpVegasStateVariables();
    }

    /**
     * Update state vars with new measured RTT value. Passing two simtime_t's
     * will allow rttMeasurementComplete() to do calculations in double or
     * in 200ms/500ms ticks, as needed)
     */
    virtual void rttMeasurementComplete(simtime_t tSent, simtime_t tAcked) override;

    /** Redefine what should happen on retransmission */
    virtual void processRexmitTimer(TcpEventCode& event) override;

  public:
    /** Ctor */
    TcpVegas();

    /** Utility function to recalculate ssthresh */
    virtual void recalculateSlowStartThreshold();

    /** Redefine what should happen when data got acked, to add congestion window management */
    virtual void receivedDataAck(uint32_t firstSeqAcked) override;

    /** Redefine what should happen when dupAck was received, to add congestion window management */
    virtual void receivedDuplicateAck() override;

    virtual void receivedDataAckNewReno(uint32_t firstSeqAcked);

    virtual void receivedDuplicateAckNewReno();
};

} // namespace tcp
} // namespace inet

#endif

