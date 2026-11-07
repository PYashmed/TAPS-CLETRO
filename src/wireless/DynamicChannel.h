#ifndef __SATNET3D_WIRELESS_DYNAMICCHANNEL_H
#define __SATNET3D_WIRELESS_DYNAMICCHANNEL_H

#include <omnetpp.h>
#include "inet/common/INETDefs.h"
#include "inet/common/ModuleAccess.h"
#include "inet/mobility/contract/IMobility.h"

using namespace omnetpp;
using namespace inet;

namespace satnet3d {

class DynamicChannelManager;

class DynamicChannel : public cDatarateChannel, public cListener
{
  public:
    struct DelayUpdateInput {
        DynamicChannel *channel = nullptr;
        Coord sourcePosition = Coord::ZERO;
        Coord destinationPosition = Coord::ZERO;
        double propagationSpeed = 0;
        bool valid = false;
    };

  protected:
    cModule *sourceModule = nullptr;
    cModule *destinationModule = nullptr;
    IMobility *sourceMobility = nullptr;
    IMobility *destinationMobility = nullptr;
    bool autoUpdateDelay = true;
    bool useDynamicChannelManager = false;
    bool managedByDynamicChannelManager = false;
    bool subscribed = false;
    bool updatingDelay = false;
    cComponent *sourceMobilityComponent = nullptr;
    cComponent *destinationMobilityComponent = nullptr;
    Coord sourcePosition = Coord::ZERO;
    Coord destinationPosition = Coord::ZERO;
    bool sourcePositionValid = false;
    bool destinationPositionValid = false;
    simtime_t dynamicDelay = SIMTIME_ZERO;
    bool dynamicDelayValid = false;
    double propagationSpeed = 299792458.0; // m/s
    simtime_t minUpdateInterval = SIMTIME_ZERO;
    simtime_t lastDelayUpdateTime = -1;
    DynamicChannelManager *dynamicChannelManager = nullptr;
    virtual bool initializeChannel(int stage) override;
    virtual Result processMessage(cMessage *msg, const SendOptions& options, simtime_t t) override;
    virtual void resolveMobilities();
    virtual void updateDelay(bool force = false);
    virtual void setCachedDelay(double delay, simtime_t updateTime);
    virtual bool registerWithDynamicChannelManager();
    virtual void receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details) override;
    cModule* getMobilityModule(cModule *module);
  public:
    DynamicChannel();
    virtual ~DynamicChannel();

    virtual bool isDisabled() const override;
    virtual simtime_t getDelay() const override;
    virtual double getDatarate() const override;
    virtual double getBitErrorRate() const override;
    virtual double getPacketErrorRate() const override;
    virtual void managedUpdateDelay();
    // Refreshes the physical propagation-delay truth at the current simulation time,
    // bypassing minUpdateInterval, and returns the refreshed value.
    virtual simtime_t refreshAndGetDelay();
    // Predicts propagation delay at a future simulation time using analytic
    // circular-orbit state where available, otherwise constant velocity.
    virtual simtime_t predictDelayAt(simtime_t targetTime) const;
    virtual bool collectManagedDelayUpdateInput(DelayUpdateInput& input, bool force = false);
    virtual void applyManagedDelay(double delay, simtime_t updateTime);
    virtual cModule *getSourceNodeModule() const;
    virtual cModule *getDestinationNodeModule() const;
};

} // namespace satnet3d

#endif
