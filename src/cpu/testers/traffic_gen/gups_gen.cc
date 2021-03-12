/*
 * Copyright (c) 2021 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cpu/testers/traffic_gen/gups_gen.hh"

#include <cstring>
#include <string>

#include "base/random.hh"
#include "debug/GUPSGen.hh"
#include "sim/sim_exit.hh"

#define POLY 0x0000000000000007UL
#define PERIOD 1317624576693539401L


GUPSGen::GUPSGen(const GUPSGenParams& params):
    ClockedObject(params),
    nextSendEvent([this]{ sendNextReq(); }, name()),
    system(params.system),
    requestorId(system->getRequestorId(this)),
    port(name() + ".port", this),
    blockSize(params.block_size),
    startAddr(params.start_addr),
    memSize(params.mem_size),
    reqQueueSize(params.request_queue_size),
    doGenerate(true),
    doneReading(false),
    onTheFlyRequests(0),
    elementSize(8),
    stats(this)
{}

Port&
GUPSGen::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port"){
        return ClockedObject::getPort(if_name, idx);
    }
    else{
        return port;
    }
}

void
GUPSGen::init()
{

    tableSize = memSize / sizeof(uint64_t);
    numUpdates = 4 * tableSize;

    for (int i = 0; i < 128; i++)
    {
        randomSeeds[i] = randomNumberGenerator((numUpdates / 128) * i);
    }

}

void
GUPSGen::startup()
{
    uint64_t stride_size = blockSize / sizeof(uint64_t);

    for (uint64_t start_index = 0; start_index < tableSize;
                                start_index += stride_size)
    {
        uint8_t write_data[blockSize];
        for (uint64_t offset = 0; offset < stride_size; offset++)
        {
            uint64_t value = start_index + offset;
            std::memcpy(write_data + offset * sizeof(uint64_t),
                        &value, sizeof(uint64_t));
        }
        Addr addr = indexToAddr(start_index);
        PacketPtr pkt = getWritePacket(addr, blockSize, write_data);
        port.sendFunctionalPacket(pkt);
        delete pkt;
    }

    schedule(nextSendEvent, nextCycle());

}

uint64_t
GUPSGen::randomNumberGenerator(int64_t n)
{
    int i, j;
    uint64_t m2[64];
    uint64_t temp, ran;

    while (n < 0) n += PERIOD;
    while (n > PERIOD) n -= PERIOD;
    if (n == 0) return 0x1;

    temp = 0x1;
    for (i = 0; i < 64; i++)
    {
        m2[i] = temp;
        temp = (temp << 1) ^ ((int64_t) temp < 0 ? POLY : 0);
        temp = (temp << 1) ^ ((int64_t) temp < 0 ? POLY : 0);
    }

    for (i = 62; i >= 0; i--)
    {
        if ((n >> i) & 1)
            break;
    }

    ran = 0x2;
    while (i > 0)
    {
        temp = 0;
        for (j = 0; j < 64; j++)
            if ((ran >> j) & 1)
            temp ^= m2[j];
        ran = temp;
        i -= 1;
        if ((n >> i) & 1)
            ran = (ran << 1) ^ ((int64_t) ran < 0 ? POLY : 0);
    }

    return ran;
}

Addr
GUPSGen::indexToAddr (uint64_t index)
{
    Addr ret = index * elementSize + startAddr;

    return ret;
}

PacketPtr
GUPSGen::getReadPacket(Addr addr, unsigned int size)
{
    RequestPtr req = std::make_shared<Request>(addr, size,
                                            0, requestorId);
    // Dummy PC to have PC-based prefetchers latch on; get entropy into higher
    // bits
    req->setPC(((Addr)requestorId) << 2);

    // Embed it in a packet
    PacketPtr pkt = new Packet(req, MemCmd::ReadReq);

    uint8_t* pkt_data = new uint8_t[req->getSize()];
    pkt->dataDynamic(pkt_data);

    return pkt;
}

PacketPtr
GUPSGen::getWritePacket(Addr addr, unsigned int size, uint8_t *data)
{
    RequestPtr req = std::make_shared<Request>(addr, size, 0,
                                               requestorId);
    // Dummy PC to have PC-based prefetchers latch on; get entropy into higher
    // bits
    req->setPC(((Addr)requestorId) << 2);

    // Embed it in a packet
    PacketPtr pkt = new Packet(req, MemCmd::WriteReq);

    uint8_t* pkt_data = new uint8_t[req->getSize()];
    pkt->dataDynamic(pkt_data);

    // std::fill_n(pkt_data, req->getSize(), (uint8_t)requestorId);
    std::memcpy(pkt_data, data, req->getSize());

    return pkt;
}

void
GUPSGen::handleResponse(PacketPtr pkt)
{
    onTheFlyRequests--;
    if (pkt->isWrite()) {
        DPRINTF(GUPSGen, "handleResponse: received a write resp.\n");
        stats.totalUpdates++;

        stats.totalWrites++;
        stats.totalBytesWritten += elementSize;
        stats.totalWriteLat += curTick() - exitTimes[pkt->req];

        exitTimes.erase(pkt->req);
        delete pkt;
    }
    else {
        DPRINTF(GUPSGen, "handleResponse: received a read resp.\n");

        stats.totalReads++;
        stats.totalBytesRead += elementSize;
        stats.totalReadLat += curTick() - exitTimes[pkt->req];

        exitTimes.erase(pkt->req);

        responsePool.push(pkt);
    }
    if (doneReading && requestPool.empty() && onTheFlyRequests == 0){
        stats.totalTicks = curTick();
        exitSimLoop(name() + " has encountered the exit state and will "
                "terminate the simulation.\n");
        return;
    }
}

void GUPSGen::wakeUp()
{
    if (!nextSendEvent.scheduled() && !requestPool.empty())
    {
        schedule(nextSendEvent, nextCycle());
    }
}

void
GUPSGen::sendNextReq() {

    static uint64_t iterations = 0;
    static int rIndex = 0;

    if (doGenerate) {
        if (!responsePool.empty()) {
            PacketPtr pkt = responsePool.front();
            responsePool.pop();

            uint64_t *updated_value = pkt->getPtr<uint64_t>();
            *updated_value ^= updateTable[pkt->req];
            updateTable.erase(pkt->req);

            uint8_t write_data[elementSize];
            std::memcpy(write_data, updated_value, elementSize);
            Addr addr = pkt->getAddr();
            delete pkt;

            pkt = getWritePacket(addr, elementSize, write_data);
            requestPool.push(pkt);
        }
        else if (!doneReading) {
            assert (rIndex < 128);

            randomSeeds[rIndex] = (randomSeeds[rIndex] << 1) ^
                            ((int64_t) randomSeeds[rIndex] < 0 ? POLY : 0);
            uint64_t value = randomSeeds[rIndex];
            uint64_t index = randomSeeds[rIndex] & (tableSize - 1);
            Addr addr = indexToAddr(index);
            PacketPtr pkt = getReadPacket(addr, sizeof(uint64_t));
            updateTable[pkt->req] = value;
            requestPool.push(pkt);

            if (requestPool.size() == reqQueueSize) {
                doGenerate = false;
            }

            rIndex++;
            if (rIndex >= 128) {
                rIndex = 0;
                iterations++;
                if (iterations >= numUpdates / 128) {
                    DPRINTF(GUPSGen, "sendNextReq: Done creating reads.\n");
                    doneReading = true;
                }
            }
        }
    }

    if (!requestPool.empty() && !port.blocked())
    {
        PacketPtr pkt = requestPool.front();

        exitTimes[pkt->req] = curTick();
        if (pkt->isWrite()) {
            DPRINTF(GUPSGen, "sendNextReq: Sent write pkt, pkt->addr_range: "
                            "%s, pkt->data: %lu.\n",
                            *pkt->getPtr<uint64_t>(),
                            pkt->getAddrRange().to_string());
        }
        else {
            DPRINTF(GUPSGen, "sendNextReq: Sent read pkt, pkt->addr_range: "
                            "%s.\n", pkt->getAddrRange().to_string());
        }
        port.sendTimingPacket(pkt);
        onTheFlyRequests++;
        requestPool.pop();

        if (requestPool.size() < reqQueueSize) {
            doGenerate = true;
        }
    }

    if (!nextSendEvent.scheduled() && (doGenerate ||
        (!port.blocked() && !requestPool.empty()))) {
        schedule(nextSendEvent, nextCycle());
    }

    return;
}


void
GUPSGen::GenPort::sendTimingPacket(PacketPtr pkt)
{
    panic_if(_blocked, "Should never try to send if blocked MemSide!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        DPRINTF(GUPSGen, "GenPort::sendTimingPacket: Packet blocked\n");
        blockedPacket = pkt;
        _blocked = true;
    }
    else {
        DPRINTF(GUPSGen, "GenPort::sendTimingPacket: Packet sent\n");
    }

}

void
GUPSGen::GenPort::sendFunctionalPacket(PacketPtr pkt)
{
    sendFunctional(pkt);
}

void
GUPSGen::GenPort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    DPRINTF(GUPSGen, "GenPort::recvReqRetry: Received a retry.\n");

    assert(_blocked && (blockedPacket != nullptr));
    // Try to resend it. It's possible that it fails again.
    _blocked = false;
    sendTimingPacket(blockedPacket);
    if (!_blocked){
        blockedPacket = nullptr;
    }

    owner->wakeUp();
}

bool
GUPSGen::GenPort::recvTimingResp(PacketPtr pkt)
{
    owner->handleResponse(pkt);
    return true;
}

GUPSGen::GUPSGenStat::GUPSGenStat(GUPSGen* parent):
    Stats::Group(parent),
    ADD_STAT(totalTicks,
        "Total number of ticks for this generator to generate requests"),
    ADD_STAT(totalUpdates,
        "Total number of updates the generator made in the memory"),
    ADD_STAT(GUPS,
        "Number of updates per second"),
    ADD_STAT(totalReads,
        "Total number of read requests"),
    ADD_STAT(totalBytesRead,
        "Total number of bytes read"),
    ADD_STAT(avgReadBW,
        "Average read bandwidth received from memory"),
    ADD_STAT(totalReadLat,
        "Total latency of read requests."),
    ADD_STAT(avgReadLat,
        "Average latency for read requests"),
    ADD_STAT(totalWrites,
        "Total number of read requests"),
    ADD_STAT(totalBytesWritten,
        "Total number of bytes read"),
    ADD_STAT(avgWriteBW,
        "Average read bandwidth received from memory"),
    ADD_STAT(totalWriteLat,
        "Total latency of write requests."),
    ADD_STAT(avgWriteLat,
        "Average latency for read requests")
{}

void
GUPSGen::GUPSGenStat::regStats(){
    GUPS.precision(8);
    avgReadBW.precision(2);
    avgReadLat.precision(2);
    avgWriteBW.precision(2);
    avgWriteLat.precision(2);

    GUPS = (totalUpdates * 1e3) / totalTicks;

    avgReadBW = (totalBytesRead * 1e12) / totalTicks;
    avgReadLat = (totalReadLat * 1e-3) / totalReads;

    avgWriteBW = (totalBytesWritten * 1e12) / totalTicks;
    avgWriteLat = (totalWriteLat * 1e-3) / totalWrites;
}