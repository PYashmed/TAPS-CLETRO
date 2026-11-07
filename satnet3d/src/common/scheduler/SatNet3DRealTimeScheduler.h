#ifndef __SATNET3D_REALTIMESCHEDULER_H
#define __SATNET3D_REALTIMESCHEDULER_H

#include <cstdint>
#include "inet/common/scheduler/RealTimeScheduler.h"

namespace satnet3d {

/**
 * Real-time scheduler that also drains external sockets while the simulation
 * is behind wall clock time. The normal INET scheduling path is unchanged.
 */
class SatNet3DRealTimeScheduler : public inet::RealTimeScheduler
{
  protected:
    int64_t behindPollIntervalNs = 100000; // 100 us
    int maxBatchSize = 256;
    int64_t lastBehindPollNs = 0;

    virtual bool receiveWithTimeout(int64_t timeout) override;
    bool pollBehind();

  public:
    SatNet3DRealTimeScheduler() = default;
    virtual void startRun() override;
    virtual omnetpp::cEvent *takeNextEvent() override;
    void setBehindPollInterval(int64_t nanoseconds);
    void setMaxBatchSize(int packets);
};

} // namespace satnet3d

#endif
