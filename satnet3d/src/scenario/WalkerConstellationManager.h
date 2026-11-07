#ifndef __SATNET3D_SCENARIO_WALKERCONSTELLATIONMANAGER_H
#define __SATNET3D_SCENARIO_WALKERCONSTELLATIONMANAGER_H

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <omnetpp.h>

#include "inet/common/scenario/ScenarioManager.h"
#include "inet/common/geometry/common/GeographicCoordinateSystem.h"
#include "inet/mobility/contract/IMobility.h"

#include "TopologyCoordinator.h"

using namespace omnetpp;
using namespace inet;

namespace satnet3d {

class WalkerConstellationManager : public ScenarioManager
{
  protected:
    struct LinkSpec {
        int srcIndex;
        int srcGateIndex;
        int destIndex;
        int destGateIndex;
        bool intraPlane;
    };
    struct PolarLink {
        int srcIndex;
        int srcGateIndex;
        int destIndex;
        int destGateIndex;
        bool connected = true;
    };

  protected:
    std::vector<cModule *> satellites;
    std::vector<PolarLink> polarLinks;
    TopologyCoordinator *topologyCoordinator = nullptr;
    inet::IGeographicCoordinateSystem *coordinateSystem = nullptr;
    cMessage *polarLinkUpdateTimer = nullptr;
    bool isWalkerStar = false;
    double polarRegionLatitude = 75.0;
    simtime_t polarLinkUpdateInterval = SIMTIME_ZERO;

  protected:
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(cMessage *message) override;
    virtual void finish() override;
    virtual cModule *resolveTargetNetworkModule() const;

    virtual void generateConstellation(cModule *network);

    virtual void createSatelliteNodes(cModule *network, std::vector<cModule *>& createdSatellites);
    virtual cModule *createSingleSatelliteNode(cModule *network, int numEthInterfaces);
    virtual void configureSatelliteMobility(cModule *satellite, int satelliteIndex, int totalSatellites);
    virtual double getRaanSpreadDegrees() const;
    virtual void configureWalkerMobility(cModule *satellite, int satelliteIndex, int totalSatellites);

    virtual std::vector<LinkSpec> buildLinkTable(int totalSatellites) const;
    virtual std::vector<LinkSpec> buildOneStarFourLinks(int totalSatellites) const;
    virtual void createSatelliteLinks(const std::vector<cModule *>& createdSatellites);
    virtual void connectPair(cModule *a, int gateIndexA, cModule *b, int gateIndexB, bool intraPlane);
    virtual void setPolarLinkConnected(PolarLink& link, bool connected);
    virtual bool isInPolarRegionAtStart(int satelliteIndex) const;
    virtual bool isInPolarRegion(int satelliteIndex) const;
    virtual void updatePolarLinks();
    virtual void initializeChannelsRecursively(cModule *module) const;
    virtual int findOrCreateFreeGateIndex(cModule *module) const;

    virtual void setParamViaScenarioManager(const char *modulePath, const char *parName, const std::string& value);

  public:
    virtual ~WalkerConstellationManager();
};

} // namespace satnet3d

#endif
