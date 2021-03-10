#include "cpu/testers/traffic_gen/gups_gen.hh"

#include <cstring>
#include <string>

#include "base/random.hh"
#include "debug/GUPSGen.hh"
#include "sim/sim_exit.hh"

#define POLY 0x0000000000000007UL
#define PERIOD 1317624576693539401L


GUPSGen::GUPSGen(const GUPSGenParams &params):
    ClockedObject(params),
    system(params.system),
    requestorId(system->getRequestorId(this)),
    nextGenEvent([this]{ generateNextReq(); }, name()),
    nextSendEvent([this]{ sendNextBatch(); }, name()),
    port(name() + ".port", this),
    startAddr(params.start_addr),
    memSize(params.mem_size),
    blockSize(params.block_size),
    doneReading(false),
    onTheFlyRequests(0)
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
    }

    /*
     * initialize memory by functional access
     * call generateNextBatch here.
    */
   schedule(nextGenEvent, curTick());
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
GUPSGen::generateNextReq()
{
    static uint64_t iterations = 0;
    static int rIndex = 0;

    assert (rIndex < 128);

    if (iterations >= numUpdates / 128){
        doneReading = true;

        // exitSimLoop(name() + " has encountered the exit state and will "
                // "terminate the simulation.\n");
        return;
    }

    randomSeeds[rIndex] = (randomSeeds[rIndex] << 1) ^
                    ((int64_t) randomSeeds[rIndex] < 0 ? POLY : 0);
    uint64_t value = randomSeeds[rIndex];
    uint64_t index = randomSeeds[rIndex] & (tableSize - 1);
    Addr addr = indexToAddr(index);
    updateTable[addr] = value;
    PacketPtr pkt = getReadPacket(addr, sizeof(uint64_t));
    DPRINTF(GUPSGen, "generateNextBatch: Pushed pkt, pkt->addr_range: %s,"
                    "(pkt != nullptr): %d\n",
                    pkt->getAddrRange().to_string(), pkt != nullptr);
    requestPool.push(pkt);

    rIndex++;
    if (rIndex >= 128){
        rIndex = 0;
        iterations++;
    }

    schedule(nextGenEvent, curTick() + 250);

    if (!nextSendEvent.scheduled() && !port.blocked()
                            && !requestPool.empty()) {
        schedule(nextSendEvent, curTick() + 100);
    }
}


void
GUPSGen::sendNextBatch()
{
    if (port.blocked()){
        return;
    }

    if (!requestPool.empty())
    {
        PacketPtr pkt = requestPool.front();

        exitTimes[pkt] = curTick();
        DPRINTF(GUPSGen, "sendNextBatch: Sent pkt, pkt->addr_range: %s,"
                        "(pkt != nullptr): %d\n",
                        pkt->getAddrRange().to_string(), pkt != nullptr);
        port.sendTimingPacket(pkt);
        onTheFlyRequests++;
        requestPool.pop();
    }

    if (!nextSendEvent.scheduled() && !port.blocked()
                                && !requestPool.empty()) {
        schedule(nextSendEvent, curTick() + 250);
    }

    // if (requestPool.empty())
    // {
    //     generateNextBatch();
    // }

    return;
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
    Addr ret = index * 8 + startAddr;

    return ret;
}

void
GUPSGen::handleResponse(PacketPtr pkt)
{
    delete pkt;
    onTheFlyRequests--;
    if (doneReading && requestPool.empty() && onTheFlyRequests == 0){
        exitSimLoop(name() + " has encountered the exit state and will "
                "terminate the simulation.\n");
        return;
    }
    // responsePool.push(pkt);
}

void
GUPSGen::GenPort::sendTimingPacket(PacketPtr pkt)
{
    panic_if(_blocked, "Should never try to send if blocked MemSide!");
    DPRINTF(GUPSGen, "GenPort::sendTimingPacket: "
                        "Packet pkt arrived at port, pkt->addr_range: %s,"
                        "(pkt != nullptr): %d\n",
                        pkt->getAddrRange().to_string(), pkt != nullptr);
    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        DPRINTF(GUPSGen, "GenPort::sendTimingPacket: Packet blocked\n");
        blockedPacket = pkt;
        _blocked = true;
        DPRINTF(GUPSGen, "GenPort::sendTimingPacket: Status update,"
                    "port._blocked: %d, port.blockedPacket->addr_range: %s,"
                    "(port.blockedPacket != nullptr): %d\n",
                    _blocked, blockedPacket->getAddrRange().to_string(),
                    blockedPacket != nullptr);
    }
    else {
        DPRINTF(GUPSGen, "GenPort::sendTimingPacket: Packet sent\n");
    }

}

void
GUPSGen::GenPort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    DPRINTF(GUPSGen, "GenPort::recvReqRetry: Received a retry, "
                    "_blocked: %d, (blockedPacket != nullptr): %d\n",
                    _blocked, blockedPacket != nullptr);
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


void
GUPSGen::GenPort::sendFunctionalPacket(PacketPtr pkt)
{
    sendFunctional(pkt);
}

void GUPSGen::wakeUp()
{
    if (!nextSendEvent.scheduled() && !requestPool.empty()){
        schedule(nextSendEvent, curTick() + 100);
    }
}
