#ifndef __SATNET3D_SCENARIO_TOPOLOGYMANAGER_H
#define __SATNET3D_SCENARIO_TOPOLOGYMANAGER_H

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <omnetpp.h>

#include "inet/common/scenario/ScenarioManager.h"
#include "inet/mobility/contract/IMobility.h"

using namespace omnetpp;
using namespace inet;

namespace satnet3d {

class TopologyCoordinator;

class TopologyManager : public ScenarioManager
{
  protected:
    struct LinkInfo {
        int sourceModuleId = -1;
        int targetModuleId = -1;
        int sourceGateIndex = -1;
        int targetGateIndex = -1;
    };

  protected:
    cMessage *updateTimer = nullptr;
    std::map<std::pair<int, int>, LinkInfo> activeLinks;
    bool enabled = true;
    bool autoMaintain = true;
    simtime_t initialUpdateOffset = SIMTIME_ZERO;
    simtime_t updateInterval = SIMTIME_ZERO;
    int updateSchedulingPriority = 0;
    TopologyCoordinator *topologyCoordinator = nullptr;

  protected:
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
    virtual void scheduleNextUpdate(simtime_t fromTime);

    virtual void maintainInterLayerLinks();
    virtual cModule *getRequiredLayer(const char *path) const;
    virtual std::vector<cModule *> collectLayerNodes(cModule *layerModule, const char *typePattern, const char *namePattern) const;
    virtual bool matchesNodeFilters(cModule *node, const char *typePattern, const char *namePattern) const;
    virtual IMobility *findMobility(cModule *node) const;

    virtual std::map<std::pair<int, int>, LinkInfo> buildDesiredLinks(const std::vector<cModule *>& primaryNodes, const std::vector<cModule *>& peerNodes) const;
    virtual void removeObsoleteLinks(const std::map<std::pair<int, int>, LinkInfo>& desiredLinks);
    virtual void addMissingLinks(const std::map<std::pair<int, int>, LinkInfo>& desiredLinks);

    virtual void connectLink(LinkInfo& link);
    virtual void disconnectLink(const LinkInfo& link);
    virtual int findOrCreateFreeGateIndex(cModule *module, bool primarySide) const;
    virtual int countFreeGates(cModule *module, bool primarySide) const;
    virtual int getGateRangeStart(bool primarySide) const;
    virtual int getGateRangeEnd(cModule *module, bool primarySide) const;

    virtual cModule *findModuleById(int moduleId) const;

    virtual double computeDistance(cModule *sourceNode, cModule *targetNode) const;
    virtual bool isWithinRange(double distance) const;
};

} // namespace satnet3d

#endif