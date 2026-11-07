//
// Copyright (C) 2020 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#ifndef __SATNET3D_ACTIVEPACKETSOURCEMULTIPLEINTERVAL_H
#define __SATNET3D_ACTIVEPACKETSOURCEMULTIPLEINTERVAL_H

#include "inet/queueing/source/ActivePacketSource.h"

namespace satnet3d {

using namespace inet;

class ActivePacketSourceMultipleInterval : public inet::queueing::ActivePacketSource
{
  protected:
    std::vector<clocktime_t> productionIntervalList;
    int productionIntervalNum;
    ClockEvent *productionIntervalSwap = nullptr;
    clocktime_t productionIntervalSwapPeriod;
    clocktime_t productionInterval;
    clocktime_t stopTime = CLOCKTIME_ZERO;
    int curPrdIntvNum;
    int prdIntvNum;
    int pcp = -1;
    int vlanId = -1;

  protected:
    virtual void initialize(int stage) override;
    virtual void handleMessage(cMessage *message) override;
    virtual Packet *createPacket() override;

    virtual void scheduleProductionIntervalSwap();
    virtual void scheduleProductionTimerAndProducePacket() override;
    virtual bool isBeforeStopTime(clocktime_t time) const;
  public:
    virtual ~ActivePacketSourceMultipleInterval() { cancelAndDeleteClockEvent(productionIntervalSwap); }

};

} // namespace satnet3d

#endif
