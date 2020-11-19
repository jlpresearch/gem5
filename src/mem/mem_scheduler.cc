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
        cpuPorts.emplace_back(name() + ".cpu_side" + std::to_string(i), i, this);
        requestQueues.push_back(RequestQueue(readBufferSize, writeBufferSize, i));
        responseQueues.push_back(ResponseQueue(respBufferSize, i));
    }
    for (uint32_t i = 0; i < nMemPorts; ++i){
        memPorts.emplace_back(name() + ".mem_side" + std::to_string(i), i, this);
    }
    currentReadEntry = readQueues.begin();
    currentWriteEntry = writeQueues.begin();
}

Port &
MemScheduler::getPort(const std::string &if_name, PortID idx)
{

    // This is the name from the Python SimObject declaration (MemScheduler.py)
    if (if_name == "mem_side" && idx < memPorts.size()) {
        return memPorts[idx];
    } else if (if_name == "cpu_side") {
        return cpuPorts[idx];
    } else {
        // pass it along to our super class
        return SimObject::getPort(if_name, idx);
    }
}

void
MemScheduler::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(_blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)){
        DPRINTF(MemScheduler, "sendPacket: Changing blockedPacket value, blockedPacket == nullptr: %d\n", _blockedPacket == nullptr);
        _blockedPacket = pkt;
        DPRINTF(MemScheduler, "sendPacket: Changed blockedPacket value, blockedPacket == nullptr: %d\n", _blockedPacket == nullptr);
        _blocked = true;
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
    DPRINTF(MemScheduler, "Sending retry req for %d\n", portId());
    sendRetryReq();
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
    if (!owner->handleRequest(portId(), pkt)) {
        DPRINTF(MemScheduler, "recvTimingReq: handleRequest returned false.\n");
        return false;
    }
    return true;
}

void
MemScheduler::CPUSidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(_blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = _blockedPacket;
    DPRINTF(MemScheduler, "recvRespRetry: Changing blockedPacket value, blockedPacket == nullptr: %d\n", _blockedPacket == nullptr);
    _blockedPacket = nullptr;
    DPRINTF(MemScheduler, "recvRespRetry: Changed blockedPacket value, blockedPacket == nullptr: %d\n", _blockedPacket == nullptr);
    _blocked = false;
    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void
MemScheduler::MemSidePort::trySendRetry()
{
    DPRINTF(MemScheduler, "Sending retry resp for memPort[%d]\n", portId());
    sendRetryResp();
}

void
MemScheduler::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the memobj is blocking.
    panic_if(_blocked == true, "Should never try to send if blocked MemSide!");
    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
        DPRINTF(MemScheduler, "Setting blocked to true on port %s\n", this->name());
        _blocked = true;
    }
}

bool
MemScheduler::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the memobj.
    return owner->handleResponse(portId(), pkt);
}

void
MemScheduler::MemSidePort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    assert(_blocked == true && blockedPacket != nullptr);
    DPRINTF(MemScheduler, "Received ReqRetry\n");
    // Try to resend it. It's possible that it fails again.
    _blocked = false;
    sendPacket(blockedPacket);
    blockedPacket = nullptr;

    owner->wakeUp();
}

void
MemScheduler::MemSidePort::recvRangeChange()
{
    owner->recvRangeChange(_portId);
}

bool
MemScheduler::handleRequest(PortID portId, PacketPtr pkt)
{
       ///////////////////////////////////////////////////////////////////////
      /////////////////// TODO: adjust the schedule freq ////////////////////
     /////////////////// sendRetry respective queue = true /////////////////
    ///////////////////////////////////////////////////////////////////////
    DPRINTF(MemScheduler, "handleRequest: Got request from cpuPort[%d]\n", portId);
    auto queue = find_if(requestQueues.begin(), requestQueues.end(), [portId](RequestQueue &obj){return obj.cpuPortId == portId;});
    if (queue->blocked(pkt->isRead() && !pkt->isWrite())){
        queue->sendRetry = true;
        return false;
    }
    DPRINTF(MemScheduler, "handleRequest: Pushing request to queues, requestQueues[%d], size = %d\n", portId, queue->readQueue.size());
    queue->push(pkt);
    DPRINTF(MemScheduler, "handleRequest: Pushed request to queues, requestQueues[%d], size = %d\n", portId, queue->readQueue.size());
    if(!nextReqEvent.scheduled()){
        DPRINTF(MemScheduler, "handleRequest: Scheduling nextReqEvent in handleRequest\n");
        schedule(nextReqEvent, curTick() + 100);
    }
    return true;
}

void
MemScheduler::processNextReqEvent(){

    uint64_t minCheck = -1;
    DPRINTF(MemScheduler, "processNextReqEvent: Finding the least recently visited non-empty queue\n");
    for (auto &it : requestQueues){
        if ((it.timesChecked < minCheck) && (!it.emptyRead())){
            minCheck = it.timesChecked;
        }
    }
    auto queue = find_if(requestQueues.begin(), requestQueues.end(), [minCheck](RequestQueue &obj){
        return (obj.timesChecked == minCheck);
        });
    DPRINTF(MemScheduler, "processNextReqEvent: Least recently visited queue found, cpuPortId = %d, (timesChecked = %d) == (minChecked = %d)\n", queue->cpuPortId, queue->timesChecked, minCheck);
    PacketPtr pkt = queue->readQueue.front();
    AddrRange addr_range = pkt->getAddrRange();
    DPRINTF(MemScheduler, "processNextReqEvent: pkt->addr_range: %s\n", addr_range.to_string());
    PortID memPortId = memPortMap.contains(addr_range)->second;
    DPRINTF(MemScheduler, "processNextReqEvent: Looked up outgoing routing table for MemSidePort PortID: %d\n", memPortId);
    auto memPort = find_if(memPorts.begin(), memPorts.end(), [memPortId](MemSidePort &obj){return obj.portId() == memPortId;});
    DPRINTF(MemScheduler, "processNextReqEvent: Port with proper addr range, memSidePort[%d]\n", memPort->portId());
    queue->timesChecked++;

    DPRINTF(MemScheduler, "processNextReqEvent: Sending the packet if port not blocked\n");
    if (!memPort->blocked()){
        DPRINTF(MemScheduler, "processNextReqEvent: Port not blocked! Sending the packet\n");
        memPort->sendPacket(pkt);
        PortID cpuPortId = queue->cpuPortId;
        if (pkt->needsResponse()){
            DPRINTF(MemScheduler, "processNextReqEvent: Updating incoming routing table for future, <%s, %d>\n", pkt->getAddrRange().to_string(), cpuPortId);
            RequestPtr req = pkt->req;
            respRoutingTable[req] = cpuPortId;
        }
        DPRINTF(MemScheduler, "processNextReqEvent: Popping request from queues, requestQueues[%d], size = %d\n", cpuPortId, queue->readQueue.size());
        queue->readQueue.pop();
        DPRINTF(MemScheduler, "processNextReqEvent: Popped request from queues, requestQueues[%d], size = %d\n", cpuPortId, queue->readQueue.size());
    }

    if (!nextReqEvent.scheduled()){
        for (auto &queue : requestQueues){
            if (!queue.emptyRead()){
                DPRINTF(MemScheduler, "processNextReqEvent: Scheduling nextReqEvent in processNextReqEvent\n");
                schedule(nextReqEvent, curTick() + 500);
            }
        }
    }

    if (queue->sendRetry && !queue->blocked((pkt->isRead() && !pkt->isWrite()))){
        PortID cpuPortId = queue->cpuPortId;
        auto cpuPort = find_if(cpuPorts.begin(), cpuPorts.end(), [cpuPortId](CPUSidePort &obj){return obj.portId() == cpuPortId;});
        DPRINTF(MemScheduler, "processNextReqEvent: Sending retry to ports previously blocked\n");
        cpuPort->trySendRetry();
        queue->sendRetry = false;
    }

    return;
}

void
MemScheduler::processNextRespEvent(){

    DPRINTF(MemScheduler, "processNextRespEvent: Iterating over respQueues to send resp to cpuPorts\n");
    for (auto &queue : responseQueues){
        PortID cpuPortId = queue.cpuPortId;
        auto cpuPort = find_if(cpuPorts.begin(), cpuPorts.end(), [cpuPortId](CPUSidePort &obj){return obj.portId() == cpuPortId;});
        DPRINTF(MemScheduler, "processNextRespEvent: cpuPort chosen, (cpuPortId = %d) == (cpuPort->portId = %d)\n", cpuPortId, cpuPort->portId());
        if((!cpuPort->blocked()) && (!queue.respQueue.empty())){
            PacketPtr pkt = queue.respQueue.front();
            cpuPort->sendPacket(pkt);
            DPRINTF(MemScheduler, "processNextRespEvent: Popping resp responseQueue[%d], size = %d\n", cpuPortId, queue.respQueue.size());
            queue.respQueue.pop();
            DPRINTF(MemScheduler, "processNextRespEvent: Popped resp responseQueue[%d], size = %d\n", cpuPortId, queue.respQueue.size());
        }
    }

    if (!nextRespEvent.scheduled()){
        for (auto &queue : responseQueues){
            if (!queue.empty()){
                DPRINTF(MemScheduler, "processNextRespEvent: Scheduling nextRespEvent in processNextRespEvent\n");
                schedule(nextRespEvent, curTick() + 500);
                break;
            }
        }
    }

    for (auto &queue : responseQueues){
        if ((queue.sendRetry) && (!queue.blocked())){
            PortID memPortId = queue.memPortId;
            auto memPort = find_if(memPorts.begin(), memPorts.end(), [memPortId](MemSidePort &obj){return obj.portId() == memPortId;});
            DPRINTF(MemScheduler, "processNextRespEvent: Sending retry to ports previously blocked, <respQueue[%d], memPort[%d]>\n", queue.cpuPortId, memPort->portId());
            memPort->trySendRetry();
            queue.sendRetry = false;
        }
    }
    return;
}

bool
MemScheduler::handleResponse(PortID memPortId, PacketPtr pkt)
{
    RequestPtr req = pkt->req;
    PortID cpuPortId = respRoutingTable[req];
    DPRINTF(MemScheduler, "handleResponse: Received response from memSidePort[%d]\n", memPortId);
    DPRINTF(MemScheduler, "handleResponse: Looking up the incoming routing table to find proper queue for response, <%s, %d>\n", pkt->getAddrRange().to_string(), cpuPortId);
    auto responseQueue = find_if(responseQueues.begin(), responseQueues.end(), [cpuPortId](ResponseQueue &obj){return obj.cpuPortId == cpuPortId;});
    DPRINTF(MemScheduler, "handleResponse: Found correct cpuPort for range, <%s, %d>\n", pkt->getAddrRange().to_string(), responseQueue->cpuPortId);
    DPRINTF(MemScheduler, "handleResponse: Pushing response if queue not blocked!\n");
    if (responseQueue->blocked()){
        DPRINTF(MemScheduler, "handleResponse: Queue blocked! Remembering to send retry later, memPortId = %d\n", memPortId);
        responseQueue->sendRetry = true;
        responseQueue->memPortId = memPortId;
        return false;
    }
    DPRINTF(MemScheduler, "handleResponse: Queue not blocked!\n");
    DPRINTF(MemScheduler, "handleResponse: Pushing response to queues, responseQueues[%d], size = %d\n", cpuPortId, responseQueue->respQueue.size());
    responseQueue->respQueue.push(pkt);
    DPRINTF(MemScheduler, "handleResponse: Pushed response to queues, responseQueues[%d], size = %d\n", cpuPortId, responseQueue->respQueue.size());
    if(!nextRespEvent.scheduled()){
        DPRINTF(MemScheduler, "handleResponse: Scheduling nextRespEvent in handleResponse\n");
        schedule(nextRespEvent, curTick() + 100);
    }
    return true;
}

void
MemScheduler::handleFunctional(PacketPtr pkt)
{
    // Just pass this on to the memory side to handle for now.
    const Addr base_addr = pkt->getAddr();
    // Simply forward to the memory port
    for (auto &memPort : memPorts)
        // AddrRangeList addr_range = memPort->getAddrRanges();
        for (auto &addr_range : memPort.getAddrRanges())
            if (addr_range.start() <= base_addr &&
                    base_addr <= addr_range.end())
                memPort.sendFunctional(pkt);
}

AddrRangeList
MemScheduler::getAddrRanges() const
{
    DPRINTF(MemScheduler, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    AddrRangeList ret;
    // Simply forward to the memory port
    for (auto &memPort : memPorts){
        // AddrRangeList addr_range = memPort->getAddrRanges();
        for (auto &addr_range : memPort.getAddrRanges()){
            ret.push_back(addr_range);
        }
    }
    return ret;
}

void
MemScheduler::sendRangeChange()
{
    for (auto &cpuPort : cpuPorts)
        cpuPort.sendRangeChange();
}

void MemScheduler::recvRangeChange(PortID portId)
{
    DPRINTF(MemScheduler, "Received range change from mem_side_port[%d].\n", portId);
    // for
    // AddrRangeList ranges = memPorts[portId].getAddrRanges();
    // for (const auto& r: ranges) {
    //     DPRINTF(MemScheduler, "Adding range %s for id %d\n",
    //             r.to_string(), portId);
    //     if (memPortMap.insert(r, portId) == memPortMap.end()) {
    //         PortID conflict_id = memPortMap.intersects(r)->second;
    //         fatal("%s has two ports responding within range "
    //                 "%s:\n\t%s\n\t%s\n",
    //                 name(),
    //                 r.to_string(),
    //                 memPorts[portId].getPeer(),
    //                 memPorts[conflict_id].getPeer());
    //     }
    // }
    for (auto &port : memPorts){
        if (port.portId() == portId){
            AddrRangeList ranges = port.getAddrRanges();
            for (auto &r : ranges){
                memPortMap.insert(r, portId);
            }
        }
    }
    sendRangeChange();
}

void
MemScheduler::wakeUp()
{
    if (!nextReqEvent.scheduled()){
        for (auto &queue : requestQueues){
            if (!queue.emptyRead()){
                schedule(nextReqEvent, curTick() + 100);
                return;
            }
        }
    }
}
