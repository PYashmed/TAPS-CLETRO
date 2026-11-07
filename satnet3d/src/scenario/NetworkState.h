#ifndef __SATNET3D_SCENARIO_NETWORKSTATE_H
#define __SATNET3D_SCENARIO_NETWORKSTATE_H

#include <cstdint>
#include <string>
#include <vector>

#include <omnetpp.h>

namespace satnet3d {

struct NetworkInterfaceState {
    std::string name;
    std::string macAddress;
};

struct NetworkNodeState {
    int moduleId = -1;
    std::string name;
    std::string role;
    double latitude = 0;
    double longitude = 0;
    double altitude = 0;
    ///////TJH
    //补充卫星相关信息
    bool isSatellite = false;
    double orbitalInclination = 0;
    double raan = 0;
    double phase = 0;
    bool isAscending = false;
    ///////
    std::vector<NetworkInterfaceState> interfaces;
};

struct NetworkLinkState {
    int sourceModuleId = -1;
    int destinationModuleId = -1;
    std::string sourceNode;
    std::string sourceGate;
    std::string destinationNode;
    std::string destinationGate;
    double sourceToDestinationDelay = 0;
    double destinationToSourceDelay = 0;
    double sourceToDestinationDatarate = -1;
    double destinationToSourceDatarate = -1;
    std::string owner;
};

struct TrafficFlowState {
    std::string flowId;
    std::string sourceNode;
    std::string destinationNode;
    int priority = -1;
    int packetSize = -1;
    double period = -1;
    double startTime = -1;
    double endTime = -1;
    double deadline = -1;
    double maxJitter = -1;
};

struct NetworkSnapshot {
    uint64_t topologyVersion = 0;
    uint64_t positionVersion = 0;
    uint64_t delayVersion = 0;
    uint64_t linkMetricVersion = 0;
    omnetpp::simtime_t simulationTime = SIMTIME_ZERO;
    std::vector<NetworkNodeState> nodes;
    std::vector<NetworkLinkState> links;
    std::vector<TrafficFlowState> flows;
};

} // namespace satnet3d

#endif
