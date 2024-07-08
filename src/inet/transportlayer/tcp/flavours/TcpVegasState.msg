//
// Copyright (C) 2013 Maria Fernandez, Carlos Calafate, Juan-Carlos Cano and Pietro Manzoni
// Copyright (C) 2022 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//

import inet.common.INETDefs;
import inet.transportlayer.tcp.flavours.TcpTahoeRenoFamilyState;

namespace inet::tcp;

///
/// State variables for TcpVegas.
///
struct TcpVegasStateVariables extends TcpTahoeRenoFamilyStateVariables
{
    @descriptor(readonly);

    uint32_t recoverypoint = 0;
    simtime_t cwnd_changed = 0; // last time cwnd changes because of a rtx.

    simtime_t baseRTT = SIMTIME_MAX;
    simtime_t sumRTT = SIMTIME_ZERO; // sum of rtt's measured within one RTT
    int cntRTT = 0; // # of rtt's measured within one RTT
    uint32_t begseq = 0; // register next pkt to be sent,for rtt calculation in receivedDataAck
    simtime_t begtime = 0; // register time for rtt calculation
	
    simtime_t rtt_timeout = 10000.0; // vegas fine-grained timeout
    simtime_t sa; // average for vegas fine-grained timeout
    simtime_t sd; // deviation for vegas fine-grained timeout


    bool inc_flag = true; // for slow start: "exponential growth only every other RTT"
    bool incr_ss = false; // to control no incr. cwnd if during slowstart ssthresh has been exceeded before the rtt is over
    int32_t incr = 0; // incr/decr
    uint32_t worried = 0; // pkts a to retransmit due to vegas fine-grained timeout
    
    uint32_t beg_snd_nxt = 0;
    simtime_t minRTT = SIMTIME_MAX;
    int gamma = 1;
    int beta = 4;
    int alpha = 2;
};

cplusplus(TcpVegasStateVariables) {{
  public:
    virtual std::string str() const override;
    virtual std::string detailedInfo() const override;
}}

