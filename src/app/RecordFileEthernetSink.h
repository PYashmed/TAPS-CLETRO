#ifndef __SATNET3D_RECORDFILEETHERNETSINK_H
#define __SATNET3D_RECORDFILEETHERNETSINK_H

#include <fstream>
#include <map>
#include <string>

#include "inet/applications/base/ApplicationBase.h"
#include "inet/linklayer/common/MacAddress.h"
#include "inet/linklayer/ethernet/common/EthernetSocket.h"

namespace satnet3d {

class RecordFileEthernetSink : public inet::ApplicationBase, public inet::EthernetSocket::ICallback
{
  protected:
    inet::EthernetSocket socket;
    inet::NetworkInterface *networkInterface = nullptr;
    inet::MacAddress localAddress;
    inet::MacAddress remoteAddress;

    std::ofstream recordStream;
    std::string recordFile;
    bool appendFile = false;
    bool writeHeader = true;
    long receivedPackets = 0;
    std::map<int, long> nextFlowSequenceNumbers;

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessageWhenUp(omnetpp::cMessage *message) override;
    virtual void finish() override;

    virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
    virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
    virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;

    virtual void socketDataArrived(inet::EthernetSocket *socket, inet::Packet *packet) override;
    virtual void socketErrorArrived(inet::EthernetSocket *socket, inet::Indication *indication) override;
    virtual void socketClosed(inet::EthernetSocket *socket) override;

    inet::MacAddress resolveMacAddress(const char *address) const;
    long extractSequenceNumber(const inet::Packet *packet) const;
    int extractPriority(const inet::Packet *packet) const;
    int extractVlanId(const inet::Packet *packet) const;
    long extractTrailingSequenceNumber(const char *packetName) const;
    long getNextFlowSequenceNumber(int vlanId);
    void closeRecordFile();

  public:
    virtual ~RecordFileEthernetSink();
};

} // namespace satnet3d

#endif
