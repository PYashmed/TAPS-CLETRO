#ifndef __SATNET3D_MOBILITY_MOBILITYMANAGER_H
#define __SATNET3D_MOBILITY_MOBILITYMANAGER_H

#include <map>
#include <vector>

#include <omnetpp.h>

using namespace omnetpp;

namespace satnet3d {

class GeodeticMobilityBase;

class MobilityManager : public cSimpleModule
{
  protected:
    struct Registration {
        GeodeticMobilityBase *mobility = nullptr;
        simtime_t interval = SIMTIME_ZERO;
    };

    bool enabled = true;
    int schedulingPriority = 0;
    cMessage *updateTimer = nullptr;
    double wallTotalSeconds = 0;
    double wallCollectSeconds = 0;
    double wallMoveSeconds = 0;
    double wallScheduleSeconds = 0;
    std::map<GeodeticMobilityBase *, Registration> registrations;
    std::map<simtime_t, std::vector<GeodeticMobilityBase *>> updateBuckets;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;

    virtual void scheduleNext();
    virtual void scheduleMobility(GeodeticMobilityBase *mobility, simtime_t updateTime);
    virtual void removeFromBuckets(GeodeticMobilityBase *mobility);

  public:
    virtual ~MobilityManager();

    virtual bool isEnabled() const { return enabled; }
    virtual bool registerMobility(GeodeticMobilityBase *mobility, simtime_t interval);
    virtual void unregisterMobility(GeodeticMobilityBase *mobility);
};

} // namespace satnet3d

#endif
