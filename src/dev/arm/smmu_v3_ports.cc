/*
 * Copyright (c) 2013, 2018-2019 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
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

#include "dev/arm/smmu_v3_ports.hh"

#include "base/logging.hh"
#include "dev/arm/smmu_v3.hh"
#include "dev/arm/smmu_v3_slaveifc.hh"

SMMUMasterPort::SMMUMasterPort(const std::string &_name, SMMUv3 &_smmu) :
    RequestPort(_name, &_smmu),
    smmu(_smmu)
{}

bool
SMMUMasterPort::recvTimingResp(PacketPtr pkt)
{
    return smmu.masterRecvTimingResp(pkt);
}

void
SMMUMasterPort::recvReqRetry()
{
    return smmu.masterRecvReqRetry();
}

SMMUMasterTableWalkPort::SMMUMasterTableWalkPort(const std::string &_name,
                                                 SMMUv3 &_smmu) :
    RequestPort(_name, &_smmu),
    smmu(_smmu)
{}

bool
SMMUMasterTableWalkPort::recvTimingResp(PacketPtr pkt)
{
    return smmu.masterTableWalkRecvTimingResp(pkt);
}

void
SMMUMasterTableWalkPort::recvReqRetry()
{
    return smmu.masterTableWalkRecvReqRetry();
}

SMMUSlavePort::SMMUSlavePort(const std::string &_name,
                             SMMUv3SlaveInterface &_ifc,
                             PortID _id)
:
    QueuedResponsePort(_name, &_ifc, respQueue, _id),
    ifc(_ifc),
    respQueue(_ifc, *this)
{}

void
SMMUSlavePort::recvFunctional(PacketPtr pkt)
{
    if (!respQueue.trySatisfyFunctional(pkt))
        recvAtomic(pkt);
}

Tick
SMMUSlavePort::recvAtomic(PacketPtr pkt)
{
    return ifc.recvAtomic(pkt);
}

bool
SMMUSlavePort::recvTimingReq(PacketPtr pkt)
{
    return ifc.recvTimingReq(pkt);
}

SMMUControlPort::SMMUControlPort(const std::string &_name,
                                 SMMUv3 &_smmu, AddrRange _addrRange)
:
    SimpleTimingPort(_name, &_smmu),
    smmu(_smmu),
    addrRange(_addrRange)
{}

Tick
SMMUControlPort::recvAtomic(PacketPtr pkt)
{
    Addr addr = pkt->getAddr();
    unsigned size = pkt->getSize();

    if (!addrRange.contains(addr) || !addrRange.contains(addr+size))
        panic("SMMU: invalid address on control port %x, packet size %d",
                addr, size);

    // @todo: We need to pay for this and not just zero it out
    pkt->headerDelay = pkt->payloadDelay = 0;

    return pkt->isRead() ? smmu.readControl(pkt) : smmu.writeControl(pkt);
}

AddrRangeList
SMMUControlPort::getAddrRanges() const
{
    AddrRangeList list;
    list.push_back(addrRange);
    return list;
}

SMMUATSMasterPort::SMMUATSMasterPort(const std::string &_name,
                                     SMMUv3SlaveInterface &_ifc) :
    QueuedRequestPort(_name, &_ifc, reqQueue, snoopRespQueue),
    ifc(_ifc),
    reqQueue(_ifc, *this),
    snoopRespQueue(_ifc, *this)
{}

bool
SMMUATSMasterPort::recvTimingResp(PacketPtr pkt)
{
    return ifc.atsMasterRecvTimingResp(pkt);
}

SMMUATSSlavePort::SMMUATSSlavePort(const std::string &_name,
                                   SMMUv3SlaveInterface &_ifc) :
    QueuedResponsePort(_name, &_ifc, respQueue),
    ifc(_ifc),
    respQueue(_ifc, *this)
{}

void
SMMUATSSlavePort::recvFunctional(PacketPtr pkt)
{
    panic("Functional access on ATS port!");
}

Tick
SMMUATSSlavePort::recvAtomic(PacketPtr pkt)
{
    return ifc.atsSlaveRecvAtomic(pkt);
}

bool
SMMUATSSlavePort::recvTimingReq(PacketPtr pkt)
{
    return ifc.atsSlaveRecvTimingReq(pkt);
}
