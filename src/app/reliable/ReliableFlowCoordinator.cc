// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ReliableFlowCoordinator.h"

#include "ReliableEthernetPacket_m.h"

namespace satnet3d {

using namespace omnetpp;

Define_Module(ReliableFlowCoordinator);

void ReliableFlowCoordinator::handleMessage(cMessage *message)
{
    throw cRuntimeError("ReliableFlowCoordinator does not accept ordinary messages (received %s)", message->getFullName());
}

void ReliableFlowCoordinator::registerSource(uint64_t flowId, int sourceModuleId)
{
    Enter_Method("registerSource(flowId=%llu, sourceModuleId=%d)", static_cast<unsigned long long>(flowId), sourceModuleId);

    auto existing = sourceModuleIds.find(flowId);
    if (existing != sourceModuleIds.end() && existing->second != sourceModuleId)
        throw cRuntimeError("Reliable flow %llu is already registered by module id %d",
                static_cast<unsigned long long>(flowId), existing->second);
    sourceModuleIds[flowId] = sourceModuleId;
}

void ReliableFlowCoordinator::reportDelivery(uint64_t flowId, uint64_t sequenceNumber)
{
    Enter_Method("reportDelivery(flowId=%llu, sequenceNumber=%llu)",
            static_cast<unsigned long long>(flowId), static_cast<unsigned long long>(sequenceNumber));

    auto sourceIt = sourceModuleIds.find(flowId);
    if (sourceIt == sourceModuleIds.end())
        throw cRuntimeError("No ReliableEthernetSource is registered for flow %llu",
                static_cast<unsigned long long>(flowId));

    cModule *source = dynamic_cast<cModule *>(getSimulation()->getComponent(sourceIt->second));
    if (source == nullptr)
        throw cRuntimeError("The source module registered for flow %llu no longer exists",
                static_cast<unsigned long long>(flowId));

    auto indication = new ReliableDeliveryInd("deliveryInd");
    indication->setFlowId(flowId);
    indication->setSequenceNumber(sequenceNumber);
    sendDirect(indication, source->gate("deliveryIn"));
}

} // namespace satnet3d
