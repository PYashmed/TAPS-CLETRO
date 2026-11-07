// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ReliableEthernetSource.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "ReliableEthernetPacket_m.h"
#include "ReliableFlowCoordinator.h"

#include "inet/common/ModuleAccess.h"
#include "inet/common/Protocol.h"
#include "inet/common/ProtocolUtils.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/linklayer/common/MacAddressTag_m.h"
#include "inet/linklayer/common/PcpTag_m.h"
#include "inet/linklayer/common/VlanTag_m.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/networklayer/contract/IInterfaceTable.h"

namespace satnet3d {

using namespace inet;
using namespace omnetpp;

Define_Module(ReliableEthernetSource);

simsignal_t ReliableEthernetSource::packetGeneratedSignal = registerSignal("packetGenerated");
simsignal_t ReliableEthernetSource::packetRetransmittedSignal = registerSignal("packetRetransmitted");
simsignal_t ReliableEthernetSource::deliveryConfirmedSignal = registerSignal("deliveryConfirmed");
simsignal_t ReliableEthernetSource::packetFailedSignal = registerSignal("packetFailed");
simsignal_t ReliableEthernetSource::outstandingPacketCountSignal = registerSignal("outstandingPacketCount");

ReliableEthernetSource::ReliableEthernetSource()
{
}

ReliableEthernetSource::~ReliableEthernetSource()
{
    cancelTraffic();
    cancelAndDelete(generationTimer);
    generationTimer = nullptr;
}

void ReliableEthernetSource::initialize(int stage)
{
    ApplicationBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        int64_t configuredFlowId = par("flowId").intValue();
        int64_t configuredInitialSequenceNumber = par("initialSequenceNumber").intValue();
        if (configuredFlowId < 0)
            throw cRuntimeError("flowId must be nonnegative");
        if (configuredInitialSequenceNumber < 0)
            throw cRuntimeError("initialSequenceNumber must be nonnegative");
        flowId = configuredFlowId;
        nextSequenceNumber = configuredInitialSequenceNumber;
        productionInterval = par("productionInterval");
        retransmissionTimeout = par("retransmissionTimeout");
        startTime = par("startTime");
        stopTime = par("stopTime");
        maxRetransmissions = par("maxRetransmissions");
        pcp = par("pcp");
        vlanId = par("vlanId");
        packetLength = B(par("packetLength"));

        if (productionInterval <= SIMTIME_ZERO)
            throw cRuntimeError("productionInterval must be positive");
        if (retransmissionTimeout <= SIMTIME_ZERO)
            throw cRuntimeError("retransmissionTimeout must be positive");
        if (stopTime >= SIMTIME_ZERO && stopTime < startTime)
            throw cRuntimeError("stopTime must be negative or not earlier than startTime");
        if (packetLength < B(36))
            throw cRuntimeError("packetLength must be at least 36 B");
        if (pcp < -1 || pcp > 7)
            throw cRuntimeError("pcp must be -1 or in the range 0..7");
        if (vlanId < -1 || vlanId > 4094)
            throw cRuntimeError("vlanId must be -1 or in the range 0..4094");

        generationTimer = new cMessage("generatePacket");
        WATCH(flowId);
        WATCH(nextSequenceNumber);
        WATCH(packetsGenerated);
        WATCH(transmissionAttempts);
        WATCH(retransmissions);
        WATCH(packetsDelivered);
        WATCH(packetsFailed);
    }
    else if (stage == INITSTAGE_NETWORK_INTERFACE_CONFIGURATION + 1) {
        auto interfaceTable = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        networkInterface = interfaceTable->findInterfaceByName(par("interface"));
        if (networkInterface == nullptr)
            throw cRuntimeError("Cannot find network interface '%s'", par("interface").stringValue());

        const char *localAddressString = par("localAddress");
        localAddress = *localAddressString == '\0' ? networkInterface->getMacAddress() : resolveMacAddress(localAddressString);
        remoteAddress = resolveMacAddress(par("remoteAddress"));
        if (remoteAddress.isUnspecified())
            throw cRuntimeError("remoteAddress must resolve to a MAC address");

        coordinator = getModuleFromPar<ReliableFlowCoordinator>(par("coordinatorModule"), this);
        coordinator->registerSource(flowId, getId());
    }
}

MacAddress ReliableEthernetSource::resolveMacAddress(const char *address) const
{
    if (address == nullptr || *address == '\0')
        return MacAddress();

    L3Address result;
    L3AddressResolver resolver;
    if (resolver.tryResolve(address, result, L3AddressResolver::ADDR_MAC) && result.getType() == L3Address::MAC)
        return result.toMac();
    return MacAddress(address);
}

void ReliableEthernetSource::configureSocket()
{
    socket.setCallback(this);
    socket.setNetworkInterface(networkInterface);
    socket.setOutputGate(gate("socketOut"));
    socket.bind(localAddress, remoteAddress, &Protocol::unknown, par("steal"));
}

void ReliableEthernetSource::handleStartOperation(LifecycleOperation *operation)
{
    configureSocket();
    scheduleFirstGeneration();
}

void ReliableEthernetSource::handleStopOperation(LifecycleOperation *operation)
{
    cancelTraffic();
    socket.close();
    delayActiveOperationFinish(par("stopOperationTimeout"));
}

void ReliableEthernetSource::handleCrashOperation(LifecycleOperation *operation)
{
    cancelTraffic();
    socket.destroy();
}

void ReliableEthernetSource::scheduleFirstGeneration()
{
    simtime_t first = std::max(simTime(), startTime);
    if ((stopTime < SIMTIME_ZERO || first < stopTime) && !generationTimer->isScheduled())
        scheduleAt(first, generationTimer);
}

void ReliableEthernetSource::scheduleNextGeneration()
{
    simtime_t next = simTime() + productionInterval;
    if (stopTime < SIMTIME_ZERO || next < stopTime)
        scheduleAt(next, generationTimer);
}

void ReliableEthernetSource::handleMessageWhenUp(cMessage *message)
{
    if (message == generationTimer) {
        generatePacket();
        scheduleNextGeneration();
    }
    else if (auto timeoutEvent = dynamic_cast<TimeoutEvent *>(message))
        processTimeout(timeoutEvent);
    else if (message->getArrivalGate() == gate("deliveryIn"))
        processDelivery(message);
    else if (socket.belongsToSocket(message))
        socket.processMessage(message);
    else
        throw cRuntimeError("Unexpected message %s", message->getFullName());
}

void ReliableEthernetSource::generatePacket()
{
    uint64_t sequenceNumber = nextSequenceNumber++;
    TxRecord record;
    record.firstSendTime = simTime();
    auto inserted = outstandingPackets.emplace(sequenceNumber, record);
    if (!inserted.second)
        throw cRuntimeError("Sequence number %llu is already outstanding", static_cast<unsigned long long>(sequenceNumber));

    packetsGenerated++;
    transmit(sequenceNumber);
    emit(outstandingPacketCountSignal, static_cast<long>(outstandingPackets.size()));
}

void ReliableEthernetSource::transmit(uint64_t sequenceNumber)
{
    auto recordIt = outstandingPackets.find(sequenceNumber);
    ASSERT(recordIt != outstandingPackets.end());
    TxRecord& record = recordIt->second;
    record.attempts++;
    record.lastSendTime = simTime();

    std::string packetName = "reliable-" + std::to_string(flowId) + "-" + std::to_string(sequenceNumber) + "-" + std::to_string(record.attempts);
    auto packet = new Packet(packetName.c_str());
    auto header = makeShared<ReliableEthernetHeader>();
    header->setFlowId(flowId);
    header->setSequenceNumber(sequenceNumber);
    header->setAttempt(record.attempts);
    header->setOriginalCreationTime(record.firstSendTime);
    header->setLastTransmissionTime(record.lastSendTime);
    packet->insertAtBack(header);
    if (packetLength > header->getChunkLength())
        packet->insertAtBack(makeShared<ByteCountChunk>(packetLength - header->getChunkLength()));

    packet->addTag<PacketProtocolTag>()->setProtocol(&Protocol::unknown);
    packet->addTag<MacAddressReq>()->setDestAddress(remoteAddress);
    if (pcp >= 0)
        packet->addTag<PcpReq>()->setPcp(pcp);
    if (vlanId >= 0)
        packet->addTag<VlanReq>()->setVlanId(vlanId);
    if (pcp >= 0 || vlanId >= 0) {
        ensureEncapsulationProtocolReq(packet, &Protocol::ieee8021qCTag);
        setDispatchProtocol(packet);
    }

    if (record.attempts == 1)
        emit(packetGeneratedSignal, packet);
    else {
        retransmissions++;
        emit(packetRetransmittedSignal, packet);
    }
    transmissionAttempts++;
    emit(packetSentSignal, packet);
    socket.send(packet);

    record.timeoutEvent = new TimeoutEvent(sequenceNumber);
    scheduleAt(simTime() + retransmissionTimeout, record.timeoutEvent);
}

void ReliableEthernetSource::processTimeout(TimeoutEvent *timeoutEvent)
{
    uint64_t sequenceNumber = timeoutEvent->sequenceNumber;
    auto recordIt = outstandingPackets.find(sequenceNumber);
    if (recordIt == outstandingPackets.end()) {
        delete timeoutEvent;
        return;
    }

    TxRecord& record = recordIt->second;
    ASSERT(record.timeoutEvent == timeoutEvent);
    record.timeoutEvent = nullptr;
    delete timeoutEvent;

    int retransmissionsSoFar = static_cast<int>(record.attempts) - 1;
    // stopTime only stops the periodic generation of new sequence numbers.
    // Packets that were generated before stopTime remain outstanding and may
    // be retransmitted during the drain interval until they are delivered,
    // maxRetransmissions is reached, or the simulation ends.
    if (maxRetransmissions >= 0 && retransmissionsSoFar >= maxRetransmissions) {
        packetsFailed++;
        emit(packetFailedSignal, static_cast<long>(sequenceNumber));
        outstandingPackets.erase(recordIt);
        emit(outstandingPacketCountSignal, static_cast<long>(outstandingPackets.size()));
    }
    else
        transmit(sequenceNumber);
}

void ReliableEthernetSource::processDelivery(cMessage *message)
{
    auto indication = check_and_cast<ReliableDeliveryInd *>(message);
    if (indication->getFlowId() != flowId)
        throw cRuntimeError("Received delivery indication for flow %llu, expected %llu",
                static_cast<unsigned long long>(indication->getFlowId()), static_cast<unsigned long long>(flowId));

    auto recordIt = outstandingPackets.find(indication->getSequenceNumber());
    if (recordIt != outstandingPackets.end()) {
        if (recordIt->second.timeoutEvent != nullptr)
            cancelAndDelete(recordIt->second.timeoutEvent);
        packetsDelivered++;
        emit(deliveryConfirmedSignal, static_cast<long>(indication->getSequenceNumber()));
        outstandingPackets.erase(recordIt);
        emit(outstandingPacketCountSignal, static_cast<long>(outstandingPackets.size()));
    }
    delete indication;
}

void ReliableEthernetSource::socketDataArrived(EthernetSocket *socket, Packet *packet)
{
    EV_WARN << "ReliableEthernetSource received an unexpected Ethernet packet; discarding it\n";
    delete packet;
}

void ReliableEthernetSource::socketErrorArrived(EthernetSocket *socket, Indication *indication)
{
    EV_WARN << "Ignoring Ethernet socket error " << indication->getName() << "\n";
    delete indication;
}

void ReliableEthernetSource::socketClosed(EthernetSocket *socket)
{
    if (operationalState == State::STOPPING_OPERATION)
        startActiveOperationExtraTimeOrFinish(par("stopOperationExtraTime"));
}

void ReliableEthernetSource::cancelTraffic()
{
    if (generationTimer != nullptr)
        cancelEvent(generationTimer);
    for (auto& entry : outstandingPackets) {
        if (entry.second.timeoutEvent != nullptr)
            cancelAndDelete(entry.second.timeoutEvent);
    }
    outstandingPackets.clear();
}

void ReliableEthernetSource::finish()
{
    recordScalar("packets generated", packetsGenerated);
    recordScalar("transmission attempts", transmissionAttempts);
    recordScalar("retransmissions", retransmissions);
    recordScalar("packets delivered", packetsDelivered);
    recordScalar("packets failed", packetsFailed);
    recordScalar("packets outstanding", outstandingPackets.size());
    ApplicationBase::finish();
}

void ReliableEthernetSource::refreshDisplay() const
{
    ApplicationBase::refreshDisplay();
    char text[128];
    snprintf(text, sizeof(text), "generated: %ld\ndelivered: %ld\nretransmitted: %ld\noutstanding: %zu",
            packetsGenerated, packetsDelivered, retransmissions, outstandingPackets.size());
    getDisplayString().setTagArg("t", 0, text);
}

} // namespace satnet3d
