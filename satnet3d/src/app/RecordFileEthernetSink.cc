#include "RecordFileEthernetSink.h"

#include <iomanip>
#include <regex>

#include "inet/applications/base/ApplicationPacket_m.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/Protocol.h"
#include "inet/common/SequenceNumberTag_m.h"
#include "inet/common/socket/SocketTag_m.h"
#include "inet/linklayer/common/PcpTag_m.h"
#include "inet/linklayer/common/UserPriorityTag_m.h"
#include "inet/linklayer/common/VlanTag_m.h"
#include "inet/linklayer/ieee8021q/Ieee8021qTagHeader_m.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/networklayer/contract/IInterfaceTable.h"

namespace satnet3d {

using namespace inet;
using namespace omnetpp;

Define_Module(RecordFileEthernetSink);

RecordFileEthernetSink::~RecordFileEthernetSink()
{
    closeRecordFile();
}

void RecordFileEthernetSink::initialize(int stage)
{
    ApplicationBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        recordFile = par("recordFile").stdstringValue();
        appendFile = par("appendFile");
        writeHeader = par("writeHeader");
        if (recordFile.empty())
            throw cRuntimeError("recordFile must not be empty");

        std::ios::openmode mode = std::ios::out | (appendFile ? std::ios::app : std::ios::trunc);
        recordStream.open(recordFile, mode);
        if (!recordStream.is_open())
            throw cRuntimeError("Cannot open Ethernet packet record file '%s'", recordFile.c_str());
        recordStream << std::fixed << std::setprecision(6);
        if (writeHeader && !appendFile)
            recordStream << "simTime,endToEndDelay,sequenceNumber,priority,vlanId,flowSequenceNumber,packetBytes\n";
        WATCH(receivedPackets);
    }
    else if (stage == INITSTAGE_NETWORK_INTERFACE_CONFIGURATION + 1) {
        auto interfaceTable = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        networkInterface = interfaceTable->findInterfaceByName(par("interface"));
        if (networkInterface == nullptr)
            throw cRuntimeError("Cannot find network interface '%s'", par("interface").stringValue());

        const char *localAddressString = par("localAddress");
        localAddress = *localAddressString == '\0' ? networkInterface->getMacAddress() : resolveMacAddress(localAddressString);
        remoteAddress = resolveMacAddress(par("remoteAddress"));
    }
}

MacAddress RecordFileEthernetSink::resolveMacAddress(const char *address) const
{
    if (address == nullptr || *address == '\0')
        return MacAddress();
    L3Address result;
    L3AddressResolver resolver;
    if (resolver.tryResolve(address, result, L3AddressResolver::ADDR_MAC) && result.getType() == L3Address::MAC)
        return result.toMac();
    return MacAddress(address);
}

void RecordFileEthernetSink::handleStartOperation(LifecycleOperation *operation)
{
    socket.setCallback(this);
    socket.setNetworkInterface(networkInterface);
    socket.setOutputGate(gate("socketOut"));
    const char *protocolName = par("protocol");
    const Protocol *protocol = *protocolName == '\0' ? nullptr : Protocol::getProtocol(protocolName);
    socket.bind(localAddress, remoteAddress, protocol, par("steal"));
}

void RecordFileEthernetSink::handleStopOperation(LifecycleOperation *operation)
{
    socket.close();
    delayActiveOperationFinish(par("stopOperationTimeout"));
}

void RecordFileEthernetSink::handleCrashOperation(LifecycleOperation *operation)
{
    socket.destroy();
}

void RecordFileEthernetSink::handleMessageWhenUp(cMessage *message)
{
    if (socket.belongsToSocket(message))
        socket.processMessage(message);
    else
        throw cRuntimeError("Unexpected message %s", message->getFullName());
}

void RecordFileEthernetSink::socketDataArrived(EthernetSocket *socket, Packet *packet)
{
    packet->removeTagIfPresent<SocketInd>();
    auto frontChunk = packet->peekAtFront();
    if (auto qTag = dynamicPtrCast<const Ieee8021qTagEpdHeader>(frontChunk)) {
        packet->addTagIfAbsent<PcpInd>()->setPcp(qTag->getPcp());
        packet->addTagIfAbsent<VlanInd>()->setVlanId(qTag->getVid());
        packet->popAtFront<Ieee8021qTagEpdHeader>();
    }
    else if (auto qTag = dynamicPtrCast<const Ieee8021qTagTpidHeader>(frontChunk)) {
        packet->addTagIfAbsent<PcpInd>()->setPcp(qTag->getPcp());
        packet->addTagIfAbsent<VlanInd>()->setVlanId(qTag->getVid());
        packet->popAtFront<Ieee8021qTagTpidHeader>();
    }
    int vlanId = extractVlanId(packet);
    long sequenceNumber = extractSequenceNumber(packet);
    int priority = extractPriority(packet);
    long flowSequenceNumber = getNextFlowSequenceNumber(vlanId);

    recordStream << simTime().dbl() << ','
                 << (simTime() - packet->getCreationTime()).dbl() << ','
                 << sequenceNumber << ','
                 << priority << ','
                 << vlanId << ','
                 << flowSequenceNumber << ','
                 << packet->getDataLength().get<B>() << '\n';

    receivedPackets++;
    emit(packetReceivedSignal, packet);
    delete packet;
}

long RecordFileEthernetSink::extractSequenceNumber(const Packet *packet) const
{
    if (auto sequenceNumberInd = packet->findTag<SequenceNumberInd>())
        return sequenceNumberInd->getSequenceNumber();
    if (auto sequenceNumberReq = packet->findTag<SequenceNumberReq>())
        return sequenceNumberReq->getSequenceNumber();
    if (auto applicationPacket = dynamicPtrCast<const ApplicationPacket>(packet->peekAtFront()))
        return applicationPacket->getSequenceNumber();
    long sequenceNumber = extractTrailingSequenceNumber(packet->getName());
    return sequenceNumber >= 0 ? sequenceNumber : receivedPackets;
}

int RecordFileEthernetSink::extractPriority(const Packet *packet) const
{
    if (auto pcpInd = packet->findTag<PcpInd>())
        return pcpInd->getPcp();
    if (auto pcpReq = packet->findTag<PcpReq>())
        return pcpReq->getPcp();
    if (auto userPriorityInd = packet->findTag<UserPriorityInd>())
        return userPriorityInd->getUserPriority();
    if (auto userPriorityReq = packet->findTag<UserPriorityReq>())
        return userPriorityReq->getUserPriority();
    return -1;
}

int RecordFileEthernetSink::extractVlanId(const Packet *packet) const
{
    if (auto vlanInd = packet->findTag<VlanInd>())
        return vlanInd->getVlanId();
    if (auto vlanReq = packet->findTag<VlanReq>())
        return vlanReq->getVlanId();
    return -1;
}

long RecordFileEthernetSink::extractTrailingSequenceNumber(const char *packetName) const
{
    static const std::regex trailingNumberRegex(R"(.*-(\d+)$)");
    std::cmatch match;
    if (packetName != nullptr && std::regex_match(packetName, match, trailingNumberRegex))
        return std::stol(match[1].str());
    return -1;
}

long RecordFileEthernetSink::getNextFlowSequenceNumber(int vlanId)
{
    return nextFlowSequenceNumbers[vlanId]++;
}

void RecordFileEthernetSink::socketErrorArrived(EthernetSocket *socket, Indication *indication)
{
    EV_WARN << "Ignoring Ethernet socket error " << indication->getName() << EV_ENDL;
    delete indication;
}

void RecordFileEthernetSink::socketClosed(EthernetSocket *socket)
{
    if (operationalState == State::STOPPING_OPERATION)
        startActiveOperationExtraTimeOrFinish(par("stopOperationExtraTime"));
}

void RecordFileEthernetSink::closeRecordFile()
{
    if (recordStream.is_open()) {
        recordStream.flush();
        recordStream.close();
    }
}

void RecordFileEthernetSink::finish()
{
    closeRecordFile();
    recordScalar("received packets", receivedPackets);
    ApplicationBase::finish();
}

} // namespace satnet3d
