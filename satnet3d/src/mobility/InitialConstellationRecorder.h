#ifndef __SATNET3D_INITIALCONSTELLATIONRECORDER_H
#define __SATNET3D_INITIALCONSTELLATIONRECORDER_H

#include <omnetpp.h>

#include "inet/common/InitStages.h"

namespace satnet3d {

class InitialConstellationRecorder : public omnetpp::cSimpleModule
{
  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(omnetpp::cMessage *message) override;
};

} // namespace satnet3d

#endif
