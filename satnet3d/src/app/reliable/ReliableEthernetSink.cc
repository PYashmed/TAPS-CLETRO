// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ReliableEthernetSink.h"

#include <cstdio>
#include <iomanip>

#include "ReliableEthernetPacket_m.h"
#include "ReliableFlowCoordinator.h"

#include "inet/common/ModuleAccess.h"
#include "inet/common/Protocol.h"
#include "inet/common/socket/SocketTag_m.h"
#include "inet/linklayer/common/PcpTag_m.h"
#include "inet/linklayer/common/VlanTag_m.h"
#include "inet/linklayer/ieee8021q/Ieee8021qTagHeader_m.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/networklayer/contract/IInterfaceTable.h"

namespace satnet3d {

using namespace inet;
using namespace omnetpp;

Define_Module(ReliableEthernetSink);

simsignal_t ReliableEthernetSink::simulatedDropSignal = registerSignal("simulatedDrop");
simsignal_t ReliableEthernetSink::duplicatePacketSignal = registerSignal("duplicatePacket");
simsignal_t ReliableEthernetSink::packetDeliveredSignal = registerSignal("packetDelivered");
simsignal_t ReliableEthernetSink::reliableEndToEndDelaySignal = registerSignal("reliableEndToEndDelay");
simsignal_t ReliableEthernetSink::attemptNetworkDelaySignal = registerSignal("attemptNetworkDelay");
simsignal_t ReliableEthernetSink::deliveryAttemptSignal = registerSignal("deliveryAttempt");

ReliableEthernetSink::ReliableEthernetSink()
{
}

ReliableEthernetSink::~ReliableEthernetSink()
{
    closeRecordFile();
}

void ReliableEthernetSink::initialize(int stage)
{
    ApplicationBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        acceptedFlowId = par("acceptedFlowId").intValue();
        lossModel = par("lossModel").stdstringValue();
        firstAttemptDropProbability = par("firstAttemptDropProbability");
        legacyBlockSize = par("legacyBlockSize");
        legacyDropsPerBlock = par("legacyDropsPerBlock");
        legacyFirstSequenceNumber = par("legacyFirstSequenceNumber");
        legacyMaxSequenceNumber = par("legacyMaxSequenceNumber");
        legacyAllowDuplicateSelections = par("legacyAllowDuplicateSelections");
        rngIndex = par("rngIndex");
        recordFile = par("recordFile").stdstringValue();
        appendFile = par("appendFile");
        writeHeader = par("writeHeader");

        if (lossModel != "none" && lossModel != "legacyBlock" && lossModel != "bernoulli")
            throw cRuntimeError("Unknown lossModel '%s'", lossModel.c_str());
        if (firstAttemptDropProbability < 0 || firstAttemptDropProbability > 1)
            throw cRuntimeError("firstAttemptDropProbability must be in the range 0..1");
        if (legacyBlockSize <= 0 || legacyDropsPerBlock < 0)
            throw cRuntimeError("legacyBlockSize must be positive and legacyDropsPerBlock must be nonnegative");
        if (!legacyAllowDuplicateSelections && legacyDropsPerBlock > legacyBlockSize)
            throw cRuntimeError("legacyDropsPerBlock cannot exceed legacyBlockSize when duplicate selections are disabled");
        if (legacyFirstSequenceNumber < 0 || legacyMaxSequenceNumber < -1 ||
                (legacyMaxSequenceNumber >= 0 && legacyMaxSequenceNumber < legacyFirstSequenceNumber))
            throw cRuntimeError("Invalid legacy sequence number range");
        if (rngIndex < 0 || rngIndex >= getEnvir()->getNumRNGs())
            throw cRuntimeError("rngIndex %d is outside the configured RNG range", rngIndex);
        if (recordFile.empty())
            throw cRuntimeError("recordFile must not be empty");

        std::ios::openmode mode = std::ios::out | (appendFile ? std::ios::app : std::ios::trunc);
        recordStream.open(recordFile, mode);
        if (!recordStream.is_open())
            throw cRuntimeError("Cannot open reliable Ethernet record file '%s'", recordFile.c_str());
        recordStream << std::fixed << std::setprecision(6);
        if (writeHeader && !appendFile)
            recordStream << "simTime,endToEndDelay,attemptNetworkDelay,flowId,sequenceNumber,attempt,priority,vlanId,outcome\n";

        WATCH(receivedAttempts);
        WATCH(simulatedDrops);
        WATCH(duplicatePackets);
        WATCH(deliveredPacketCount);
    }
    else if (stage == INITSTAGE_NETWORK_INTERFACE_CONFIGURATION + 1) {
        auto interfaceTable = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        networkInterface = interfaceTable->findInterfaceByName(par("interface"));
        if (networkInterface == nullptr)
            throw cRuntimeError("Cannot find network interface '%s'", par("interface").stringValue());

        const char *localAddressString = par("localAddress");
        localAddress = *localAddressString == '\0' ? networkInterface->getMacAddress() : resolveMacAddress(localAddressString);
        remoteAddress = resolveMacAddress(par("remoteAddress"));
        coordinator = getModuleFromPar<ReliableFlowCoordinator>(par("coordinatorModule"), this);
    }
}

MacAddress ReliableEthernetSink::resolveMacAddress(const char *address) const
{
    if (address == nullptr || *address == '\0')
        return MacAddress();

    L3Address result;
    L3AddressResolver resolver;
    if (resolver.tryResolve(address, result, L3AddressResolver::ADDR_MAC) && result.getType() == L3Address::MAC)
        return result.toMac();
    return MacAddress(address);
}

void ReliableEthernetSink::ensureLegacyBlockInitialized(uint64_t sequenceNumber)
{
    uint64_t firstSequenceNumber = static_cast<uint64_t>(legacyFirstSequenceNumber);
    if (sequenceNumber < firstSequenceNumber)
        return;

    uint64_t blockIndex = (sequenceNumber - firstSequenceNumber) / static_cast<uint64_t>(legacyBlockSize);
    if (!initializedLegacyBlocks.insert(blockIndex).second)
        return;

    uint64_t blockStart = firstSequenceNumber + blockIndex * static_cast<uint64_t>(legacyBlockSize);
    std::set<int> selectedOffsets;
    for (int i = 0; i < legacyDropsPerBlock; i++) {
        int offset;
        do
            offset = intuniform(0, legacyBlockSize - 1, rngIndex);
        while (!legacyAllowDuplicateSelections && selectedOffsets.count(offset) != 0);
        selectedOffsets.insert(offset);

        uint64_t droppedSequenceNumber = blockStart + static_cast<uint64_t>(offset);
        if (legacyMaxSequenceNumber < 0 || droppedSequenceNumber <= static_cast<uint64_t>(legacyMaxSequenceNumber))
            legacyDroppedSequences.insert(droppedSequenceNumber);
    }
}

bool ReliableEthernetSink::shouldDropFirstAttempt(uint64_t flowId, uint64_t sequenceNumber, unsigned int attempt)
{
    if (attempt != 1 || lossModel == "none")
        return false;

    if (lossModel == "legacyBlock" &&
            (sequenceNumber < static_cast<uint64_t>(legacyFirstSequenceNumber) ||
             (legacyMaxSequenceNumber >= 0 && sequenceNumber > static_cast<uint64_t>(legacyMaxSequenceNumber))))
        return false;

    PacketKey key(flowId, sequenceNumber);
    auto existing = firstAttemptDropDecisions.find(key);
    if (existing != firstAttemptDropDecisions.end())
        return existing->second;

    bool drop;
    if (lossModel == "legacyBlock") {
        ensureLegacyBlockInitialized(sequenceNumber);
        drop = legacyDroppedSequences.count(sequenceNumber) != 0;
    }
    else
        drop = uniform(0, 1, rngIndex) < firstAttemptDropProbability;
    firstAttemptDropDecisions[key] = drop;
    return drop;
}

void ReliableEthernetSink::configureSocket()
{
    socket.setCallback(this);
    socket.setNetworkInterface(networkInterface);
    socket.setOutputGate(gate("socketOut"));
    const char *protocolName = par("protocol");
    const Protocol *protocol = *protocolName == '\0' ? nullptr : Protocol::getProtocol(protocolName);
    socket.bind(localAddress, remoteAddress, protocol, par("steal"));
}

void ReliableEthernetSink::handleStartOperation(LifecycleOperation *operation)
{
    configureSocket();
}

void ReliableEthernetSink::handleStopOperation(LifecycleOperation *operation)
{
    socket.close();
    delayActiveOperationFinish(par("stopOperationTimeout"));
}

void ReliableEthernetSink::handleCrashOperation(LifecycleOperation *operation)
{
    socket.destroy();
}

void ReliableEthernetSink::handleMessageWhenUp(cMessage *message)
{
    if (socket.belongsToSocket(message))
        socket.processMessage(message);
    else
        throw cRuntimeError("Unexpected message %s", message->getFullName());
}

void ReliableEthernetSink::socketDataArrived(EthernetSocket *socket, Packet *packet)
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
    receivedAttempts++;
    emit(packetReceivedSignal, packet);

    auto header = packet->peekAtFront<ReliableEthernetHeader>();
    uint64_t flowId = header->getFlowId();
    uint64_t sequenceNumber = header->getSequenceNumber();
    unsigned int attempt = header->getAttempt();
    if (acceptedFlowId >= 0 && flowId != static_cast<uint64_t>(acceptedFlowId))
        throw cRuntimeError("Received flow %llu, but acceptedFlowId is %lld",
                static_cast<unsigned long long>(flowId), static_cast<long long>(acceptedFlowId));

    PacketKey key(flowId, sequenceNumber);
    if (shouldDropFirstAttempt(flowId, sequenceNumber, attempt)) {
        recordPacket(packet, *header, "simulatedDrop");
        simulatedDrops++;
        emit(simulatedDropSignal, packet);
        delete packet;
        return;
    }

    if (deliveredPackets.count(key) != 0) {
        recordPacket(packet, *header, "duplicate");
        duplicatePackets++;
        emit(duplicatePacketSignal, packet);
        delete packet;
        return;
    }

    deliveredPackets.insert(key);
    deliveredPacketCount++;
    recordPacket(packet, *header, "delivered");
    emit(reliableEndToEndDelaySignal, simTime() - header->getOriginalCreationTime());
    emit(attemptNetworkDelaySignal, simTime() - header->getLastTransmissionTime());
    emit(deliveryAttemptSignal, static_cast<long>(attempt));
    emit(packetDeliveredSignal, packet);
    coordinator->reportDelivery(flowId, sequenceNumber);
    delete packet;
}

void ReliableEthernetSink::recordPacket(const Packet *packet, const ReliableEthernetHeader& header, const char *outcome)
{
    int priority = -1;
    if (auto pcpInd = packet->findTag<PcpInd>())
        priority = pcpInd->getPcp();
    else if (auto pcpReq = packet->findTag<PcpReq>())
        priority = pcpReq->getPcp();

    int vlanId = -1;
    if (auto vlanInd = packet->findTag<VlanInd>())
        vlanId = vlanInd->getVlanId();
    else if (auto vlanReq = packet->findTag<VlanReq>())
        vlanId = vlanReq->getVlanId();

    recordStream << simTime().dbl() << ','
                 << (simTime() - header.getOriginalCreationTime()).dbl() << ','
                 << (simTime() - header.getLastTransmissionTime()).dbl() << ','
                 << header.getFlowId() << ','
                 << header.getSequenceNumber() << ','
                 << header.getAttempt() << ','
                 << priority << ','
                 << vlanId << ','
                 << outcome << '\n';
}

void ReliableEthernetSink::closeRecordFile()
{
    if (recordStream.is_open()) {
        recordStream.flush();
        recordStream.close();
    }
}

void ReliableEthernetSink::socketErrorArrived(EthernetSocket *socket, Indication *indication)
{
    EV_WARN << "Ignoring Ethernet socket error " << indication->getName() << "\n";
    delete indication;
}

void ReliableEthernetSink::socketClosed(EthernetSocket *socket)
{
    if (operationalState == State::STOPPING_OPERATION)
        startActiveOperationExtraTimeOrFinish(par("stopOperationExtraTime"));
}

void ReliableEthernetSink::finish()
{
    closeRecordFile();
    recordScalar("received attempts", receivedAttempts);
    recordScalar("simulated drops", simulatedDrops);
    recordScalar("duplicate packets", duplicatePackets);
    recordScalar("delivered packets", deliveredPacketCount);
    ApplicationBase::finish();
}

void ReliableEthernetSink::refreshDisplay() const
{
    ApplicationBase::refreshDisplay();
    char text[128];
    snprintf(text, sizeof(text), "attempts: %ld\ndelivered: %ld\ndropped: %ld\nduplicates: %ld",
            receivedAttempts, deliveredPacketCount, simulatedDrops, duplicatePackets);
    getDisplayString().setTagArg("t", 0, text);
}

} // namespace satnet3d
