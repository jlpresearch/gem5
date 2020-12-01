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

#ifndef __MEM_MEM_SCHEDULER_HH__
#define __MEM_MEM_SCHEDULER_HH__

#include <unordered_map>
#include <queue>

#include "base/statistics.hh"
#include "mem/port.hh"
#include "params/MemScheduler.hh"
// #include "sim/sim_object.hh"
#include "sim/clocked_object.hh"
#include "base/addr_range_map.hh"

/**
 * A very simple memory object. Current implementation doesn't even cache
 * anything it just forwards requests and responses.
 * This memobj is fully blocking (not non-blocking). Only a single request can
 * be outstanding at a time.
 */
// typedef std::deque<MemPacket*> MemPacketQueue;
class MemScheduler : public ClockedObject
{
  private:

    /**
     * Port on the CPU-side that receives requests.
     * Mostly just forwards requests to the owner.
     * Part of a vector of ports. One for each CPU port (e.g., data, inst)
     */

    struct RequestQueue{
      std::queue<PacketPtr> readQueue;
      std::queue<PacketPtr> writeQueue;
      uint64_t timesChecked;
      const uint32_t readQueueSize;
      const uint32_t writeQueueSize;
      const uint32_t writeThreshold;
      const PortID cpuPortId;
      const bool unifiedQueue;
      bool sendReadRetry;
      bool sendWriteRetry;
      bool blocked(bool isRead){
        if (!unifiedQueue){
          return isRead ? readQueue.size() == readQueueSize : writeQueue.size() == writeQueueSize;
        }
        else{
          return  readQueue.size() == readQueueSize;
        }
      }
      void push(PacketPtr pkt){
        if (!unifiedQueue){
          if (pkt->isRead())
            readQueue.push(pkt);
          else if(pkt->isWrite())
            writeQueue.push(pkt);
        }
        else
        {
          readQueue.push(pkt);
        }
      }
      bool emptyRead(){
        return readQueue.empty();
      }
      // bool emptyWrite(){
      //   return writeQueue.empty();
      // }
      bool serviceWrite(){
        if (unifiedQueue){
          return false;
        }
        bool ret = !writeQueue.empty() && readQueue.empty();
        ret |= writeQueue.size() > int((writeThreshold * writeQueueSize) / 100);
        return ret;
      }
      RequestQueue(uint32_t rQueueSize, uint32_t wQueueSize, uint32_t writePercentage, bool unifiedQ, PortID portId):
      timesChecked(0),
      readQueueSize(rQueueSize),
      writeQueueSize(wQueueSize),
      writeThreshold(writePercentage),
      cpuPortId(portId),
      unifiedQueue(unifiedQ),
      sendReadRetry(false),
      sendWriteRetry(false){}
    };

    struct ResponseQueue{
      std::queue<PacketPtr> respQueue;
      const uint32_t respQueueSize;
      PortID cpuPortId;
      PortID memPortId;
      bool sendRetry;
      bool blocked(){
        return respQueue.size() == respQueueSize;
      }
      bool empty(){
        return respQueue.empty();
      }
      ResponseQueue(uint32_t queueSize, PortID portId):
      respQueueSize(queueSize),
      cpuPortId(portId),
      sendRetry(false){}
    };

    class CPUSidePort : public ResponsePort
    {
      private:
        /// The object that owns this object (MemScheduler)
        MemScheduler *owner;

        /// Keep track of whether the port has been occupied by a previous packet
        bool _blocked;
        /// If we tried to send a packet and it was blocked, store it here
        PacketPtr _blockedPacket;
        const PortID _portId;

      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        CPUSidePort(const std::string& name, PortID portId, MemScheduler *owner) :
            ResponsePort(name, owner), owner(owner), _blocked(false),
            _blockedPacket(nullptr), _portId(portId)
        {}

        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is hanled in this function.
         *
         * @param packet to send.
         */
        void sendPacket(PacketPtr pkt);

        /**
         * Get a list of the non-overlapping address ranges the owner is
         * responsible for. All response ports must override this function
         * and return a populated list with at least one item.
         *
         * @return a list of ranges responded to
         */
        AddrRangeList getAddrRanges() const override;

        /**
         * Send a retry to the peer port only if it is needed. This is called
         * from the MemScheduler whenever it is unblocked.
         */
        void trySendRetry();

        bool blocked(){
          return _blocked;
        }

        PacketPtr blockedPkt(){
          return _blockedPacket;
        }

        PortID portId(){
          return _portId;
        }

      protected:
        /**
         * Receive an atomic request packet from the request port.
         * No need to implement in this simple memobj.
         */
        Tick recvAtomic(PacketPtr pkt) override
        { panic("recvAtomic unimpl."); }

        /**
         * Receive a functional request packet from the request port.
         * Performs a "debug" access updating/reading the data in place.
         *
         * @param packet the requestor sent.
         */
        void recvFunctional(PacketPtr pkt) override;

        /**
         * Receive a timing request from the request port.
         *
         * @param the packet that the requestor sent
         * @return whether this object can consume the packet. If false, we
         *         will call sendRetry() when we can try to receive this
         *         request again.
         */
        bool recvTimingReq(PacketPtr pkt) override;

        /**
         * Called by the request port if sendTimingResp was called on this
         * response port (causing recvTimingResp to be called on the request
         * port) and was unsuccesful.
         */
        void recvRespRetry() override;
    };

    /**
     * Port on the memory-side that receives responses.
     * Mostly just forwards requests to the owner
     */
    class MemSidePort : public RequestPort
    {
      private:
        /// The object that owns this object (MemScheduler)
        MemScheduler *owner;
        bool _blocked;
        /// If we tried to send a packet and it was blocked, store it here
        PacketPtr blockedPacket;
        const PortID _portId;
      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        MemSidePort(const std::string& name, PortID portId, MemScheduler *owner) :
            RequestPort(name, owner), owner(owner), _blocked(false), blockedPacket(nullptr), _portId(portId)
        {}

        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is hanled in this function.
         *
         * @param packet to send.
         */
        void trySendRetry();

        void sendPacket(PacketPtr pkt);

        bool blocked(){
          return _blocked;
        }

        PortID portId(){
          return _portId;
        }

      protected:
        /**
         * Receive a timing response from the response port.
         */
        bool recvTimingResp(PacketPtr pkt) override;

        /**
         * Called by the response port if sendTimingReq was called on this
         * request port (causing recvTimingReq to be called on the responder
         * port) and was unsuccesful.
         */
        void recvReqRetry() override;

        /**
         * Called to receive an address range change from the peer responder
         * port. The default implementation ignores the change and does
         * nothing. Override this function in a derived class if the owner
         * needs to be aware of the address ranges, e.g. in an
         * interconnect component like a bus.
         */
        void recvRangeChange() override;
    };

    /**
     * Handle the request from the CPU side
     *
     * @param requesting packet
     * @return true if we can handle the request this cycle, false if the
     *         requestor needs to retry later
     */
    bool handleRequest(PortID portId, PacketPtr pkt);

    /**
     * Handle the respone from the memory side
     *
     * @param responding packet
     * @return true if we can handle the response this cycle, false if the
     *         responder needs to retry later
     */
    bool handleResponse(PortID memPortId, PacketPtr pkt);

    /**
     * Handle a packet functionally. Update the data on a write and get the
     * data on a read.
     *
     * @param packet to functionally handle
     */
    void handleFunctional(PacketPtr pkt);

    /**
     * Return the address ranges this memobj is responsible for. Just use the
     * same as the next upper level of the hierarchy.
     *
     * @return the address ranges this memobj is responsible for
     */
    AddrRangeList getAddrRanges() const;

    /**
     * Tell the CPU side to ask for our memory ranges.
     */
    void sendRangeChange();

    void recvRangeChange(PortID portId);

    void wakeUp();

    /// Instantiation of the CPU-side ports
    std::vector<CPUSidePort> cpuPorts;

    /// Instantiation of the memory-side port
    std::vector<MemSidePort> memPorts;

    void processNextReqEvent();
    void processNextReqEventOpt();
    EventFunctionWrapper nextReqEvent;

    void processNextRespEvent();
    EventFunctionWrapper nextRespEvent;

    const uint32_t readBufferSize;
    const uint32_t writeBufferSize;
    const uint32_t respBufferSize;
    const uint32_t nMemPorts;
    const uint32_t nCpuPorts;
    const bool unifiedQueue;

    std::vector<RequestQueue> requestQueues;
    std::vector<ResponseQueue> responseQueues;
    std::unordered_map<RequestPtr, PortID> respRoutingTable;
    std::queue<PacketPtr> requestPool;
    AddrRangeMap<PortID, 0> memPortMap;

    std::unordered_map<PacketPtr, Tick> entryTimes;
    struct MemSchedulerStat : public Stats::Group
    {
      MemSchedulerStat(MemScheduler *parent);
      void regStats() override;

      Stats::Scalar readReqs;
      Stats::Scalar writeReqs;
      Stats::Scalar failedArbitrations;
      Stats::Scalar totalArbitrations;
      Stats::Scalar totalRQDelay;
      Stats::Scalar totalWQDelay;
      Stats::Formula avgRQDelay;
      Stats::Formula avgWQDelay;
    } stats;

  public:

    /** constructor
     */
    MemScheduler(const MemSchedulerParams &params);

    /**
     * Get a port with a given name and index. This is used at
     * binding time and returns a reference to a protocol-agnostic
     * port.
     *
     * @param if_name Port name
     * @param idx Index in the case of a VectorPort
     *
     * @return A reference to the given port
     */
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
};


#endif // __LEARNING_GEM5_PART2_SIMPLE_MEMOBJ_HH__
