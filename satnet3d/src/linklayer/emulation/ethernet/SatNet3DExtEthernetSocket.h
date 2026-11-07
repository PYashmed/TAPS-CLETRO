#ifndef __SATNET3D_EXTETHERNETSOCKET_H
#define __SATNET3D_EXTETHERNETSOCKET_H

#include <vector>
#include <deque>
#include <cstdint>
#include <string>

#include <omnetpp/platdep/sockets.h>

#include "inet/emulation/linklayer/ethernet/ExtEthernetSocket.h"
#include "inet/common/packet/Packet.h"
#include "../../../common/scheduler/SatNet3DExternalPollable.h"

namespace satnet3d {

class SatNet3DExtEthernetSocket : public inet::ExtEthernetSocket, public SatNet3DExternalPollable
{
  protected:
    bool promiscuousMode = false;
    bool preserveExternalRxTiming = true;
    int externalRxQueueLimit = 100000;
    int64_t timingQueueDrops = 0;
    std::vector<uint8_t> acceptedMacPrefix;
    std::string makeFrameKey(const uint8_t *frame, int length) const;
    void rememberTransmit(const std::string& key);
    void recordReceive(const std::string& key, int64_t rxNs);
    void drainTransmitTimestamps();
    struct PendingPacket {
        inet::Packet *packet = nullptr;
        omnetpp::simtime_t releaseTime;
    };
    std::deque<PendingPacket> pendingPackets;
    omnetpp::cMessage *releaseTimer = nullptr;
    int64_t batchBaseRxNs = 0;
    omnetpp::simtime_t batchBaseSimTime;
    omnetpp::simtime_t lastReleaseTime;
    void enqueueReceived(inet::Packet *packet, int64_t rxNs);
    void releaseDuePackets();

    virtual void initialize(int stage) override;
    virtual void handleMessage(omnetpp::cMessage *message) override;
    virtual void openSocket() override;
    virtual void finish() override;
    virtual bool notify(int fd) override;
    virtual bool pollExternal(int maxPackets) override;

    virtual void readFilterParameters();
    virtual bool matchesAcceptedMacPrefix(const uint8_t *frame, int length) const;
};

} // namespace satnet3d

#endif
