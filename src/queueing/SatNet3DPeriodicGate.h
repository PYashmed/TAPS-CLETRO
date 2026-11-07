#ifndef __SATNET3D_SATNET3DPERIODICGATE_H
#define __SATNET3D_SATNET3DPERIODICGATE_H

#include "inet/common/clock/ClockUserModuleMixin.h"
#include "inet/queueing/base/PacketGateBase.h"

namespace satnet3d {
namespace queueing {

class PeriodicGateManager;

class SatNet3DPeriodicGate : public inet::ClockUserModuleMixin<inet::queueing::PacketGateBase>
{
  public:
    static omnetpp::simsignal_t guardBandStateChangedSignal;

  protected:
    int index = 0;
    bool initiallyOpen = false;
    inet::clocktime_t initialOffset;
    inet::clocktime_t offset;
    inet::clocktime_t totalDuration = 0;
    std::vector<inet::clocktime_t> durations;
    bool scheduleForAbsoluteTime = false;
    bool enableImplicitGuardBand = true;
    int openSchedulingPriority = 0;
    int closeSchedulingPriority = 0;
    bool isInGuardBand_ = false;

    bool usePeriodicGateManager = true;
    PeriodicGateManager *periodicGateManager = nullptr;
    inet::ClockEvent *changeTimer = nullptr;
    bool managedChangeScheduled = false;
    inet::clocktime_t nextChangeClockTime = inet::CLOCKTIME_ZERO;

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void finish() override;
    virtual void handleMessage(omnetpp::cMessage *message) override;
    virtual void handleParameterChange(const char *name) override;
    virtual bool canPacketFlowThrough(inet::Packet *packet) const override;

    virtual void initializeGating();
    virtual void scheduleChangeTimer();
    virtual void processChangeTimer();
    virtual void readDurationsPar();
    virtual void updateIsInGuardBand();
    virtual void registerWithManager();

  public:
    virtual ~SatNet3DPeriodicGate();

    virtual inet::clocktime_t getInitialOffset() const { return initialOffset; }
    virtual bool getInitiallyOpen() const { return initiallyOpen; }
    virtual const std::vector<inet::clocktime_t>& getDurations() const { return durations; }
    virtual bool isInGuardBand() const { return isInGuardBand_; }

    bool hasScheduledManagedChange() const { return managedChangeScheduled; }
    inet::clocktime_t getNextChangeClockTime() const { return nextChangeClockTime; }
    void processManagedChangeTimer();

    virtual void open() override;
    virtual void close() override;
    virtual void handleCanPushPacketChanged(const omnetpp::cGate *gate) override;
    virtual void handleCanPullPacketChanged(const omnetpp::cGate *gate) override;
};

} // namespace queueing
} // namespace satnet3d

#endif
