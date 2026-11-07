// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef SATNET3D_APP_RELIABLE_RELIABLEETHERNETSINK_H_
#define SATNET3D_APP_RELIABLE_RELIABLEETHERNETSINK_H_

#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "inet/applications/base/ApplicationBase.h"
#include "inet/linklayer/common/MacAddress.h"
#include "inet/linklayer/ethernet/common/EthernetSocket.h"

namespace satnet3d {

class ReliableEthernetHeader;
class ReliableFlowCoordinator;

class ReliableEthernetSink : public inet::ApplicationBase, public inet::EthernetSocket::ICallback
{
  protected:
    using PacketKey = std::pair<uint64_t, uint64_t>;

    inet::EthernetSocket socket;
    inet::NetworkInterface *networkInterface = nullptr;
    inet::MacAddress localAddress;
    inet::MacAddress remoteAddress;
    ReliableFlowCoordinator *coordinator = nullptr;

    int64_t acceptedFlowId = -1;
    std::string lossModel;
    double firstAttemptDropProbability = 0;
    int legacyBlockSize = 50;
    int legacyDropsPerBlock = 2;
    int legacyFirstSequenceNumber = 0;
    int legacyMaxSequenceNumber = -1;
    bool legacyAllowDuplicateSelections = false;
    int rngIndex = 0;

    std::ofstream recordStream;
    std::string recordFile;
    bool appendFile = false;
    bool writeHeader = true;

    std::set<uint64_t> legacyDroppedSequences;
    std::set<uint64_t> initializedLegacyBlocks;
    std::set<PacketKey> deliveredPackets;
    std::map<PacketKey, bool> firstAttemptDropDecisions;

    long receivedAttempts = 0;
    long simulatedDrops = 0;
    long duplicatePackets = 0;
    long deliveredPacketCount = 0;

    static omnetpp::simsignal_t simulatedDropSignal;
    static omnetpp::simsignal_t duplicatePacketSignal;
    static omnetpp::simsignal_t packetDeliveredSignal;
    static omnetpp::simsignal_t reliableEndToEndDelaySignal;
    static omnetpp::simsignal_t attemptNetworkDelaySignal;
    static omnetpp::simsignal_t deliveryAttemptSignal;

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
    void ensureLegacyBlockInitialized(uint64_t sequenceNumber);
    bool shouldDropFirstAttempt(uint64_t flowId, uint64_t sequenceNumber, unsigned int attempt);
    inet::MacAddress resolveMacAddress(const char *address) const;
    void recordPacket(const inet::Packet *packet, const ReliableEthernetHeader& header, const char *outcome);
    void closeRecordFile();

  public:
    ReliableEthernetSink();
    virtual ~ReliableEthernetSink();
};

} // namespace satnet3d

#endif
