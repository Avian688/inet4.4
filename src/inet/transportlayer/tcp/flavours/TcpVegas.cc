//
// Copyright (C) 2013 Maria Fernandez, Carlos Calafate, Juan-Carlos Cano and Pietro Manzoni
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//

#include "inet/transportlayer/tcp/flavours/TcpVegas.h"

#include <algorithm> // min,max

#include "inet/transportlayer/tcp/Tcp.h"

namespace inet {

namespace tcp {

#define MIN_REXMIT_TIMEOUT     1.0   // 1s
#define MAX_REXMIT_TIMEOUT     240   // 2 * MSL (RFC 1122)

Register_Class(TcpVegas);

simsignal_t TcpVegas::minRTTSignal = cComponent::registerSignal("minRTT");
simsignal_t TcpVegas::baseRTTSignal = cComponent::registerSignal("baseRTT");
simsignal_t TcpVegas::targetCwndSignal = cComponent::registerSignal("targetCwnd");

TcpVegas::TcpVegas()
    : TcpTahoeRenoFamily(), state((TcpVegasStateVariables *&)TcpAlgorithm::state)
{
}

std::string TcpVegasStateVariables::str() const {
    std::stringstream out;
    out << TcpTahoeRenoFamilyStateVariables::str();
    return out.str();
}

std::string TcpVegasStateVariables::detailedInfo() const {
    std::stringstream out;
    out << TcpTahoeRenoFamilyStateVariables::detailedInfo();
    return out.str();
}

// Process rexmit timer
void TcpVegas::processRexmitTimer(TcpEventCode& event)
{
    TcpTahoeRenoFamily::processRexmitTimer(event);

    if (event == TCP_E_ABORT)
        return;

    // RFC 3782, page 6:
    // "6)  Retransmit timeouts:
    // After a retransmit timeout, record the highest sequence number
    // transmitted in the variable "recover" and exit the Fast Recovery
    // procedure if applicable."
    state->recover = (state->snd_max - 1);
    EV_INFO << "recover=" << state->recover << "\n";
    state->lossRecovery = false;
    state->firstPartialACK = false;
    EV_INFO << "Loss Recovery terminated.\n";

    // After REXMIT timeout TCP NewReno should start slow start with snd_cwnd = snd_mss.
    //
    // If calling "retransmitData();" there is no rexmit limitation (bytesToSend > snd_cwnd)
    // therefore "sendData();" has been modified and is called to rexmit outstanding data.
    //
    // RFC 2581, page 5:
    // "Furthermore, upon a timeout cwnd MUST be set to no more than the loss
    // window, LW, which equals 1 full-sized segment (regardless of the
    // value of IW).  Therefore, after retransmitting the dropped segment
    // the TCP sender uses the slow start algorithm to increase the window
    // from 1 full-sized segment to the new value of ssthresh, at which
    // point congestion avoidance again takes over."

    // begin Slow Start (RFC 2581)
    recalculateSlowStartThreshold();
    state->snd_cwnd = state->snd_mss;

    conn->emit(cwndSignal, state->snd_cwnd);

    EV_INFO << "Begin Slow Start: resetting cwnd to " << state->snd_cwnd
            << ", ssthresh=" << state->ssthresh << "\n";
    state->afterRto = true;
    conn->retransmitOneSegment(true);
}

void TcpVegas::receivedDataAck(uint32_t firstSeqAcked)
{
    if (seqGreater(state->snd_una, state->beg_snd_nxt)) {
        /* Do the Vegas once-per-RTT cwnd adjustment. */

        /* Save the extent of the current window so we can use this
         * at the end of the next RTT.
         */
        state->beg_snd_nxt = state->snd_nxt;

        /* We do the Vegas calculations only if we got enough RTT
         * samples that we can be reasonably sure that we got
         * at least one RTT sample that wasn't from a delayed ACK.
         * If we only had 2 samples total,
         * then that means we're getting only 1 ACK per RTT, which
         * means they're almost certainly delayed ACKs.
         * If  we have 3 samples, we should be OK.
         */

        if (state->cntRTT <= 2) {
            /* We don't have enough RTT samples to do the Vegas
             * calculation, so we'll behave like Reno.
             */
            receivedDataAckNewReno(firstSeqAcked);
        }
        else {
            TcpTahoeRenoFamily::receivedDataAck(firstSeqAcked);
            uint32_t diff;
            simtime_t rtt;
            uint32_t target_cwnd;
            double targetCalc;
            /* We have enough RTT samples, so, using the Vegas
             * algorithm, we determine if we should increase or
             * decrease cwnd, and by how much.
             */

            /* Pluck out the RTT we are using for the Vegas
             * calculations. This is the min RTT seen during the
             * last RTT. Taking the min filters out the effects
             * of delayed ACKs, at the cost of noticing congestion
             * a bit later.
             */

            rtt = state->minRTT;

            /* Calculate the cwnd we should have, if we weren't
             * going too fast.
             *
             * This is:
             *     (actual rate in segments) * baseRTT
             */
            //double tmp = state->baseRTT / state->minRTT;
            targetCalc = (double)(state->snd_cwnd/state->snd_mss) * state->baseRTT.dbl();
            target_cwnd = targetCalc / rtt.dbl();
            conn->emit(targetCwndSignal, target_cwnd);

            diff = (double)(state->snd_cwnd/state->snd_mss) - target_cwnd;

            if ((diff > state->gamma) && (state->snd_cwnd < state->ssthresh)) {
                /* Going too fast. Time to slow down
                 * and switch to congestion avoidance.
                 */

                /* Set cwnd to match the actual rate
                 * exactly:
                 *   cwnd = (actual rate) * baseRTT
                 * Then we add 1 because the integer
                 * truncation robs us of full link
                 * utilization.
                 */
                state->snd_cwnd = std::min(state->snd_cwnd, ((target_cwnd * state->snd_mss) + state->snd_mss));
                recalculateSlowStartThreshold();
            }
            else if (state->snd_cwnd < state->ssthresh){
                // perform Slow Start. RFC 2581: "During slow start, a TCP increments cwnd
                // by at most SMSS bytes for each ACK received that acknowledges new data."
                state->snd_cwnd += state->snd_mss;
            }
            else{
                /* Congestion avoidance. */

                /* Figure out where we would like cwnd
                 * to be.
                 */
                if (diff > state->beta){
                    /* The old window was too fast, so
                     * we slow down.
                     */
                    state->snd_cwnd = state->snd_cwnd - state->snd_mss;
                    recalculateSlowStartThreshold();
                }
                else if (diff < state->alpha) {
                    /* We don't have enough extra packets
                     * in the network, so speed up.
                     */
                    state->snd_cwnd = state->snd_cwnd + state->snd_mss;
                }
                else {
                    /* Sending just as fast as we
                     * should be.
                     */
                }
            }

            if (state->snd_cwnd < state->snd_mss) {
                state->snd_cwnd = 2 * state->snd_mss;
            }
        }

        state->cntRTT = 0;
        state->minRTT = SIMTIME_MAX;
    } // end 'once per rtt' section
    else{
        if(state->snd_cwnd < state->ssthresh){
            receivedDataAckNewReno(firstSeqAcked);
        }
    }
    conn->emit(cwndSignal, state->snd_cwnd);
    // Try to send more data
    sendData(false);
}

void TcpVegas::receivedDuplicateAck()
{
    receivedDuplicateAckNewReno();
}

void TcpVegas::rttMeasurementComplete(simtime_t tSent, simtime_t tAcked)
{
    //
    // Jacobson's algorithm for estimating RTT and adaptively setting RTO.
    //
    // Note: this implementation calculates in doubles. An impl. which uses
    // 500ms ticks is available from old tcpmodule.cc:calcRetransTimer().
    //

    // update smoothed RTT estimate (srtt) and variance (rttvar)
    const double g = 0.125; // 1 / 8; (1 - alpha) where alpha == 7 / 8;
    simtime_t newRTT = tAcked - tSent;

    state->baseRTT = std::min(state->baseRTT, newRTT);
    state->minRTT = std::min(state->minRTT, newRTT);
    conn->emit(baseRTTSignal, state->baseRTT);
    conn->emit(minRTTSignal, state->minRTT);
    state->cntRTT++;

    simtime_t& srtt = state->srtt;
    simtime_t& rttvar = state->rttvar;

    simtime_t err = newRTT - srtt;

    srtt += g * err;
    rttvar += g * (fabs(err) - rttvar);

    // assign RTO (here: rexmit_timeout) a new value
    simtime_t rto = srtt + 4 * rttvar;

    if (rto > MAX_REXMIT_TIMEOUT)
        rto = MAX_REXMIT_TIMEOUT;
    else if (rto < MIN_REXMIT_TIMEOUT)
        rto = MIN_REXMIT_TIMEOUT;

    state->rexmit_timeout = rto;

    // record statistics
    EV_DETAIL << "Measured RTT=" << (newRTT * 1000) << "ms, updated SRTT=" << (srtt * 1000)
              << "ms, new RTO=" << (rto * 1000) << "ms\n";

    conn->emit(rttSignal, newRTT);
    conn->emit(srttSignal, srtt);
    conn->emit(rttvarSignal, rttvar);
    conn->emit(rtoSignal, rto);
}

void TcpVegas::receivedDataAckNewReno(uint32_t firstSeqAcked)
{
    TcpTahoeRenoFamily::receivedDataAck(firstSeqAcked);

     // RFC 3782, page 5:
     // "5) When an ACK arrives that acknowledges new data, this ACK could be
     // the acknowledgment elicited by the retransmission from step 2, or
     // elicited by a later retransmission.
     //
     // Full acknowledgements:
     // If this ACK acknowledges all of the data up to and including
     // "recover", then the ACK acknowledges all the intermediate
     // segments sent between the original transmission of the lost
     // segment and the receipt of the third duplicate ACK.  Set cwnd to
     // either (1) min (ssthresh, FlightSize + SMSS) or (2) ssthresh,
     // where ssthresh is the value set in step 1; this is termed
     // "deflating" the window.  (We note that "FlightSize" in step 1
     // referred to the amount of data outstanding in step 1, when Fast
     // Recovery was entered, while "FlightSize" in step 5 refers to the
     // amount of data outstanding in step 5, when Fast Recovery is
     // exited.)  If the second option is selected, the implementation is
     // encouraged to take measures to avoid a possible burst of data, in
     // case the amount of data outstanding in the network is much less
     // than the new congestion window allows.  A simple mechanism is to
     // limit the number of data packets that can be sent in response to
     // a single acknowledgement; this is known as "maxburst_" in the NS
     // simulator.  Exit the Fast Recovery procedure."
     if (state->lossRecovery) {
         if (seqGE(state->snd_una - 1, state->recover)) {
             // Exit Fast Recovery: deflating cwnd
             //
             // option (1): set cwnd to min (ssthresh, FlightSize + SMSS)
             uint32_t flight_size = state->snd_max - state->snd_una;
             state->snd_cwnd = std::min(state->ssthresh, flight_size + state->snd_mss);
             EV_INFO << "Fast Recovery - Full ACK received: Exit Fast Recovery, setting cwnd to " << state->snd_cwnd << "\n";
             // option (2): set cwnd to ssthresh
 //            state->snd_cwnd = state->ssthresh;
 //            tcpEV << "Fast Recovery - Full ACK received: Exit Fast Recovery, setting cwnd to ssthresh=" << state->ssthresh << "\n";
             // TODO - If the second option (2) is selected, take measures to avoid a possible burst of data (maxburst)!
             conn->emit(cwndSignal, state->snd_cwnd);

             state->lossRecovery = false;
             state->firstPartialACK = false;
             EV_INFO << "Loss Recovery terminated.\n";
         }
         else {
             // RFC 3782, page 5:
             // "Partial acknowledgements:
             // If this ACK does *not* acknowledge all of the data up to and
             // including "recover", then this is a partial ACK.  In this case,
             // retransmit the first unacknowledged segment.  Deflate the
             // congestion window by the amount of new data acknowledged by the
             // cumulative acknowledgement field.  If the partial ACK
             // acknowledges at least one SMSS of new data, then add back SMSS
             // bytes to the congestion window.  As in Step 3, this artificially
             // inflates the congestion window in order to reflect the additional
             // segment that has left the network.  Send a new segment if
             // permitted by the new value of cwnd.  This "partial window
             // deflation" attempts to ensure that, when Fast Recovery eventually
             // ends, approximately ssthresh amount of data will be outstanding
             // in the network.  Do not exit the Fast Recovery procedure (i.e.,
             // if any duplicate ACKs subsequently arrive, execute Steps 3 and 4
             // above).
             //
             // For the first partial ACK that arrives during Fast Recovery, also
             // reset the retransmit timer.  Timer management is discussed in
             // more detail in Section 4."

             EV_INFO << "Fast Recovery - Partial ACK received: retransmitting the first unacknowledged segment\n";
             // retransmit first unacknowledged segment
             conn->retransmitOneSegment(false);

             // deflate cwnd by amount of new data acknowledged by cumulative acknowledgement field
             state->snd_cwnd -= state->snd_una - firstSeqAcked;

             conn->emit(cwndSignal, state->snd_cwnd);

             EV_INFO << "Fast Recovery: deflating cwnd by amount of new data acknowledged, new cwnd=" << state->snd_cwnd << "\n";

             // if the partial ACK acknowledges at least one SMSS of new data, then add back SMSS bytes to the cwnd
             if (state->snd_una - firstSeqAcked >= state->snd_mss) {
                 state->snd_cwnd += state->snd_mss;

                 conn->emit(cwndSignal, state->snd_cwnd);

                 EV_DETAIL << "Fast Recovery: inflating cwnd by SMSS, new cwnd=" << state->snd_cwnd << "\n";
             }

             // try to send a new segment if permitted by the new value of cwnd
             sendData(false);

             // reset REXMIT timer for the first partial ACK that arrives during Fast Recovery
             if (state->lossRecovery) {
                 if (!state->firstPartialACK) {
                     state->firstPartialACK = true;
                     EV_DETAIL << "First partial ACK arrived during recovery, restarting REXMIT timer.\n";
                     restartRexmitTimer();
                 }
             }
         }
     }
     else {
         //
         // Perform slow start and congestion avoidance.
         //
         if (state->snd_cwnd < state->ssthresh) {
             EV_DETAIL << "cwnd <= ssthresh: Slow Start: increasing cwnd by SMSS bytes to ";

             // perform Slow Start. RFC 2581: "During slow start, a TCP increments cwnd
             // by at most SMSS bytes for each ACK received that acknowledges new data."
             state->snd_cwnd += state->snd_mss;

             // Note: we could increase cwnd based on the number of bytes being
             // acknowledged by each arriving ACK, rather than by the number of ACKs
             // that arrive. This is called "Appropriate Byte Counting" (ABC) and is
             // described in RFC 3465. This RFC is experimental and probably not
             // implemented in real-life TCPs, hence it's commented out. Also, the ABC
             // RFC would require other modifications as well in addition to the
             // two lines below.
             //
 //            int bytesAcked = state->snd_una - firstSeqAcked;
 //            state->snd_cwnd += bytesAcked * state->snd_mss;

             conn->emit(cwndSignal, state->snd_cwnd);

             EV_DETAIL << "cwnd=" << state->snd_cwnd << "\n";
         }
         else {
             // perform Congestion Avoidance (RFC 2581)
             uint32_t incr = state->snd_mss * state->snd_mss / state->snd_cwnd;

             if (incr == 0)
                 incr = 1;

             state->snd_cwnd += incr;

             conn->emit(cwndSignal, state->snd_cwnd);

             //
             // Note: some implementations use extra additive constant mss / 8 here
             // which is known to be incorrect (RFC 2581 p5)
             //
             // Note 2: RFC 3465 (experimental) "Appropriate Byte Counting" (ABC)
             // would require maintaining a bytes_acked variable here which we don't do
             //

             EV_DETAIL << "cwnd > ssthresh: Congestion Avoidance: increasing cwnd linearly, to " << state->snd_cwnd << "\n";
         }

         // RFC 3782, page 13:
         // "When not in Fast Recovery, the value of the state variable "recover"
         // should be pulled along with the value of the state variable for
         // acknowledgments (typically, "snd_una") so that, when large amounts of
         // data have been sent and acked, the sequence space does not wrap and
         // falsely indicate that Fast Recovery should not be entered (Section 3,
         // step 1, last paragraph)."
         state->recover = (state->snd_una - 2);
     }

     sendData(false);
}


void TcpVegas::receivedDuplicateAckNewReno()
{
    TcpTahoeRenoFamily::receivedDuplicateAck();

        if (state->dupacks == state->dupthresh) {
            if (!state->lossRecovery) {
                // RFC 3782, page 4:
                // "1) Three duplicate ACKs:
                // When the third duplicate ACK is received and the sender is not
                // already in the Fast Recovery procedure, check to see if the
                // Cumulative Acknowledgement field covers more than "recover".  If
                // so, go to Step 1A.  Otherwise, go to Step 1B."
                //
                // RFC 3782, page 6:
                // "Step 1 specifies a check that the Cumulative Acknowledgement field
                // covers more than "recover".  Because the acknowledgement field
                // contains the sequence number that the sender next expects to receive,
                // the acknowledgement "ack_number" covers more than "recover" when:
                //      ack_number - 1 > recover;"
                if (state->snd_una - 1 > state->recover) {
                    EV_INFO << "NewReno on dupAcks == DUPTHRESH(=" << state->dupthresh << ": perform Fast Retransmit, and enter Fast Recovery:";

                    // RFC 3782, page 4:
                    // "1A) Invoking Fast Retransmit:
                    // If so, then set ssthresh to no more than the value given in
                    // equation 1 below.  (This is equation 3 from [RFC2581]).
                    //      ssthresh = max (FlightSize / 2, 2*SMSS)           (1)
                    // In addition, record the highest sequence number transmitted in
                    // the variable "recover", and go to Step 2."
                    recalculateSlowStartThreshold();
                    state->recover = (state->snd_max - 1);
                    state->firstPartialACK = false;
                    state->lossRecovery = true;
                    EV_INFO << " set recover=" << state->recover;

                    // RFC 3782, page 4:
                    // "2) Entering Fast Retransmit:
                    // Retransmit the lost segment and set cwnd to ssthresh plus 3 * SMSS.
                    // This artificially "inflates" the congestion window by the number
                    // of segments (three) that have left the network and the receiver
                    // has buffered."
                    state->snd_cwnd = state->ssthresh + 3 * state->snd_mss;

                    conn->emit(cwndSignal, state->snd_cwnd);

                    EV_DETAIL << " , cwnd=" << state->snd_cwnd << ", ssthresh=" << state->ssthresh << "\n";
                    conn->retransmitOneSegment(false);

                    // RFC 3782, page 5:
                    // "4) Fast Recovery, continued:
                    // Transmit a segment, if allowed by the new value of cwnd and the
                    // receiver's advertised window."
                    sendData(false);
                }
                else {
                    EV_INFO << "NewReno on dupAcks == DUPTHRESH(=" << state->dupthresh << ": not invoking Fast Retransmit and Fast Recovery\n";

                    // RFC 3782, page 4:
                    // "1B) Not invoking Fast Retransmit:
                    // Do not enter the Fast Retransmit and Fast Recovery procedure.  In
                    // particular, do not change ssthresh, do not go to Step 2 to
                    // retransmit the "lost" segment, and do not execute Step 3 upon
                    // subsequent duplicate ACKs."
                }
            }
            EV_INFO << "NewReno on dupAcks == DUPTHRESH(=" << state->dupthresh << ": TCP is already in Fast Recovery procedure\n";
        }
        else if (state->dupacks > state->dupthresh) {
            if (state->lossRecovery) {
                // RFC 3782, page 4:
                // "3) Fast Recovery:
                // For each additional duplicate ACK received while in Fast
                // Recovery, increment cwnd by SMSS.  This artificially inflates the
                // congestion window in order to reflect the additional segment that
                // has left the network."
                state->snd_cwnd += state->snd_mss;

                conn->emit(cwndSignal, state->snd_cwnd);

                EV_DETAIL << "NewReno on dupAcks > DUPTHRESH(=" << state->dupthresh << ": Fast Recovery: inflating cwnd by SMSS, new cwnd=" << state->snd_cwnd << "\n";

                // RFC 3782, page 5:
                // "4) Fast Recovery, continued:
                // Transmit a segment, if allowed by the new value of cwnd and the
                // receiver's advertised window."
                sendData(false);
            }
        }
}

void TcpVegas::recalculateSlowStartThreshold()
{
    // RFC 2581, page 4:
    // "When a TCP sender detects segment loss using the retransmission
    // timer, the value of ssthresh MUST be set to no more than the value
    // given in equation 3:
    //
    //   ssthresh = max (FlightSize / 2, 2*SMSS)            (3)
    //
    // As discussed above, FlightSize is the amount of outstanding data in
    // the network."

    // set ssthresh to flight size / 2, but at least 2 SMSS
    // (the formula below practically amounts to ssthresh = cwnd / 2 most of the time)
    uint32_t flight_size = std::min(state->snd_cwnd, state->snd_wnd); // FIXME - Does this formula computes the amount of outstanding data?
//    uint32_t flight_size = state->snd_max - state->snd_una;
    state->ssthresh = std::max(flight_size / 2, 2 * state->snd_mss);

    conn->emit(ssthreshSignal, state->ssthresh);
}

} // namespace tcp

} // namespace inet
