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

#ifndef __MEM_GUPS_GEN_HH__
#define __MEM_GUPS_GEN_HH__

#include <queue>
#include <unordered_map>
#include <vector>

#include "base/statistics.hh"
#include "mem/port.hh"
#include "params/GUPSGen.hh"
#include "sim/clocked_object.hh"
#include "sim/system.hh"

class GUPSGen : public ClockedObject {

    private:

        class GenPort : public RequestPort
        {
            private:
                /// The object that owns this object (GUPSGen)
                GUPSGen *owner;
                bool _blocked;
                /// If we tried to send a packet and it was blocked,
                // store it here
                PacketPtr blockedPacket;

            public:
                /**
                 * Constructor. Just calls the superclass constructor.
                 */
                GenPort(const std::string& name, GUPSGen *owner) :
                    RequestPort(name, owner), owner(owner), _blocked(false),
                    blockedPacket(nullptr)
                {}

                bool blocked(){
                    return _blocked;
                }
                /**
                 * Send a packet across this port. T
                 * his is called by the owner and all of the flow
                 * control is hanled in this function.
                 * @param packet to send.
                 */
                // void trySendRetry();

                void sendTimingPacket(PacketPtr pkt);
                void sendFunctionalPacket(PacketPtr pkt);

            protected:
                /**
                 * Receive a timing response from the response port.
                 */
                bool recvTimingResp(PacketPtr pkt) override;

                /**
                 * Called by the response port if sendTimingReq
                 * was called on this request port (causing recvTimingReq
                 * to be called on the responder port) and was unsuccesful.
                 */
                void recvReqRetry() override;

                /**
                 * Called to receive an address range change
                 * from the peer responder port. The default implementation
                 * ignores the change and does nothing. Override
                 * this function in a derived class if the owner
                 * needs to be aware of the address ranges, e.g. in an
                 * interconnect component like a bus.
                 */
                // void recvRangeChange() override;
        };


        virtual void init() override;

        virtual void startup() override;

        uint64_t randomNumberGenerator(int64_t n);
        Addr indexToAddr (uint64_t index);

        PacketPtr getReadPacket(Addr, unsigned int);
        PacketPtr getWritePacket(Addr, unsigned int, uint8_t*);

        void handleResponse(PacketPtr pkt);
        void wakeUp();

        void sendNextReq();
        EventFunctionWrapper nextSendEvent;

        std::unordered_map<RequestPtr, uint64_t> updateTable;
        std::unordered_map<RequestPtr, Tick> exitTimes;
        std::queue<PacketPtr> requestPool;
        std::queue<PacketPtr> responsePool;

        uint64_t randomSeeds[128];
        uint64_t numUpdates;
        uint64_t tableSize;

        System *const system;

        const RequestorID requestorId;

        GenPort port;

        Addr blockSize;

        Addr startAddr;

        Addr memSize;

        int reqQueueSize;

        bool doGenerate;

        bool doneReading;

        uint64_t onTheFlyRequests;

        const int elementSize;

        struct GUPSGenStat : public Stats::Group
        {
            GUPSGenStat(GUPSGen* parent);
            void regStats() override;

            Stats::Scalar totalTicks;

            Stats::Scalar totalUpdates;
            Stats::Formula GUPS;

            Stats::Scalar totalReads;
            Stats::Scalar totalBytesRead;
            Stats::Formula avgReadBW;
            Stats::Scalar totalReadLat;
            Stats::Formula avgReadLat;

            Stats::Scalar totalWrites;
            Stats::Scalar totalBytesWritten;
            Stats::Formula avgWriteBW;
            Stats::Scalar totalWriteLat;
            Stats::Formula avgWriteLat;
        } stats;

    public:

        GUPSGen(const GUPSGenParams &params);


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

#endif
