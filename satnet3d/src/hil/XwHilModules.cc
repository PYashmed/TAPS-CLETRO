#include "../app/RecordFileEthernetSink.h"
#include "../app/reliable/ReliableEthernetPacket_m.h"
#include "../source/ActivePacketSourceMultipleInterval.h"
#include "../linklayer/emulation/ethernet/SatNet3DExtEthernetSocket.h"

#include <set>
#include <sstream>

#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/common/packet/serializer/ChunkSerializerRegistry.h"
#include "inet/common/packet/serializer/FieldsChunkSerializer.h"
#include "inet/common/socket/SocketTag_m.h"
#include "inet/linklayer/common/PcpTag_m.h"
#include "inet/linklayer/common/VlanTag_m.h"
#include "inet/linklayer/ethernet/common/EthernetMacHeader_m.h"
#include "inet/linklayer/ieee8021q/Ieee8021qTagHeader_m.h"
#include "inet/physicallayer/wired/ethernet/EthernetPhyHeader_m.h"
#include "inet/protocolelement/transceiver/PacketTransmitter.h"
#include "inet/queueing/common/PacketDelayer.h"

namespace satnet3d {
namespace hil {

using namespace inet;
using namespace omnetpp;

// Fixed 36-byte layout: flow(8), sequence(8), attempt(4), creation_ps(8),
// attempt_ps(8), all big endian. No OMNeT++ pointers or tags cross the wire.
class ReliableEthernetHeaderSerializer : public FieldsChunkSerializer
{
  protected:
    virtual void serialize(MemoryOutputStream& stream, const Ptr<const Chunk>& chunk) const override
    {
        const auto& header = staticPtrCast<const ReliableEthernetHeader>(chunk);
        stream.writeUint64Be(header->getFlowId());
        stream.writeUint64Be(header->getSequenceNumber());
        stream.writeUint32Be(header->getAttempt());
        stream.writeUint64Be(header->getOriginalCreationTime().inUnit(SIMTIME_PS));
        stream.writeUint64Be(header->getLastTransmissionTime().inUnit(SIMTIME_PS));
    }

    virtual const Ptr<Chunk> deserialize(MemoryInputStream& stream) const override
    {
        auto header = makeShared<ReliableEthernetHeader>();
        header->setFlowId(stream.readUint64Be());
        header->setSequenceNumber(stream.readUint64Be());
        header->setAttempt(stream.readUint32Be());
        header->setOriginalCreationTime(SimTime(static_cast<int64_t>(stream.readUint64Be()), SIMTIME_PS));
        header->setLastTransmissionTime(SimTime(static_cast<int64_t>(stream.readUint64Be()), SIMTIME_PS));
        return header;
    }
};

Register_Serializer(ReliableEthernetHeader, ReliableEthernetHeaderSerializer);

class HilPacketTransmitter : public PacketTransmitter
{
  protected:
    virtual void startTx(Packet *packet) override
    {
        ASSERT(!isTransmitting());
        txDatarate = bps(*dataratePar);
        txStartTime = simTime();
        txStartClockTime = getClockTime();
        txSignal = encodePacket(packet);
        emit(transmissionStartedSignal, txSignal);

        auto frame = packet->dup();
        frame->popAtFront<physicallayer::EthernetPhyHeader>();
        frame->trim();
        auto tag = frame->getTagForUpdate<PacketProtocolTag>();
        tag->setProtocol(&Protocol::ethernetMac);
        tag->setFrontOffset(b(0));
        tag->setBackOffset(b(0));
        send(frame, outputGate);
        scheduleTxEndTimer(txSignal);
    }
};

Define_Module(HilPacketTransmitter);

class HilEthernetSocket : public SatNet3DExtEthernetSocket
{
  protected:
    virtual void openSocket() override
    {
        const std::string name = par("device").stdstringValue();
        if (name.find("REPLACE_") == 0)
            throw cRuntimeError("HIL wiring template is not filled: %s device=%s",
                                getFullPath().c_str(), name.c_str());
        if (getParentModule()->par("externalEnabled").boolValue() && name.empty())
            throw cRuntimeError("Set a Linux device for HIL endpoint %s", getFullPath().c_str());
        SatNet3DExtEthernetSocket::openSocket();
    }

    virtual void handleMessage(cMessage *message) override
    {
        if (!message->isSelfMessage()) {
            auto packet = check_and_cast<Packet *>(message);
            packet->trim();
            packet->popAtBack<EthernetFcs>(B(4));
            packet->trim();
            auto tag = packet->getTagForUpdate<PacketProtocolTag>();
            tag->setFrontOffset(b(0));
            tag->setBackOffset(b(0));
        }
        SatNet3DExtEthernetSocket::handleMessage(message);
    }
};

Define_Module(HilEthernetSocket);

class HilPropagationDelayer : public queueing::PacketDelayer
{
  protected:
    virtual clocktime_t computeDelay(Packet *packet) const override
    {
        if (!par("enabled").boolValue())
            return CLOCKTIME_ZERO;
        auto nic = getContainingNicModule(this);
        auto channel = dynamic_cast<cDatarateChannel *>(nic->gate("phys$o")->getTransmissionChannel());
        if (channel == nullptr)
            throw cRuntimeError("HIL endpoint %s requires a retained logical datarate channel",
                                static_cast<cModule *>(nic)->getFullPath().c_str());
        return SIMTIME_AS_CLOCKTIME(channel->getDelay());
    }
};

Define_Module(HilPropagationDelayer);

class HilShadowTrafficGuard : public cSimpleModule, public cListener
{
  protected:
    std::vector<int> subscribedNodeIds;

    virtual int numInitStages() const override { return NUM_INIT_STAGES; }

    virtual void initialize(int stage) override
    {
        if (stage != INITSTAGE_LAST)
            return;
        auto network = getSimulation()->getSystemModule();
        std::istringstream input(par("shadowSatellites").stdstringValue());
        std::set<int> indices;
        int index;
        while (input >> index) {
            if (index < 0 || !indices.insert(index).second)
                throw cRuntimeError("Invalid HIL shadow index %d", index);
            auto node = network->getSubmodule("satellite", index);
            if (node == nullptr)
                throw cRuntimeError("HIL shadow satellite[%d] does not exist", index);
            node->subscribe(packetReceivedFromLowerSignal, this);
            subscribedNodeIds.push_back(node->getId());
        }
        if (!input.eof() || indices.empty())
            throw cRuntimeError("shadowSatellites must be a nonempty list of satellite indices");
    }

    virtual void receiveSignal(cComponent *source, simsignal_t, cObject *object, cObject *) override
    {
        auto packet = dynamic_cast<Packet *>(object);
        throw cRuntimeError("HIL shadow bypass: %s received %s through the simulated PHY. "
                            "An offline route uses an unwired boundary; update hil.ini and the physical wiring.",
                            source->getFullPath().c_str(), packet != nullptr ? packet->getName() : "a frame");
    }

  public:
    virtual ~HilShadowTrafficGuard()
    {
        for (int id : subscribedNodeIds)
            if (auto node = getSimulation()->getModule(id))
                node->unsubscribe(packetReceivedFromLowerSignal, this);
    }
};

Define_Module(HilShadowTrafficGuard);

class HilActivePacketSource : public ActivePacketSourceMultipleInterval
{
  protected:
    virtual Packet *createPacket() override
    {
        auto packet = ActivePacketSourceMultipleInterval::createPacket();
        auto length = packet->getDataLength();
        if (length < B(36))
            throw cRuntimeError("HIL measurement payload requires packetLength >= 36 B");
        if (vlanId < 1)
            throw cRuntimeError("HIL source requires the flow's VLAN ID");

        auto header = makeShared<ReliableEthernetHeader>();
        header->setFlowId(vlanId);
        header->setSequenceNumber(numProcessedPackets - 1);
        header->setAttempt(1);
        header->setOriginalCreationTime(simTime());
        header->setLastTransmissionTime(simTime());
        packet->eraseAll();
        packet->insertAtBack(header);
        if (length > header->getChunkLength())
            packet->insertAtBack(makeShared<ByteCountChunk>(length - header->getChunkLength()));
        return packet;
    }
};

Define_Module(HilActivePacketSource);

class HilRecordFileEthernetSink : public RecordFileEthernetSink
{
  protected:
    virtual void socketDataArrived(EthernetSocket *socket, Packet *packet) override
    {
        packet->removeTagIfPresent<SocketInd>();
        auto front = packet->peekAtFront();
        if (auto tag = dynamicPtrCast<const Ieee8021qTagEpdHeader>(front)) {
            packet->addTagIfAbsent<PcpInd>()->setPcp(tag->getPcp());
            packet->addTagIfAbsent<VlanInd>()->setVlanId(tag->getVid());
            packet->popAtFront<Ieee8021qTagEpdHeader>();
        }
        else if (auto tag = dynamicPtrCast<const Ieee8021qTagTpidHeader>(front)) {
            packet->addTagIfAbsent<PcpInd>()->setPcp(tag->getPcp());
            packet->addTagIfAbsent<VlanInd>()->setVlanId(tag->getVid());
            packet->popAtFront<Ieee8021qTagTpidHeader>();
        }
        const auto& header = packet->peekAtFront<ReliableEthernetHeader>(B(36));
        const int vlan = extractVlanId(packet);
        if (header->getFlowId() != static_cast<uint64_t>(vlan) || header->getAttempt() != 1)
            throw cRuntimeError("Unexpected HIL measurement header at %s", getFullPath().c_str());
        recordStream << simTime().dbl() << ','
                     << (simTime() - header->getOriginalCreationTime()).dbl() << ','
                     << header->getSequenceNumber() << ','
                     << extractPriority(packet) << ','
                     << vlan << ','
                     << getNextFlowSequenceNumber(vlan) << ','
                     << packet->getDataLength().get<B>() << '\n';
        receivedPackets++;
        emit(packetReceivedSignal, packet);
        delete packet;
    }
};

Define_Module(HilRecordFileEthernetSink);

} // namespace hil
} // namespace satnet3d
