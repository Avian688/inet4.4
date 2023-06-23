//
// Copyright (C) 2009-2010 Thomas Reschka
// Copyright (C) 2011 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//

#include "inet/transportlayer/tcp/TcpSackRexmitQueue.h"
#include <chrono>

namespace inet {

namespace tcp {

TcpSackRexmitQueue::TcpSackRexmitQueue()
{
    conn = nullptr;
    begin = end = 0;
}

TcpSackRexmitQueue::~TcpSackRexmitQueue()
{
    while (!rexmitQueue.empty())
        rexmitQueue.pop_front();
}

void TcpSackRexmitQueue::init(uint32_t seqNum)
{
    begin = seqNum;
    end = seqNum;
}

std::string TcpSackRexmitQueue::str() const
{
    std::stringstream out;

    out << "[" << begin << ".." << end << ")";
    return out.str();
}

std::string TcpSackRexmitQueue::detailedInfo() const
{
//    std::stringstream out;
//    out << str() << endl;
//
//    uint j = 1;
//
//    for (const auto& elem : rexmitQueue) {
//        out << j << ". region: [" << elem.beginSeqNum << ".." << elem.endSeqNum
//            << ") \t sacked=" << elem.sacked << "\t rexmitted=" << elem.rexmitted
//            << endl;
//        j++;
//    }
//    return out.str();
    std::stringstream out;
    out << str() << endl;

    uint j = 1;

    for (auto elem : rexmitMap) {
        out << j << ". region: [" << elem.second.beginSeqNum << ".." << elem.second.endSeqNum
            << ") \t sacked=" << elem.second.sacked << "\t rexmitted=" << elem.second.rexmitted
            << endl;
        j++;
    }
    return out.str();
}

void TcpSackRexmitQueue::discardUpTo(uint32_t seqNum)
{
    if(!(seqLE(begin, seqNum) && seqLE(seqNum, end))){
        std::cout << "\n ASSERT in discardUpTo FAILED" << endl;
        std::cout << "\n" << detailedInfo() << endl;
    }
    ASSERT(seqLE(begin, seqNum) && seqLE(seqNum, end));
//    if (!rexmitQueue.empty()) {
//        auto i = rexmitQueue.begin();
//        while ((i != rexmitQueue.end()) && seqLE(i->endSeqNum, seqNum))
//        {
//            rexmitMap.erase(i->endSeqNum);
//            i = rexmitQueue.erase(i);
//        }
//        if (i != rexmitQueue.end()) {
//            //shift begin to seqNum. Will shift large first blocks beginSeqNum up to seqNum.
//            ASSERT(seqLE(i->beginSeqNum, seqNum) && seqLess(seqNum, i->endSeqNum));
//            i->beginSeqNum = seqNum;
//        }
//    }
    if (!rexmitMap.empty()) {
            auto i = rexmitMap.begin();
            auto iEnd = rexmitMap.end();
//            while ((i != rexmitMap.end()) && seqLE(i->second.endSeqNum, seqNum))
//            {
//                rexmitMap.erase(i->second.endSeqNum);
//                i++;
//            }
            while ((i != iEnd) && seqLE(i->second.endSeqNum, seqNum)){
                    i = rexmitMap.erase(i);
            }
//            if (i != rexmitMap.end()) {
//                //shift begin to seqNum. Will shift large first blocks beginSeqNum up to seqNum.
//                ASSERT(seqLE(i->second.beginSeqNum, seqNum) && seqLess(seqNum, i->second.endSeqNum));
//                i->second.beginSeqNum = seqNum;
//            }
        }
    begin = seqNum;

    // TESTING queue:
    //ASSERT(checkQueue());
}

void TcpSackRexmitQueue::enqueueSentData(uint32_t fromSeqNum, uint32_t toSeqNum)
{
    //std::cout << "\n BEGIN: " << begin << endl;
    //std::cout << "\n END: " << end << endl;

//    if(simTime().dbl() > 2){
//        std::cout << "\n BOUND CHECK ONE: " << boundTimeCheckOne << endl;
//        std::cout << "\n WHILE CHECK ONE: " << whileTimeCheckOne << endl;
//        std::cout << "\n BOUND CHECK TWO: " << boundTimeCheckTwo << endl;
//        std::cout << "\n WHILE CHECK TWO: " << whileTimeCheckTwo << endl;
//    }

    ASSERT(seqLE(begin, fromSeqNum) && seqLE(fromSeqNum, end));

    bool found = false;
    Region region;

    EV_INFO << "rexmitQ: " << str() << " enqueueSentData [" << fromSeqNum << ".." << toSeqNum << ")\n";

    //std::cout << "\n map size: " << rexmitMap.size() << endl;
    if(rexmitMap.size() > 3000){
        std::cout << detailedInfo() << endl;
    }
    ASSERT(seqLess(fromSeqNum, toSeqNum));

    if (rexmitMap.empty() || (end == fromSeqNum)) { //where most of the blocks are pushed onto the scoreboard. When added, pop prev element (if not rexmitted or sackeed) and extend said region.
        region.beginSeqNum = fromSeqNum;
        region.endSeqNum = toSeqNum;
        region.sacked = false;
        region.rexmitted = false;
        region.numOfDiscontiguousSacks = 0;
        if((fromSeqNum == (--rexmitMap.end())->second.endSeqNum) && (!(--rexmitMap.end())->second.sacked && !(--rexmitMap.end())->second.rexmitted)){  //extending current region TODO add check to ensure beginSeqNum of new block starts where the previous block ends
            //std::cout << "\n EXTENDING BLOCK" << endl;
            Region dupRegion;
            dupRegion.beginSeqNum = (--rexmitMap.end())->second.beginSeqNum;
            dupRegion.endSeqNum = toSeqNum;
            dupRegion.sacked = (--rexmitMap.end())->second.sacked;
            dupRegion.rexmitted = (--rexmitMap.end())->second.rexmitted;
            dupRegion.numOfDiscontiguousSacks = (--rexmitMap.end())->second.numOfDiscontiguousSacks;

            rexmitMap.erase((--rexmitMap.end())->second.endSeqNum);
            //rexmitQueue.back().endSeqNum = toSeqNum;
            rexmitMap.insert({toSeqNum, dupRegion});
            //TODO remove map element and add new with updated endSeqNumber!!!
        }
        else{
            //std::cout << "\n NOT EXTENDING BLOCK - ADDING NEW ITEM" << endl;
            //rexmitQueue.push_back(region);
            rexmitMap.insert({toSeqNum, region});
        }
        found = true;
        fromSeqNum = toSeqNum;
    }
    else {
        //auto i = rexmitQueue.begin();

        //while (i != rexmitQueue.end() && seqLE(i->endSeqNum, fromSeqNum))
        //    i++;

        auto iter = rexmitMap.upper_bound(fromSeqNum);

        ASSERT(iter != rexmitMap.end());
        ASSERT(seqLE(iter->second.beginSeqNum, fromSeqNum) && seqLess(fromSeqNum, iter->second.endSeqNum));
        //ASSERT(i != rexmitQueue.end());
        //ASSERT(seqLE(i->beginSeqNum, fromSeqNum) && seqLess(fromSeqNum, i->endSeqNum));

        if (iter->second.beginSeqNum != fromSeqNum) {// is this used? Ignore this edge case for now! TODO
            // chunk item
            region = iter->second; //TODO CHECK THIS
            region.endSeqNum = fromSeqNum;
            //rexmitQueue.insert(i, region);
            rexmitMap.insert({region.endSeqNum, region});
            iter->second.beginSeqNum = fromSeqNum;
        }

        int countTest = 0;
        //std::cout << "\n Adding block with the following info:" << endl;
        //std::cout << "\n fromSeqNum " << fromSeqNum << endl;
        //std::cout << "\n toSeqNum " << toSeqNum << endl;
        while (iter != rexmitMap.end() && seqLE(iter->second.endSeqNum, toSeqNum)) { //go to toSeqNum in queue - when is this used?
            //std::cout << "\n WHILE being used" << endl;
            //std::cout << "\n i->beginSeqNum " << i->beginSeqNum << endl;
            //std::cout << "\n i->endSeqNum " << i->endSeqNum << endl;
            iter->second.rexmitted = true;
            fromSeqNum = iter->second.endSeqNum;
            found = true;
            iter++;
            //std::cout << "\n"<<  detailedInfo() << endl;
            countTest++;
        }
        //std::cout << "\n WHILE count = " << countTest << endl;
        //std::cout << "\n Length of scoreboard =  " << rexmitQueue.size() << endl;

        if (fromSeqNum != toSeqNum) { //rarely called? this is called if the block does not exist AND fromSeqNum is not the end of the current queue
            bool beforeEnd = (iter != rexmitMap.end());

            //ASSERT(i == rexmitQueue.end() || seqLess(i->beginSeqNum, toSeqNum));
            ASSERT(iter == rexmitMap.end() || seqLess(iter->second.beginSeqNum, toSeqNum));

            region.beginSeqNum = fromSeqNum;
            region.endSeqNum = toSeqNum;
            region.sacked = beforeEnd ? iter->second.sacked : false;
            region.rexmitted = beforeEnd;
            //rexmitQueue.insert(i, region);
            rexmitMap.insert({toSeqNum, region});
            //std::cout << "\n Inserting this block to scoreboard..." << endl;
            found = true;
            fromSeqNum = toSeqNum;

            if (beforeEnd)
                iter->second.beginSeqNum = toSeqNum;
        }
    }

    ASSERT(fromSeqNum == toSeqNum);

    if (!found) {
        EV_DEBUG << "Not found enqueueSentData(" << fromSeqNum << ", " << toSeqNum << ")\nThe Queue is:\n" << detailedInfo();
    }

    ASSERT(found);

    //begin = rexmitQueue.front().beginSeqNum;
    //end = rexmitQueue.back().endSeqNum;
    begin = rexmitMap.begin()->second.beginSeqNum;

    if(begin > end){ //if new begin is greater than old end, then the seq num has exceeded 4294967295
        end = (--rexmitMap.end())->second.endSeqNum;
    }
    end = (--rexmitMap.end())->second.endSeqNum;
    //std::cout << "\n NEW BEGIN: " << begin << endl;
    //std::cout << "\n NEW END: " << end << endl;
    // TESTING queue:
    //ASSERT(checkQueue());

//    tcpEV << "rexmitQ: rexmitQLength=" << getQueueLength() << "\n";
}

bool TcpSackRexmitQueue::checkQueue() const
{
    uint32_t b = begin;
    bool f = true;

    for (const auto& elem : rexmitQueue) {
        f = f && (b == elem.beginSeqNum);
        f = f && seqLess(elem.beginSeqNum, elem.endSeqNum);
        b = elem.endSeqNum;
    }

    f = f && (b == end);

    if (!f) {
        EV_DEBUG << "Invalid Queue\nThe Queue is:\n" << detailedInfo();
    }

    return f;
}

void TcpSackRexmitQueue::setSackedBit(uint32_t fromSeqNum, uint32_t toSeqNum)
{
    if (seqLess(fromSeqNum, begin))
        fromSeqNum = begin;

    ASSERT(seqLess(fromSeqNum, end));
    ASSERT(seqLess(begin, toSeqNum) && seqLE(toSeqNum, end));
    ASSERT(seqLess(fromSeqNum, toSeqNum));

    bool found = false;

    if (!rexmitMap.empty()) {
        //auto i = rexmitQueue.begin();

        //while (i != rexmitQueue.end() && seqLE(i->endSeqNum, fromSeqNum))
        //    i++;
        auto iter = rexmitMap.upper_bound(fromSeqNum);

        ASSERT(iter != rexmitMap.end() && seqLE(iter->second.beginSeqNum, fromSeqNum) && seqLess(fromSeqNum, iter->second.endSeqNum));
        //ASSERT(i != rexmitQueue.end() && seqLE(i->beginSeqNum, fromSeqNum) && seqLess(fromSeqNum, i->endSeqNum));

        //std::cout << "\n SET SACKET BIT" << endl;
        //std::cout << "\n fromSeqNum " << fromSeqNum << endl;
        //std::cout << "\n toSeqNum " << toSeqNum << endl;
        //std::cout << "\n i->beginSeqNum " << i->beginSeqNum << endl;
        //std::cout << "\n i->endSeqNum " << i->endSeqNum << endl;
        bool firstTest = seqGreater(fromSeqNum, iter->second.beginSeqNum);
        bool secondTest = seqLess(toSeqNum, iter->second.endSeqNum);
        if (iter->second.beginSeqNum != fromSeqNum) {
            if(seqGreater(fromSeqNum, iter->second.beginSeqNum) && seqLE(toSeqNum, iter->second.endSeqNum)){ //Do not forget case where it is last block (toSeqNum == i->endSeqNum)
                //std::cout << "\n FOUND BLOCK TO BE SACKED, WITHIN LARGER BLOCK" << endl;
                uint32_t oldRegionEnd = iter->second.endSeqNum;

//                std::cout << "\n BEFORE ADD: " << endl;
//                for(auto it = rexmitMap.cbegin(); it != rexmitMap.cend(); ++it)
//                {
//                    std::cout << "KEY: "<< it->first << "\n";
//                    std::cout << "beginSeqNum" << it->second.beginSeqNum << "\n";
//                    std::cout << "endSeqNum" << it->second.endSeqNum << "\n";
//                    std::cout << "sacked" << it->second.sacked << "\n";
//                    std::cout << "rexmitted" << it->second.rexmitted << "\n";
//                }

                Region startRegion;
                startRegion.beginSeqNum = iter->second.beginSeqNum;
                startRegion.endSeqNum = fromSeqNum;
                startRegion.sacked = iter->second.sacked;
                startRegion.rexmitted = iter->second.rexmitted;
                startRegion.numOfDiscontiguousSacks = iter->second.numOfDiscontiguousSacks;

                rexmitMap.erase(iter->second.endSeqNum);
                //i->endSeqNum = fromSeqNum;

                rexmitMap.insert({fromSeqNum, startRegion});

                Region region;
                region.beginSeqNum = fromSeqNum;
                region.endSeqNum = toSeqNum;
                region.sacked = true;
                region.rexmitted = false;
                region.numOfDiscontiguousSacks = startRegion.numOfDiscontiguousSacks;
                //iter++;                                                                     //TODO Fix this error
                //rexmitQueue.insert(i,region);
                rexmitMap.insert({toSeqNum, region});

                Region endRegion;
                endRegion.beginSeqNum = toSeqNum;
                endRegion.endSeqNum = oldRegionEnd;
                endRegion.sacked = false;
                endRegion.rexmitted = false;
                endRegion.numOfDiscontiguousSacks = startRegion.numOfDiscontiguousSacks;
                //rexmitQueue.insert(i,endRegion);
                rexmitMap.insert({oldRegionEnd, endRegion});

                //get start seqNum of large block
                //get end seqNum of large black
                //
            }
            else{
                //Gap in queue (should rarely if at all occur)
                Region region = iter->second;
                region.endSeqNum = fromSeqNum;
                //rexmitQueue.insert(i, region);
                rexmitMap.insert({region.endSeqNum, region}); //TODO potentially fix
                //i->beginSeqNum = fromSeqNum;
            }
        }

        while (iter != rexmitMap.end() && seqLE(iter->second.endSeqNum, toSeqNum)) {  //Most common case. Should split existing large region if it exists into smaller region
            if (seqGE(iter->second.beginSeqNum, fromSeqNum)) { // Search region in queue!
                found = true;
                iter->second.sacked = true; // set sacked bit
                //std::cout << "\n Found block, setting sacked bit!" << endl;
                //std::cout << "\n"<<  detailedInfo() << endl;
            }

            iter++;
        }

        if (iter != rexmitMap.end() && seqLess(iter->second.beginSeqNum, toSeqNum) && seqLess(toSeqNum, iter->second.endSeqNum)) { //Edge case, rarely happens ignore for now
            Region region = iter->second;
            //std::cout << "\n ALTERNATE Found block, setting sacked bit!" << endl;
            region.endSeqNum = toSeqNum;
            region.sacked = true;
            //rexmitQueue.insert(i, region);
            rexmitMap.insert({region.endSeqNum, region});
            //i->beginSeqNum = toSeqNum; //TODO MAYBE FIX
        }
    }

    if (!found)
        EV_DETAIL << "FAILED to set sacked bit for region: [" << fromSeqNum << ".." << toSeqNum << "). Not found in retransmission queue.\n";

    //ASSERT(checkQueue());
}

bool TcpSackRexmitQueue::getSackedBit(uint32_t seqNum) const
{
    ASSERT(seqLE(begin, seqNum) && seqLE(seqNum, end));

    //RexmitQueue::const_iterator i = rexmitQueue.begin();

    if (end == seqNum)
        return false;

    auto iter = rexmitMap.upper_bound(seqNum);
//    while (i != rexmitQueue.end() && seqLE(i->endSeqNum, seqNum))
//        i++;

    ASSERT((iter != rexmitMap.end()) && seqLE(iter->second.beginSeqNum, seqNum) && seqLess(seqNum, iter->second.endSeqNum));

    return iter->second.sacked;
}

uint32_t TcpSackRexmitQueue::getHighestSackedSeqNum() const
{
    for (auto iter = rexmitMap.rbegin(); iter != rexmitMap.rend(); iter++) {
        if (iter->second.sacked)
            return iter->second.endSeqNum;
    }

    return begin;
}

uint32_t TcpSackRexmitQueue::getHighestRexmittedSeqNum() const
{
    for (auto iter = rexmitMap.rbegin(); iter != rexmitMap.rend(); iter++) {
        if (iter->second.rexmitted)
            return iter->second.endSeqNum;
    }

    return begin;
}

uint32_t TcpSackRexmitQueue::checkRexmitQueueForSackedOrRexmittedSegments(uint32_t fromSeqNum) const
{
    ASSERT(seqLE(begin, fromSeqNum) && seqLE(fromSeqNum, end));

    if (rexmitMap.empty() || (end == fromSeqNum))
        return 0;

    //RexmitQueue::const_iterator i = rexmitQueue.begin();
    uint32_t bytes = 0;

    //while (i != rexmitQueue.end() && seqLE(i->endSeqNum, fromSeqNum))
    //    i++;
    auto iter = rexmitMap.upper_bound(fromSeqNum);
    while (iter != rexmitMap.end() && ((iter->second.sacked || iter->second.rexmitted))) {
        ASSERT(seqLE(iter->second.beginSeqNum, fromSeqNum) && seqLess(fromSeqNum, iter->second.endSeqNum));

        bytes += (iter->second.endSeqNum - fromSeqNum);
        fromSeqNum = iter->second.endSeqNum;
        iter++;
    }

    return bytes;
}

void TcpSackRexmitQueue::resetSackedBit()
{
    for (auto& elem : rexmitMap)
        elem.second.sacked = false; // reset sacked bit
}

void TcpSackRexmitQueue::resetRexmittedBit()
{
    for (auto& elem : rexmitMap)
        elem.second.rexmitted = false; // reset sacked bit
}

uint32_t TcpSackRexmitQueue::getTotalAmountOfSackedBytes() const
{
    uint32_t bytes = 0;

//    for (const auto& elem : rexmitQueue) {
//        if (elem.sacked)
//            bytes += (elem.endSeqNum - elem.beginSeqNum);
//    }
    for (const auto& elem : rexmitMap) {
        if (elem.second.sacked)
            bytes += (elem.first - elem.second.beginSeqNum);
    }

    return bytes;
}

uint32_t TcpSackRexmitQueue::getAmountOfSackedBytes(uint32_t fromSeqNum)// const
{
    ASSERT(seqLE(begin, fromSeqNum) && seqLE(fromSeqNum, end));

    uint32_t bytes = 0;
//    auto imap = rexmitMap.rbegin();
//
//    std::cout << "\n rend  beginSeqNum: " << rexmitMap.rend()->second.beginSeqNum << endl;
//    for (; imap != rexmitMap.rend()++ && seqLE(fromSeqNum, imap->second.beginSeqNum); imap++) {
//        if (imap->second.sacked){
//            bytes += (imap->first - imap->second.beginSeqNum);
//        }
//    }
//    if (i != rexmitQueue.rend()
//        && seqLess(i->beginSeqNum, fromSeqNum) && seqLess(fromSeqNum, i->endSeqNum) && i->sacked)
//    {
//        bytes += (i->endSeqNum - fromSeqNum);
//    }
    std::chrono::steady_clock::time_point beginBound = std::chrono::steady_clock::now();
    auto iter = rexmitMap.upper_bound(fromSeqNum);
    std::chrono::steady_clock::time_point endBound = std::chrono::steady_clock::now();
    boundTimeCheckTwo += std::chrono::duration_cast<std::chrono::nanoseconds>(endBound - beginBound).count();

    std::chrono::steady_clock::time_point beginWhile = std::chrono::steady_clock::now();
    while (iter != rexmitMap.end()) {
        if (iter->second.sacked){
            bytes += (iter->first - iter->second.beginSeqNum);
        }
        iter++;
    }
    std::chrono::steady_clock::time_point endWhile = std::chrono::steady_clock::now();
    whileTimeCheckTwo += std::chrono::duration_cast<std::chrono::nanoseconds>(endWhile - beginWhile).count();
    return bytes;
}

uint32_t TcpSackRexmitQueue::getNumOfDiscontiguousSacks(uint32_t fromSeqNum)//const
{
    ASSERT(seqLE(begin, fromSeqNum) && seqLE(fromSeqNum, end));

    if (rexmitMap.empty() || (fromSeqNum == end))
        return 0;

    //RexmitQueue::const_iterator i = rexmitQueue.begin();
    uint32_t counter = 0;
//
//    while (i != rexmitQueue.end() && seqLE(i->endSeqNum, fromSeqNum)) // search for seqNum
//        i++;
    std::chrono::steady_clock::time_point beginBound = std::chrono::steady_clock::now();
    auto iter = rexmitMap.upper_bound(fromSeqNum);
    std::chrono::steady_clock::time_point endBound = std::chrono::steady_clock::now();
    boundTimeCheckOne += std::chrono::duration_cast<std::chrono::nanoseconds>(endBound - beginBound).count();
    // search for discontiguous sacked regions
    bool prevSacked = false;

    int countTest = 0;
    std::chrono::steady_clock::time_point beginWhile = std::chrono::steady_clock::now();
    while (iter != rexmitMap.end()) {
        countTest++;
        if (iter->second.sacked && !prevSacked){
            counter++;
        }
        if(counter >= 3){
            //std::cout << "\n COUNTER: " << counter << endl;
            std::chrono::steady_clock::time_point endWhile = std::chrono::steady_clock::now();
            whileTimeCheckOne += std::chrono::duration_cast<std::chrono::nanoseconds>(endBound - beginBound).count();
            break;
        }
        prevSacked = iter->second.sacked;
        iter++;
    }
    //std::cout << "\n COUNT TEST: " << countTest << endl;
    //std::cout << "\n" << detailedInfo() << endl;
    std::chrono::steady_clock::time_point endWhile = std::chrono::steady_clock::now();
    whileTimeCheckOne += std::chrono::duration_cast<std::chrono::nanoseconds>(endWhile - beginWhile).count();


//    while (i != rexmitQueue.end()) {
//        if (i->sacked && !prevSacked)
//            counter++;
//
//        prevSacked = i->sacked;
//        i++;
//    }

    return counter;
}

void TcpSackRexmitQueue::checkSackBlock(uint32_t fromSeqNum, uint32_t& length, bool& sacked, bool& rexmitted) const
{
    ASSERT(seqLE(begin, fromSeqNum) && seqLess(fromSeqNum, end));

    //RexmitQueue::const_iterator i = rexmitQueue.begin();
    int count = 0;
//    while (i != rexmitQueue.end() && seqLE(i->endSeqNum, fromSeqNum)){ // search for seqNum
//        i++;
//        count++;
//    }

    auto iter = rexmitMap.upper_bound(fromSeqNum);
//    std::cout << "\n Trying to find sackblock with: " << fromSeqNum << endl;
//    std::cout << "\n checkSackBlock count: " << count << endl;
//    std::cout << "\n rexmitMap count: " << std::distance(rexmitMap.begin(),rexmitMap.find(fromSeqNum)) << endl;
//    std::cout << "\n iter begin: " <<  iter->second.beginSeqNum << endl;
//    std::cout << "\n iter end: " << iter->second.endSeqNum << endl;
//    std::cout << detailedInfo() << endl;
//    for(auto it = rexmitMap.cbegin(); it != rexmitMap.cend(); ++it)
//    {
//        std::cout << it->second.beginSeqNum << " " << it->second.endSeqNum << "(" << it->first << ")" << "\n";
//    }
    ASSERT(iter != rexmitMap.end());
    ASSERT(seqLE(iter->second.beginSeqNum, fromSeqNum) && seqLess(fromSeqNum, iter->second.endSeqNum));

    length = (iter->second.endSeqNum - fromSeqNum);
    sacked = iter->second.sacked;
    rexmitted = iter->second.rexmitted;

//    ASSERT(i != rexmitQueue.end());
//    ASSERT(seqLE(i->beginSeqNum, fromSeqNum) && seqLess(fromSeqNum, i->endSeqNum));
//
//    length = (i->endSeqNum - fromSeqNum);
//    sacked = i->sacked;
//    rexmitted = i->rexmitted;
}

} // namespace tcp

} // namespace inet
