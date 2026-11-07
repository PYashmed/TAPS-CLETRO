#ifndef __SATNET3D_PERIODICGATEMANAGER_H
#define __SATNET3D_PERIODICGATEMANAGER_H

#include <set>
#include <vector>

#include <omnetpp.h>

#include "inet/common/InitStages.h"
#include "inet/clock/contract/ClockTime.h"

namespace satnet3d {
namespace queueing {

class SatNet3DPeriodicGate;

class PeriodicGateManager : public omnetpp::cSimpleModule
{
  protected:
    bool enabled = true;
    bool processingBatch = false;
    int schedulingPriority = 0;
    omnetpp::cMessage *batchTimer = nullptr;
    double wallBatchTotalSeconds = 0;
    double wallDueScanSeconds = 0;
    double wallProcessSeconds = 0;
    double wallScheduleScanSeconds = 0;
    std::vector<SatNet3DPeriodicGate *> gates;

    static std::set<const PeriodicGateManager *> liveManagers;

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(omnetpp::cMessage *message) override;
    virtual void finish() override;

    void scheduleNextBatch();

  public:
    virtual ~PeriodicGateManager();

    bool isEnabled() const { return enabled; }
    void registerGate(SatNet3DPeriodicGate *gate);
    void unregisterGate(SatNet3DPeriodicGate *gate);
    void requestReschedule();

    static bool isLiveManager(const PeriodicGateManager *manager);
};

} // namespace queueing
} // namespace satnet3d

#endif
