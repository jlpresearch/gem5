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
        System *const system;

        const RequestorID requestorId;

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

                /**
                 * Send a packet across this port. T
                 * his is called by the owner and all of the flow
                 * control is hanled in this function.
                 * @param packet to send.
                 */
                // void trySendRetry();

                void sendTimingPacket(PacketPtr pkt);
                void sendFunctionalPacket(PacketPtr pkt);

                bool blocked(){
                    return _blocked;
                }


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


        virtual void startup() override;

        virtual void init() override;

        void handleResponse(PacketPtr pkt);


        PacketPtr getReadPacket(Addr, unsigned int);
        PacketPtr getWritePacket(Addr, unsigned int, uint8_t*);

        // void generateNextBatch();


        Addr indexToAddr (uint64_t index);
        uint64_t addrToIndex (Addr addr);


        uint64_t randomNumberGenerator(int64_t n);

        void wakeUp();

        void generateNextReq();
        EventFunctionWrapper nextGenEvent;

        void sendNextBatch();
        EventFunctionWrapper nextSendEvent;

        std::unordered_map<PacketPtr, Tick> exitTimes;
        std::queue<PacketPtr> requestPool;
        std::queue<PacketPtr> responsePool;

        GenPort port;

        Addr startAddr; // Should be a multiple of 64
        Addr memSize;

        std::unordered_map<Addr, uint64_t> updateTable;

        uint64_t randomSeeds[128];
        uint64_t numUpdates;

        Addr blockSize;


        uint64_t tableSize;

        bool doneReading;

        uint64_t onTheFlyRequests;
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
