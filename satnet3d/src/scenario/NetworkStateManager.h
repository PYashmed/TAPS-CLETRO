#ifndef __SATNET3D_SCENARIO_NETWORKSTATEMANAGER_H
#define __SATNET3D_SCENARIO_NETWORKSTATEMANAGER_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include <omnetpp.h>
#include "inet/mobility/contract/IMobility.h"
#include "../common/AdaptiveGeographicCoordinateSystem.h"
#include "NetworkState.h"

namespace satnet3d {

class NetworkStateManager : public omnetpp::cSimpleModule, public omnetpp::cListener
{
  protected:
    omnetpp::cModule *networkModule = nullptr;
    inet::IGeographicCoordinateSystem *coordinateSystem = nullptr;
    std::string mobilitySubmoduleName;
    std::string linkGateName;
    std::vector<std::string> switchTypePatterns;
    std::vector<std::string> switchNamePatterns;
    std::vector<std::string> hostTypePatterns;
    std::vector<std::string> hostNamePatterns;
    NetworkSnapshot snapshot;
    std::map<int, size_t> nodeIndexById;
    std::set<omnetpp::cComponent *> subscribedMobilities;
    omnetpp::cMessage *delayRefreshTimer = nullptr;
    omnetpp::simtime_t delayRefreshInterval;

  protected:
    virtual int numInitStages() const override;
    virtual void initialize(int stage) override;
    virtual void handleMessage(omnetpp::cMessage *message) override;
    virtual void finish() override;
    virtual void receiveSignal(omnetpp::cComponent *source, omnetpp::simsignal_t signal,
                               omnetpp::cObject *object, omnetpp::cObject *details) override;
    virtual std::vector<std::string> splitPatterns(const char *parameterName) const;
    virtual bool matchesAny(const std::string& value, const std::vector<std::string>& patterns) const;
    virtual inet::IMobility *mobilityOf(omnetpp::cModule *node) const;
    virtual void rebuildSnapshot();
    virtual void collectNodes();
    virtual void collectLinks();
    virtual void collectFlows();
    virtual void refreshLinkDelays();
    virtual void updateNodePosition(omnetpp::cModule *node, inet::IMobility *mobility);

  public:
    virtual const NetworkSnapshot& getNetworkSnapshot() const { return snapshot; }
    virtual void refreshTopologyState() { rebuildSnapshot(); }
};

} // namespace satnet3d

#endif
