/*
 * Copyright (c) 2017 Jason Lowe-Power
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
#include <string>
#include "mem/mem_scheduler.hh"

#include "base/trace.hh"
#include "debug/MemScheduler.hh"

MemScheduler::MemScheduler(const MemSchedulerParams &params):
    ClockedObject(params),
    nextReqEvent([this]{ processNextReqEvent(); }, name()),
    nextRespEvent([this]{ processNextRespEvent(); }, name()),
    readBufferSize(params.read_buffer_size),
    writeBufferSize(params.write_buffer_size),
    respBufferSize(params.resp_buffer_size),
    nMemPorts(params.port_mem_side_connection_count),
    nCpuPorts(params.port_cpu_side_connection_count)
{

    panic_if(readBufferSize == 0, "readBufferSize should be non-zero");
    panic_if(writeBufferSize == 0, "writeBufferSize "
                                    "should be non-zero");
    for (uint32_t i = 0; i < nCpuPorts; ++i){
        // CPUSidePort *port = new CPUSidePort(this->name() + ".cpu_side" +
        //                                     std::to_string(i), this);
        cpuPorts.emplace(name() + ".cpu_side" + std::to_string(i), this);
    }
    for (uint32_t i = 0; i < nMemPorts; ++i){
        // MemSidePort *port = new MemSidePort(this->name() + ".mem_side" +
        //                                     std::to_string(i), this);
        cpuPorts.emplace(name() + ".mem_side" + std::to_string(i), this);
    }
    currentReadEntry = readQueues.begin();
    currentWriteEntry = writeQueues.begin();
}

Port &
MemScheduler::getPort(const std::string &if_name, PortID idx)
{

    // This is the name from the Python SimObject declaration (MemScheduler.py)
    if (if_name == "mem_side" && idx < memPorts.size()) {
        return *memPorts[idx];
    } else if (if_name == "cpu_side") {
        return *cpuPorts[idx];
    } else {
        // pass it along to our super class
        return SimObject::getPort(if_name, idx);
    }
}
// TODO: Needs to be fixed
void
MemScheduler::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)){
        DPRINTF(MemScheduler, "sendPacket: Changing blockedPacket value, blockedPacket == nullptr: %d\n", blockedPacket == nullptr);
        blockedPacket = pkt;
        DPRINTF(MemScheduler, "sendPacket: Changed blockedPacket value, blockedPacket == nullptr: %d\n", blockedPacket == nullptr);
        blocked = true;
    }
}

AddrRangeList
MemScheduler::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}
//sendReqretry
void
MemScheduler::CPUSidePort::trySendRetry()
{
    if (needRetry) {
    // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(MemScheduler, "Sending retry req for %d\n", id);
        sendRetryReq();
    }
}

bool
MemScheduler::CPUSidePort::getBlocked(){
    return blocked;
}

bool
MemScheduler::CPUSidePort::getNeedRetry(){
    return needRetry;
}

PacketPtr
MemScheduler::CPUSidePort::getBlockedPkt(){
    return blockedPacket;
}

void
MemScheduler::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    // Just forward to the memobj.
    return owner->handleFunctional(pkt);
}

bool
MemScheduler::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    // Just forward to the memobj.
    if (!owner->handleRequest(this, pkt)) {
        DPRINTF(MemScheduler, "recvTimingReq: handleRequest returned false.\n");
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
MemScheduler::CPUSidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    DPRINTF(MemScheduler, "recvRespRetry: Changing blockedPacket value, blockedPacket == nullptr: %d\n", blockedPacket == nullptr);
    blockedPacket = nullptr;
    DPRINTF(MemScheduler, "recvRespRetry: Changed blockedPacket value, blockedPacket == nullptr: %d\n", blockedPacket == nullptr);

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void
MemScheduler::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the memobj is blocking.
    panic_if(blocked == true, "Should never try to send if blocked MemSide!");
    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
        DPRINTF(MemScheduler, "Setting blocked to true on port %s\n", this->name());
        blocked = true;
    }
    DPRINTF(MemScheduler, "Send packet 111\n");
}

bool
MemScheduler::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the memobj.
    return owner->handleResponse(pkt);
}

bool MemScheduler::MemSidePort::getBlocked()
{
    return blocked;
}
void
MemScheduler::MemSidePort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blocked == true && blockedPacket != nullptr);
    DPRINTF(MemScheduler, "Received ReqRetry\n");
    // Try to resend it. It's possible that it fails again.
    blocked = false;
    sendPacket(blockedPacket);
    blockedPacket = nullptr;

    owner->wakeUp();
}

void
MemScheduler::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
MemScheduler::handleRequest(CPUSidePort *port, PacketPtr pkt)
{
    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller\n");

    uint32_t requestorId = pkt->req->requestorId();

    std::unordered_map<RequestorID, CPUSidePort*>::const_iterator ret = routingTable.find(requestorId);
    if (ret == routingTable.end())
        routingTable[requestorId] = port;

    std::unordered_map<RequestorID, bool>::const_iterator rit = readBlocked.find(requestorId);
    if (rit == readBlocked.end())
        readBlocked[requestorId] = false;

    std::unordered_map<RequestorID, bool>::const_iterator wit = writeBlocked.find(requestorId);
    if (wit == writeBlocked.end())
        writeBlocked[requestorId] = false;

    if (pkt->isRead() && readBlocked[requestorId])
        return false;

    if (pkt->isWrite() && writeBlocked[requestorId])
        return false;

    DPRINTF(MemScheduler, "Got request for addr %#x for requestorId: %d\n", pkt->getAddr(), requestorId);

    if (pkt->isRead()){
        DPRINTF(MemScheduler, "handleRequest: pushing read request, readQueue[%d].size(): %d\n", requestorId, readQueues[requestorId].size());
        readQueues[requestorId].push(pkt);
        DPRINTF(MemScheduler, "handleRequest: pushed read request, readQueue[%d].size(): %d\n", requestorId, readQueues[requestorId].size());
        if(readQueues[requestorId].size() == readBufferSize)
            readBlocked[requestorId] = true;
    }
    if (pkt->isWrite()){
        DPRINTF(MemScheduler, "handleRequest: pushing write request, writeQueue[%d].size(): %d\n", requestorId, writeQueues[requestorId].size());
        writeQueues[requestorId].push(pkt);
        DPRINTF(MemScheduler, "handleRequest: pushed write request, writeQueue[%d].size(): %d\n", requestorId, writeQueues[requestorId].size());
        if(writeQueues[requestorId].size() == writeBufferSize)
            writeBlocked[requestorId] = true;
    }
    if (!nextReqEvent.scheduled()){
        if (pkt->isRead())
            currentReadEntry = readQueues.find(requestorId);
        if (pkt->isWrite())
            currentWriteEntry = writeQueues.find(requestorId);
        DPRINTF(MemScheduler, "Scheduling nextReqEvent in handleRequest\n");
        schedule(nextReqEvent, curTick());
    }
    return true;
}

MemScheduler::MemSidePort*
MemScheduler::findMemoryPort(PacketPtr pkt){
    std::cout << "Entering findMemoryPort" << std::endl;
    const Addr base_addr = pkt->getAddr();
    std::cout << "base_addr: " << base_addr << std::endl;
    int i = 0;
    for (auto memPort : memPorts){
        std::cout << "i: " << i++ << ", ";
        for (auto addr_range : memPort->getAddrRanges()){
            std::cout << "Packet address range start: " << addr_range.start()
            << ", Packet address range end: " << addr_range.end() << std::endl;
            if (addr_range.contains(base_addr)){

                std::cout << "Exiting findMemoryPort" << std::endl;
                return memPort;
            }
        }
    }
    return NULL;
}

void
MemScheduler::processNextReqEvent(){
    std::unordered_map<RequestorID, std::queue<PacketPtr> >::iterator \
                                        initialEntry = currentReadEntry;
    std::queue<CPUSidePort*> retryPorts;
    // DPRINTF(MemScheduler, "processNextReqEvent: just entered\n");
    while (true){
        if (!currentReadEntry->second.empty()){
            PacketPtr pkt = currentReadEntry->second.front();
            MemSidePort *memPort = findMemoryPort(pkt);
            if (memPort->getBlocked() == false){
                memPort->sendPacket(pkt);
                RequestorID requestorId = currentReadEntry->first;
                DPRINTF(MemScheduler, "processNextReqEvent, popping read request, readQueue[%d].size(): %d\n", requestorId, readQueues[requestorId].size());
                currentReadEntry->second.pop();
                DPRINTF(MemScheduler, "processNextReqEvent, popped read request, readQueue[%d].size(): %d\n", requestorId, readQueues[requestorId].size());
                CPUSidePort *cpuPort = routingTable[requestorId];
                if((currentReadEntry->second.size() == readBufferSize - 1) && cpuPort->getNeedRetry()){
                    retryPorts.push(cpuPort);
                }
                DPRINTF(MemScheduler, "Processing request event, requestorId: %d\n", requestorId);
                currentReadEntry++;
                if (currentReadEntry == readQueues.end())
                    currentReadEntry = readQueues.begin();
                if (initialEntry == currentReadEntry){
                    DPRINTF(MemScheduler, "All available queues observed.\n");
                    break;
                }
            }
            else{
                currentReadEntry++;
                if (currentReadEntry == readQueues.end())
                    currentReadEntry = readQueues.begin();
                if (initialEntry == currentReadEntry){
                    DPRINTF(MemScheduler, "All mem ports blocked.\n");
                    while(!retryPorts.empty()){
                        CPUSidePort *port = retryPorts.front();
                        RequestorID requestorId;
                        for (auto entry : routingTable){
                            if (entry.second == port){
                                requestorId = entry.first;
                                break;
                            }
                        }
                        DPRINTF(MemScheduler, "Sending retry to requestorId: %d\n", requestorId);
                        readBlocked[requestorId] = false;
                        port->trySendRetry();
                        retryPorts.pop();
                    }
                    break;
                }
            }
        } else{
            currentReadEntry++;
            if (currentReadEntry == readQueues.end())
                currentReadEntry = readQueues.begin();
            if (initialEntry == currentReadEntry){
                DPRINTF(MemScheduler, "All read queues empty.\n");
                while(!retryPorts.empty()){
                    CPUSidePort *port = retryPorts.front();
                    RequestorID requestorId;
                    for (auto entry : routingTable){
                        if (entry.second == port){
                            requestorId = entry.first;
                            break;
                        }
                    }
                    DPRINTF(MemScheduler, "Sending retry to requestorId: %d\n", requestorId);
                    readBlocked[requestorId] = false;
                    port->trySendRetry();
                    retryPorts.pop();
                }
                return;
            }
        }
    }
    // currentReadEntry++;
    // if (currentReadEntry == readQueues.end()){
    //     currentReadEntry = readQueues.begin();
    // }
    // DPRINTF(MemScheduler, "processNextReqEvent: finished while loop\n");
    for (auto it : readQueues){
        if (!(it.second.empty())){
            if (!nextReqEvent.scheduled()){
                DPRINTF(MemScheduler, "Scheduling nextReqEvent in processNextReqEvent\n");
                schedule(nextReqEvent, curTick() + 500);
            }
            break;
        }
    }

    // if (sendRetry){
    //     DPRINTF(MemScheduler, "Sending retry to requestorId: %d\n", requestorId);
    //     readBlocked[requestorId] = false;
    //     cpuPort->trySendRetry();
    //     sendRetry = false;
    // }
    while(!retryPorts.empty()){
        CPUSidePort *port = retryPorts.front();
        RequestorID requestorId;
        for (auto entry : routingTable){
            if (entry.second == port){
                requestorId = entry.first;
                break;
            }
        }
        DPRINTF(MemScheduler, "Sending retry to requestorId: %d\n", requestorId);
        readBlocked[requestorId] = false;
        port->trySendRetry();
        retryPorts.pop();
    }
}

void
MemScheduler::processNextRespEvent(){
    std::unordered_map<RequestorID, std::queue<PacketPtr> >::iterator \
                                        initialEntry = currentRespEntry;
    CPUSidePort *port;
    PacketPtr pkt;
    RequestorID requestorId;

    // DPRINTF(MemScheduler, "processNextReqEvent: just entered\n");
    while (true){
        if (!currentRespEntry->second.empty()){
            pkt = currentRespEntry->second.front();
            requestorId = pkt->req->requestorId();
            port = routingTable[requestorId];
            if (port->getBlocked() == false){
                port->sendPacket(pkt);
                DPRINTF(MemScheduler, "processNextRespEvent, popping resp, respQueue[%d].size(): %d\n", requestorId, respQueues[requestorId].size());
                currentRespEntry->second.pop();
                DPRINTF(MemScheduler, "processNextRespEvent, popped resp, respQueue[%d].size(): %d\n", requestorId, respQueues[requestorId].size());
            }
            else{
                currentRespEntry++;
                if (currentRespEntry == respQueues.end())
                    currentRespEntry = respQueues.begin();
                if (initialEntry == currentRespEntry){
                    DPRINTF(MemScheduler, "All cpu ports blocked.\n");
                    return;
                }
            }
        } else{
            currentRespEntry++;
            if (currentRespEntry == respQueues.end())
                currentRespEntry = respQueues.begin();
            if (initialEntry == currentRespEntry){
                DPRINTF(MemScheduler, "All resp queues empty.\n");
                return;
            }
        }
    }
    currentRespEntry++;
    if (currentRespEntry == respQueues.end()){
        currentRespEntry = respQueues.begin();
    }

    // for (auto it : respQueues){
    //     if (!(it.second.empty())){
    //         if (!nextRespEvent.scheduled()){
    //             DPRINTF(MemScheduler, "Scheduling nextRespEvent in processNextReqEvent\n");
    //             schedule(nextRespEvent, curTick() + 500);
    //         }
    //         break;
    //     }
    // }
    // return;
}

bool
MemScheduler::handleResponse(PacketPtr pkt)
{

    // TODO: This needs to be fixed with resp packet info.
    // panic_if(!pkt->isRead(), "Should only see read and writes at memory controller\n");

    uint32_t requestorId = pkt->req->requestorId();

    std::unordered_map<RequestorID, bool>::const_iterator rit = respBlocked.find(requestorId);
    if (rit == respBlocked.end())
        respBlocked[requestorId] = false;

    if (respBlocked[requestorId])
        return false;

    DPRINTF(MemScheduler, "Got response for addr %#x for requestorId: %d\n", pkt->getAddr(), requestorId);

    DPRINTF(MemScheduler, "handleResponse: pushing response, respQueue[%d].size(): %d\n", requestorId, respQueues[requestorId].size());
    respQueues[requestorId].push(pkt);
    DPRINTF(MemScheduler, "handleResponse: pushed response, respQueue[%d].size(): %d\n", requestorId, respQueues[requestorId].size());

    if(respQueues[requestorId].size() == respBufferSize)
        respBlocked[requestorId] = true;
    if (!nextRespEvent.scheduled()){
        currentRespEntry = respQueues.find(requestorId);
        DPRINTF(MemScheduler, "Scheduling nextReqEvent in handleRequest\n");
        schedule(nextRespEvent, curTick());
    }
    return true;
}

void
MemScheduler::handleFunctional(PacketPtr pkt)
{
    // Just pass this on to the memory side to handle for now.
    const Addr base_addr = pkt->getAddr();
    // Simply forward to the memory port
    for (auto memPort : memPorts)
        // AddrRangeList addr_range = memPort->getAddrRanges();
        for (auto addr_range : memPort->getAddrRanges())
            if (addr_range.start() <= base_addr &&
                    base_addr <= addr_range.end())
                memPort->sendFunctional(pkt);
}

AddrRangeList
MemScheduler::getAddrRanges() const
{
    DPRINTF(MemScheduler, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    AddrRangeList ret;
    // Simply forward to the memory port
    for (auto memPort : memPorts){
        // AddrRangeList addr_range = memPort->getAddrRanges();
        for (auto addr_range : memPort->getAddrRanges()){
            ret.push_back(addr_range);
        }
    }
    return ret;
}

void
MemScheduler::sendRangeChange()
{
    for (auto cpuPort : cpuPorts)
        cpuPort->sendRangeChange();
}

void
MemScheduler::wakeUp()
{
    if (!nextReqEvent.scheduled())
        schedule(nextReqEvent, curTick());
}
