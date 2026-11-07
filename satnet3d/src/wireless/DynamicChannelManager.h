#ifndef __SATNET3D_WIRELESS_DYNAMICCHANNELMANAGER_H
#define __SATNET3D_WIRELESS_DYNAMICCHANNELMANAGER_H

#include <set>
#include <string>
#include <vector>

#include <omnetpp.h>

using namespace omnetpp;

namespace satnet3d {

class DynamicChannel;

class DynamicChannelManager : public cSimpleModule
{
  protected:
    bool enabled = true;
    simtime_t updateInterval = SIMTIME_ZERO;
    int schedulingPriority = 0;
    cMessage *updateTimer = nullptr;
    std::set<DynamicChannel *> channels;
    bool reportManagedChannels = true;
    double wallTotalSeconds = 0;
    double wallCollectSeconds = 0;
    double wallComputeSeconds = 0;
    double wallApplySeconds = 0;

  protected:
    virtual int numInitStages() const override;
    virtual void initialize(int stage) override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
    virtual void scheduleNext();
    virtual void updateChannels(bool force = false);
    virtual bool updateChannel(DynamicChannel *channel, bool force = false);
    virtual void reportChannelCoverage();
    virtual std::string classifyChannel(DynamicChannel *channel) const;

  public:
    virtual ~DynamicChannelManager();

    virtual bool isEnabled() const { return enabled; }
    static bool isLiveManager(DynamicChannelManager *manager);
    virtual bool registerChannel(DynamicChannel *channel);
    virtual void unregisterChannel(DynamicChannel *channel);
};

} // namespace satnet3d

#endif
