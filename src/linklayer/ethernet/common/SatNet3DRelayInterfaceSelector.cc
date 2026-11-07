#include "SatNet3DRelayInterfaceSelector.h"

#include "inet/common/ModuleAccess.h"
#include "inet/common/DirectionTag_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/linklayer/common/MacAddressTag_m.h"
#include "inet/linklayer/common/VlanTag_m.h"

namespace satnet3d {

Define_Module(SatNet3DRelayInterfaceSelector);

bool SatNet3DRelayInterfaceSelector::isForwardingInterface(NetworkInterface *networkInterface) const
{
    if (!RelayInterfaceSelector::isForwardingInterface(networkInterface))
        return false;

    cModule *networkNode = findContainingNode(networkInterface);
    if (networkNode == nullptr)
        return false;

    int nodeOutputGateId = networkInterface->getNodeOutputGateId();
    if (nodeOutputGateId < 0)
        return false;

    cGate *nodeOutputGate = networkNode->gate(nodeOutputGateId);
    return nodeOutputGate != nullptr && nodeOutputGate->isConnectedOutside();
}

void SatNet3DRelayInterfaceSelector::pushPacket(Packet *packet, const cGate *gates)
{
    Enter_Method("pushPacket");
    take(packet);

    auto interfaceReq = packet->findTag<InterfaceReq>();
    auto macAddressReq = packet->getTag<MacAddressReq>();
    auto destinationAddress = macAddressReq->getDestAddress();

    if (interfaceReq != nullptr) {
        auto networkInterface = interfaceTable->getInterfaceById(interfaceReq->getInterfaceId());
        sendPacket(packet, destinationAddress, networkInterface);
    }
    else {
        auto interfaceInd = packet->findTag<InterfaceInd>();
        auto incomingInterface = interfaceInd != nullptr ? interfaceTable->getInterfaceById(interfaceInd->getInterfaceId()) : nullptr;
        auto vlanReq = packet->findTag<VlanReq>();
        int vlanId = vlanReq != nullptr ? vlanReq->getVlanId() : 0;

        if (destinationAddress.isBroadcast()) {
            broadcastPacket(packet, destinationAddress, incomingInterface);
        }
        else if (destinationAddress.isMulticast()) {
            auto outgoingInterfaceIds = macForwardingTable->getMulticastAddressForwardingInterfaces(destinationAddress, vlanId);
            if (outgoingInterfaceIds.size() == 0)
                broadcastPacket(packet, destinationAddress, incomingInterface);
            else {
                for (auto outgoingInterfaceId : outgoingInterfaceIds) {
                    if (interfaceInd != nullptr && outgoingInterfaceId == interfaceInd->getInterfaceId())
                        ; // skip same interface
                    else {
                        auto outgoingInterface = interfaceTable->getInterfaceById(outgoingInterfaceId);
                        sendPacket(packet->dup(), destinationAddress, outgoingInterface);
                    }
                }
                delete packet;
            }
        }
        else {
            int outgoingInterfaceId = macForwardingTable->getUnicastAddressForwardingInterface(destinationAddress, vlanId);
            if (interfaceInd != nullptr && outgoingInterfaceId == interfaceInd->getInterfaceId()) {
                numDroppedFrames++;
                PacketDropDetails details;
                details.setReason(NO_INTERFACE_FOUND);
                emit(packetDroppedSignal, packet, &details);
                delete packet;
            }
            else if (outgoingInterfaceId != -1) {
                auto outgoingInterface = interfaceTable->getInterfaceById(outgoingInterfaceId);
                sendPacket(packet, destinationAddress, outgoingInterface);
            }
            else {
                numDroppedFrames++;
                PacketDropDetails details;
                details.setReason(NO_INTERFACE_FOUND);
                emit(packetDroppedSignal, packet, &details);
                delete packet;
            }
        }
    }
    numProcessedFrames++;
}

} // namespace satnet3d
