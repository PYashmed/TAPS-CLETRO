// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef SATNET3D_APP_RELIABLE_RELIABLEFLOWCOORDINATOR_H_
#define SATNET3D_APP_RELIABLE_RELIABLEFLOWCOORDINATOR_H_

#include <cstdint>
#include <map>

#include <omnetpp.h>

namespace satnet3d {

class ReliableFlowCoordinator : public omnetpp::cSimpleModule
{
  protected:
    std::map<uint64_t, int> sourceModuleIds;

  protected:
    virtual void handleMessage(omnetpp::cMessage *message) override;

  public:
    void registerSource(uint64_t flowId, int sourceModuleId);
    void reportDelivery(uint64_t flowId, uint64_t sequenceNumber);
};

} // namespace satnet3d

#endif
