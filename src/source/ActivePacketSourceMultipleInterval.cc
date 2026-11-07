//
// Copyright (C) 2020 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#include "ActivePacketSourceMultipleInterval.h"

#include "inet/common/ProtocolUtils.h"
#include "inet/linklayer/common/PcpTag_m.h"
#include "inet/linklayer/common/VlanTag_m.h"

namespace satnet3d {

Define_Module(ActivePacketSourceMultipleInterval);

void ActivePacketSourceMultipleInterval::initialize(int stage)
{
    ActivePacketSource::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        pcp = par("pcp");
        vlanId = par("vlanId");
        stopTime = par("stopTime");
        if (pcp < -1 || pcp > 7)
            throw cRuntimeError("pcp must be -1 or in the range 0..7");
        if (vlanId < -1 || vlanId > 4094)
            throw cRuntimeError("vlanId must be -1 or in the range 0..4094");
    }
    else if (stage == INITSTAGE_QUEUEING){
        productionInterval = productionIntervalParameter->doubleValue();
        curPrdIntvNum = 0;
        productionIntervalSwap = new ClockEvent("productionIntervalSwap");
        productionIntervalSwapPeriod = par("productionIntervalSwapPeriod");
        //下面读取多组productionInterval参数
        auto productionIntervalPar = check_and_cast<cValueArray *>(par("productionIntervalList").objectValue());
        prdIntvNum = productionIntervalPar->size();
        for (int i = 0; i < prdIntvNum; i++) {
            clocktime_t piTmp = productionIntervalPar->get(i).doubleValueInUnit("s");
            productionIntervalList.push_back(piTmp);
        }
        if (prdIntvNum > 0){
            productionInterval = productionIntervalList[0];
            curPrdIntvNum = (curPrdIntvNum+1) % prdIntvNum;
            scheduleClockEventAfter(productionIntervalSwapPeriod, productionIntervalSwap);
        }
    }
}

Packet *ActivePacketSourceMultipleInterval::createPacket()
{
    auto packet = ActivePacketSource::createPacket();
    if (pcp >= 0)
        packet->addTagIfAbsent<PcpReq>()->setPcp(pcp);
    if (vlanId >= 0)
        packet->addTagIfAbsent<VlanReq>()->setVlanId(vlanId);
    if (pcp >= 0 || vlanId >= 0) {
        ensureEncapsulationProtocolReq(packet, &Protocol::ieee8021qCTag);
        setDispatchProtocol(packet);
    }
    return packet;
}

void ActivePacketSourceMultipleInterval::handleMessage(cMessage *message)
{
    if (message == productionTimer) {
        if (isBeforeStopTime(getClockTime()) &&
                (consumer == nullptr || consumer->canPushSomePacket(outputGate->getPathEndGate()))) {
            if (isBeforeStopTime(getClockTime() + productionInterval))
                scheduleProductionTimer(productionInterval);
            producePacket();
        }
    }
    else if (message == productionIntervalSwap){
        scheduleProductionIntervalSwap();
    }
    else
        throw cRuntimeError("Unknown message %s", message->getFullName());
}

void ActivePacketSourceMultipleInterval::scheduleProductionIntervalSwap(){
    productionInterval = productionIntervalList[curPrdIntvNum];
    curPrdIntvNum = (curPrdIntvNum+1) % prdIntvNum;
    scheduleClockEventAfter(productionIntervalSwapPeriod, productionIntervalSwap);
//    EV << "Swapping to next prdintv, now is No." << curPrdIntvNum << ", current time: " << simTime() << "productionInterval: "<< productionInterval << EV_ENDL;
}

void ActivePacketSourceMultipleInterval::scheduleProductionTimerAndProducePacket()
{
    if (!initialProductionOffsetScheduled && initialProductionOffset >= 0) {
        if (isBeforeStopTime(getClockTime() + initialProductionOffset))
            scheduleProductionTimer(initialProductionOffset);
        initialProductionOffsetScheduled = true;
    }
    else if (isBeforeStopTime(getClockTime()) &&
            (consumer == nullptr || consumer->canPushSomePacket(outputGate->getPathEndGate()))) {
        if (isBeforeStopTime(getClockTime() + productionInterval))
            scheduleProductionTimer(productionInterval);
        producePacket();
    }
}

bool ActivePacketSourceMultipleInterval::isBeforeStopTime(clocktime_t time) const
{
    return stopTime < CLOCKTIME_ZERO || time < stopTime;
}

} // namespace satnet3d
