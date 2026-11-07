// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef SATNET3D_APP_RELIABLE_RELIABLEETHERNETSOURCE_H_
#define SATNET3D_APP_RELIABLE_RELIABLEETHERNETSOURCE_H_

#include <cstdint>
#include <map>

#include "inet/applications/base/ApplicationBase.h"
#include "inet/linklayer/common/MacAddress.h"
#include "inet/linklayer/ethernet/common/EthernetSocket.h"

namespace satnet3d {

class ReliableFlowCoordinator;

class ReliableEthernetSource : public inet::ApplicationBase, public inet::EthernetSocket::ICallback
{
  protected:
    class TimeoutEvent : public omnetpp::cMessage
    {
      public:
        uint64_t sequenceNumber;

        explicit TimeoutEvent(uint64_t sequenceNumber) : cMessage("retransmissionTimeout"), sequenceNumber(sequenceNumber) {}
    };

    struct TxRecord
    {
        omnetpp::simtime_t firstSendTime;
        omnetpp::simtime_t lastSendTime;
        unsigned int attempts = 0;
        TimeoutEvent *timeoutEvent = nullptr;
    };

    inet::EthernetSocket socket;
    inet::NetworkInterface *networkInterface = nullptr;
    inet::MacAddress localAddress;
    inet::MacAddress remoteAddress;
    ReliableFlowCoordinator *coordinator = nullptr;

    uint64_t flowId = 0;
    uint64_t nextSequenceNumber = 0;
    omnetpp::simtime_t productionInterval;
    omnetpp::simtime_t retransmissionTimeout;
    omnetpp::simtime_t startTime;
    omnetpp::simtime_t stopTime;
    int maxRetransmissions = -1;
    int pcp = -1;
    int vlanId = -1;
    inet::B packetLength;

    omnetpp::cMessage *generationTimer = nullptr;
    std::map<uint64_t, TxRecord> outstandingPackets;

    long packetsGenerated = 0;
    long transmissionAttempts = 0;
    long retransmissions = 0;
    long packetsDelivered = 0;
    long packetsFailed = 0;

    static omnetpp::simsignal_t packetGeneratedSignal;
    static omnetpp::simsignal_t packetRetransmittedSignal;
    static omnetpp::simsignal_t deliveryConfirmedSignal;
    static omnetpp::simsignal_t packetFailedSignal;
    static omnetpp::simsignal_t outstandingPacketCountSignal;

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessageWhenUp(omnetpp::cMessage *message) override;
    virtual void finish() override;
    virtual void refreshDisplay() const override;

    virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
    virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
    virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;

    virtual void socketDataArrived(inet::EthernetSocket *socket, inet::Packet *packet) override;
    virtual void socketErrorArrived(inet::EthernetSocket *socket, inet::Indication *indication) override;
    virtual void socketClosed(inet::EthernetSocket *socket) override;

    void configureSocket();
    void scheduleFirstGeneration();
    void scheduleNextGeneration();
    void generatePacket();
    void transmit(uint64_t sequenceNumber);
    void processTimeout(TimeoutEvent *timeoutEvent);
    void processDelivery(omnetpp::cMessage *message);
    void cancelTraffic();
    inet::MacAddress resolveMacAddress(const char *address) const;

  public:
    ReliableEthernetSource();
    virtual ~ReliableEthernetSource();
};

} // namespace satnet3d

#endif
