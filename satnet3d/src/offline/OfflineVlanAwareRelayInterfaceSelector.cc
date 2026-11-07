#include "OfflineVlanAwareRelayInterfaceSelector.h"

#include "inet/common/DirectionTag_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/linklayer/common/MacAddressTag_m.h"
#include "inet/linklayer/common/VlanTag_m.h"
#include "inet/linklayer/ethernet/common/EthernetMacHeader_m.h"
#include "inet/linklayer/ieee8021q/Ieee8021qTagHeader_m.h"

using namespace inet;
using namespace omnetpp;

namespace satnet3d {
namespace offline {

Define_Module(OfflineVlanAwareRelayInterfaceSelector);

unsigned int OfflineVlanAwareRelayInterfaceSelector::getVlanId(const Packet *packet) const
{
    if (auto vlanReq = packet->findTag<VlanReq>())
        return vlanReq->getVlanId();
    if (auto vlanInd = packet->findTag<VlanInd>())
        return vlanInd->getVlanId();

    // A bridge relays a complete frame, therefore the Q-tag follows the
    // Ethernet MAC header instead of being the packet's first chunk.
    auto frontChunk = packet->peekAtFront();
    if (auto macHeader = dynamicPtrCast<const EthernetMacHeader>(frontChunk)) {
        b qTagOffset = macHeader->getChunkLength();
        if (packet->hasDataAt<Ieee8021qTagEpdHeader>(qTagOffset))
            return packet->peekDataAt<Ieee8021qTagEpdHeader>(qTagOffset)->getVid();
        if (packet->hasDataAt<Ieee8021qTagTpidHeader>(qTagOffset))
            return packet->peekDataAt<Ieee8021qTagTpidHeader>(qTagOffset)->getVid();
    }
    else {
        if (auto epdHeader = dynamicPtrCast<const Ieee8021qTagEpdHeader>(frontChunk))
            return epdHeader->getVid();
        if (auto tpidHeader = dynamicPtrCast<const Ieee8021qTagTpidHeader>(frontChunk))
            return tpidHeader->getVid();
    }
    return 0;
}

void OfflineVlanAwareRelayInterfaceSelector::pushPacket(Packet *packet, const cGate *gate)
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
        auto incomingInterface = interfaceInd != nullptr ?
            interfaceTable->getInterfaceById(interfaceInd->getInterfaceId()) : nullptr;
        unsigned int vlanId = getVlanId(packet);

        if (destinationAddress.isBroadcast())
            broadcastPacket(packet, destinationAddress, incomingInterface);
        else if (destinationAddress.isMulticast()) {
            auto outgoingInterfaceIds = macForwardingTable->getMulticastAddressForwardingInterfaces(destinationAddress, vlanId);
            if (outgoingInterfaceIds.empty())
                broadcastPacket(packet, destinationAddress, incomingInterface);
            else {
                for (auto outgoingInterfaceId : outgoingInterfaceIds) {
                    if (interfaceInd == nullptr || outgoingInterfaceId != interfaceInd->getInterfaceId()) {
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
                EV_WARN << "No offline VLAN route, dropping packet"
                        << EV_FIELD(destinationAddress) << EV_FIELD(vlanId) << EV_FIELD(packet) << EV_ENDL;
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

} // namespace offline
} // namespace satnet3d
