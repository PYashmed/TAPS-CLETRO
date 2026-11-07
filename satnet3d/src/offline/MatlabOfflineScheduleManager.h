#ifndef __SATNET3D_MATLABOFFLINESCHEDULEMANAGER_H
#define __SATNET3D_MATLABOFFLINESCHEDULEMANAGER_H

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <omnetpp.h>

#include "inet/common/InitStages.h"

namespace satnet3d {
namespace offline {

class MatlabOfflineScheduleManager : public omnetpp::cSimpleModule
{
  protected:
    struct OffsetRecord {
        int trafficId = -1;
        double offsetUs = 0;
        bool enabled = false;
    };

    struct RouteRecord {
        std::string nodeName;
        int vlanId = 0;
        std::string destinationName;
        std::string outInterface;
    };

    struct WindowRecord {
        std::string nodeName;
        std::string outInterface;
        std::vector<std::pair<double, double>> windowsUs;
    };

    struct GatePattern {
        bool staticState = false;
        bool firstStateActive = false;
        double offsetSeconds = 0;
        std::vector<double> durationsSeconds;
    };

    omnetpp::cModule *networkModule = nullptr;
    omnetpp::cMessage *sliceTimer = nullptr;
    std::string configurationFile;
    double cycleUs = 0;
    omnetpp::simtime_t sliceInterval;
    int sliceCount = 0;
    int nextSlice = 0;
    int schedulingPriority = -85;
    bool applyGateSchedules = true;
    bool openPcp7AtScheduleEnd = false;
    int drainGateIndex = 7;
    std::string firstQueueState = "10";
    std::string secondQueueState = "01";

    std::map<int, OffsetRecord> offsets;
    std::vector<RouteRecord> routes;
    std::vector<std::vector<WindowRecord>> sliceWindows;
    std::vector<int> satelliteIndexMap;
    std::vector<int> sourceAccessIndexByVlan;
    std::vector<int> destinationAccessIndexByVlan;
    std::vector<int> satelliteGroundInterfaceByVlan;

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(omnetpp::cMessage *message) override;
    virtual void finish() override;

    void loadConfiguration();
    void applyStaticRoutes();
    void applyGateSlice(int slice);
    void applyDrainGateState();
    GatePattern buildGatePattern(const WindowRecord& record) const;
    void applyGatePattern(const WindowRecord& record, int gateIndex, bool firstStateOpen,
                          bool secondStateOpen, const GatePattern& pattern);
    omnetpp::cModule *findTransmissionGate(const WindowRecord& record, int gateIndex) const;

    omnetpp::cModule *findTopLevelModule(const std::string& name) const;
    std::string mapSatelliteName(const std::string& name, bool allowIgnored) const;
    std::string mapRouteNodeName(const RouteRecord& route) const;
    std::string mapRouteOutputInterface(const RouteRecord& route) const;
    static bool parseIndexedModuleName(const std::string& name, const char *baseName, int& index);
    static std::vector<int> parseIntegerList(const char *value, const char *parameterName);
    static int lookupVlanMapping(const std::vector<int>& mapping, int vlanId, const char *mappingName);
    static std::string trim(const std::string& value);

  public:
    virtual ~MatlabOfflineScheduleManager();
};

} // namespace offline
} // namespace satnet3d

#endif
