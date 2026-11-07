#include "MatlabOfflineScheduleManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include "inet/linklayer/common/MacAddress.h"
#include "inet/linklayer/ethernet/common/MacForwardingTable.h"
#include "inet/networklayer/common/NetworkInterface.h"
#include "inet/networklayer/contract/IInterfaceTable.h"
#include "inet/queueing/contract/IPacketGate.h"

using namespace omnetpp;
using namespace inet;

namespace satnet3d {
namespace offline {

Define_Module(MatlabOfflineScheduleManager);

MatlabOfflineScheduleManager::~MatlabOfflineScheduleManager()
{
    cancelAndDelete(sliceTimer);
}

void MatlabOfflineScheduleManager::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL) {
        configurationFile = par("configurationFile").stringValue();
        schedulingPriority = par("schedulingPriority").intValue();
        applyGateSchedules = par("applyGateSchedules").boolValue();
        openPcp7AtScheduleEnd = par("openPcp7AtScheduleEnd").boolValue();
        drainGateIndex = par("drainGateIndex").intValue();
        satelliteIndexMap = parseIntegerList(par("satelliteIndexMap").stringValue(), "satelliteIndexMap");
        sourceAccessIndexByVlan = parseIntegerList(par("sourceAccessIndexByVlan").stringValue(), "sourceAccessIndexByVlan");
        destinationAccessIndexByVlan = parseIntegerList(par("destinationAccessIndexByVlan").stringValue(), "destinationAccessIndexByVlan");
        satelliteGroundInterfaceByVlan = parseIntegerList(par("satelliteGroundInterfaceByVlan").stringValue(), "satelliteGroundInterfaceByVlan");
        networkModule = getModuleByPath(par("networkModule").stringValue());
        if (networkModule == nullptr)
            throw cRuntimeError("networkModule not found");
        loadConfiguration();
        sliceTimer = new cMessage("matlab-offline-slice");
        sliceTimer->setSchedulingPriority(schedulingPriority);
    }
    else if (stage == INITSTAGE_LAST)
        scheduleAt(SIMTIME_ZERO, sliceTimer);
}

void MatlabOfflineScheduleManager::handleMessage(cMessage *message)
{
    if (message != sliceTimer)
        throw cRuntimeError("Unexpected message '%s'", message->getName());

    if (nextSlice < sliceCount) {
        if (nextSlice == 0)
            applyStaticRoutes();
        if (applyGateSchedules) {
            applyGateSlice(nextSlice);
            EV_INFO << "Applied MATLAB offline GCL slice " << nextSlice
                    << " at " << simTime() << endl;
        }
        else
            EV_INFO << "Skipped empty MATLAB GCLs; all transmission gates remain open" << endl;
        nextSlice++;
    }
    else if (applyGateSchedules && openPcp7AtScheduleEnd && nextSlice == sliceCount) {
        applyDrainGateState();
        nextSlice++;
        EV_INFO << "Opened PCP7 gates for the drain interval at " << simTime() << endl;
    }
    else
        throw cRuntimeError("Unexpected offline schedule timer at %s", simTime().str().c_str());

    if (applyGateSchedules && nextSlice < sliceCount) {
        sliceTimer->setSchedulingPriority(schedulingPriority);
        scheduleAt(sliceInterval * nextSlice, sliceTimer);
    }
    else if (applyGateSchedules && openPcp7AtScheduleEnd && nextSlice == sliceCount) {
        // The schedule covers [0, sliceInterval * sliceCount). At its end,
        // switch PCP7 to a permanently open drain state before ordinary
        // priority-zero timeout events at the same simulation time.
        sliceTimer->setSchedulingPriority(schedulingPriority);
        scheduleAt(sliceInterval * sliceCount, sliceTimer);
    }
}

void MatlabOfflineScheduleManager::finish()
{
    if (sliceTimer != nullptr && sliceTimer->isScheduled())
        cancelEvent(sliceTimer);
}

std::string MatlabOfflineScheduleManager::trim(const std::string& value)
{
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<int> MatlabOfflineScheduleManager::parseIntegerList(const char *value, const char *parameterName)
{
    std::string normalized = value == nullptr ? "" : value;
    for (char& character : normalized) {
        if (character == ',' || character == ';')
            character = ' ';
    }

    std::vector<int> result;
    std::istringstream input(normalized);
    int number;
    while (input >> number)
        result.push_back(number);
    if (!input.eof())
        throw cRuntimeError("Invalid integer list in parameter %s: '%s'", parameterName, value);
    return result;
}

int MatlabOfflineScheduleManager::lookupVlanMapping(const std::vector<int>& mapping, int vlanId,
                                                     const char *mappingName)
{
    if (mapping.empty())
        return -1;
    if (vlanId < 1 || vlanId > static_cast<int>(mapping.size()))
        throw cRuntimeError("VLAN %d has no entry in %s", vlanId, mappingName);
    int mappedValue = mapping[vlanId - 1];
    if (mappedValue < 0)
        throw cRuntimeError("VLAN %d is disabled in %s", vlanId, mappingName);
    return mappedValue;
}

bool MatlabOfflineScheduleManager::parseIndexedModuleName(const std::string& name, const char *baseName, int& index)
{
    std::string prefix = std::string(baseName) + "[";
    if (name.rfind(prefix, 0) != 0 || name.back() != ']')
        return false;
    std::string indexText = name.substr(prefix.size(), name.size() - prefix.size() - 1);
    if (indexText.empty() || !std::all_of(indexText.begin(), indexText.end(), [](unsigned char c) { return std::isdigit(c); }))
        return false;
    try {
        index = std::stoi(indexText);
    }
    catch (const std::exception&) {
        return false;
    }
    return true;
}

std::string MatlabOfflineScheduleManager::mapSatelliteName(const std::string& name, bool allowIgnored) const
{
    int fileIndex;
    if (!parseIndexedModuleName(name, "satellite", fileIndex) || satelliteIndexMap.empty())
        return name;
    if (fileIndex < 0 || fileIndex >= static_cast<int>(satelliteIndexMap.size()))
        throw cRuntimeError("Satellite index %d has no satelliteIndexMap entry", fileIndex);
    int networkIndex = satelliteIndexMap[fileIndex];
    if (networkIndex < 0) {
        if (allowIgnored)
            return "";
        throw cRuntimeError("Route refers to compatibility-only %s", name.c_str());
    }
    return "satellite[" + std::to_string(networkIndex) + "]";
}

std::string MatlabOfflineScheduleManager::mapRouteNodeName(const RouteRecord& route) const
{
    int ignoredIndex;
    if (parseIndexedModuleName(route.nodeName, "satellite", ignoredIndex))
        return mapSatelliteName(route.nodeName, false);

    if (parseIndexedModuleName(route.nodeName, "groundAccess", ignoredIndex)) {
        if (route.outInterface == "eth3" && !sourceAccessIndexByVlan.empty())
            return "groundAccess[" + std::to_string(lookupVlanMapping(sourceAccessIndexByVlan,
                    route.vlanId, "sourceAccessIndexByVlan")) + "]";
        if (route.outInterface == "eth2" && !destinationAccessIndexByVlan.empty())
            return "groundAccess[" + std::to_string(lookupVlanMapping(destinationAccessIndexByVlan,
                    route.vlanId, "destinationAccessIndexByVlan")) + "]";
    }
    return route.nodeName;
}

std::string MatlabOfflineScheduleManager::mapRouteOutputInterface(const RouteRecord& route) const
{
    int ignoredIndex;
    if (route.outInterface == "eth4" && parseIndexedModuleName(route.nodeName, "satellite", ignoredIndex)
            && !satelliteGroundInterfaceByVlan.empty()) {
        int interfaceIndex = lookupVlanMapping(satelliteGroundInterfaceByVlan, route.vlanId,
                                               "satelliteGroundInterfaceByVlan");
        return "eth" + std::to_string(interfaceIndex);
    }
    return route.outInterface;
}

void MatlabOfflineScheduleManager::loadConfiguration()
{
    std::ifstream input(configurationFile);
    if (!input.is_open())
        throw cRuntimeError("Cannot open MATLAB offline configuration '%s'", configurationFile.c_str());

    std::string line;
    bool sawHeader = false;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream parser(line);
        std::string type;
        parser >> type;
        if (type == "WUYUAN_OFFLINE_V1" || type == "XW_OFFLINE_V1") {
            sawHeader = true;
        }
        else if (type == "CYCLE_US") {
            parser >> cycleUs;
        }
        else if (type == "SLICE_INTERVAL_S") {
            double seconds;
            parser >> seconds;
            sliceInterval = seconds;
        }
        else if (type == "SLICE_COUNT") {
            parser >> sliceCount;
            if (sliceCount <= 0 || sliceCount > 100)
                throw cRuntimeError("Invalid SLICE_COUNT in '%s'", configurationFile.c_str());
            sliceWindows.resize(sliceCount);
        }
        else if (type == "QUEUE_STATES") {
            parser >> firstQueueState >> secondQueueState;
            auto validState = [](const std::string& state) {
                return !state.empty() && std::all_of(state.begin(), state.end(),
                        [](char bit) { return bit == '0' || bit == '1'; });
            };
            if (!parser || !validState(firstQueueState) || !validState(secondQueueState)
                    || firstQueueState.size() != secondQueueState.size())
                throw cRuntimeError("Invalid QUEUE_STATES line: %s", line.c_str());
        }
        else if (type == "OFFSET") {
            OffsetRecord record;
            int enabled;
            parser >> record.trafficId >> record.offsetUs >> enabled;
            record.enabled = enabled != 0;
            if (!parser || record.trafficId <= 0 ||
                !std::isfinite(record.offsetUs) || record.offsetUs < 0)
                throw cRuntimeError("Invalid OFFSET line: %s", line.c_str());
            offsets[record.trafficId] = record;
        }
        else if (type == "ROUTE") {
            RouteRecord record;
            parser >> record.nodeName >> record.vlanId >> record.destinationName >> record.outInterface;
            if (!parser || record.vlanId < 1 || record.vlanId > 4094)
                throw cRuntimeError("Invalid ROUTE line: %s", line.c_str());
            routes.push_back(record);
        }
        else if (type == "WINDOWS") {
            int slice;
            int count;
            WindowRecord record;
            parser >> slice >> record.nodeName >> record.outInterface >> count;
            if (!parser || slice < 0 || slice >= sliceCount || count < 0)
                throw cRuntimeError("Invalid WINDOWS line: %s", line.c_str());
            for (int i = 0; i < count; i++) {
                double openUs;
                double closeUs;
                parser >> openUs >> closeUs;
                if (!parser || !std::isfinite(openUs) || !std::isfinite(closeUs))
                    throw cRuntimeError("Invalid window boundary: %s", line.c_str());
                record.windowsUs.emplace_back(openUs, closeUs);
            }
            sliceWindows[slice].push_back(record);
        }
        else if (type == "END") {
            break;
        }
        else {
            throw cRuntimeError("Unknown offline configuration record '%s'", type.c_str());
        }
    }

    if (!sawHeader || cycleUs <= 0 || sliceInterval <= SIMTIME_ZERO || sliceCount <= 0)
        throw cRuntimeError("Incomplete MATLAB offline configuration '%s'", configurationFile.c_str());
    EV_INFO << "Loaded MATLAB offline file with " << routes.size() << " routes, "
            << offsets.size() << " offsets and " << sliceCount << " GCL slices" << endl;
}

cModule *MatlabOfflineScheduleManager::findTopLevelModule(const std::string& name) const
{
    auto bracket = name.find('[');
    if (bracket == std::string::npos)
        return networkModule->getSubmodule(name.c_str());
    auto close = name.find(']', bracket + 1);
    if (close == std::string::npos || close + 1 != name.size())
        return nullptr;
    std::string base = name.substr(0, bracket);
    int index;
    try {
        index = std::stoi(name.substr(bracket + 1, close - bracket - 1));
    }
    catch (const std::exception&) {
        return nullptr;
    }
    return networkModule->getSubmodule(base.c_str(), index);
}

void MatlabOfflineScheduleManager::applyStaticRoutes()
{
    std::set<std::string> clearedNodes;
    std::map<std::string, std::string> installedRules;
    int applied = 0;

    for (const auto& route : routes) {
        std::string mappedNodeName = mapRouteNodeName(route);
        std::string mappedOutputInterface = mapRouteOutputInterface(route);
        cModule *node = findTopLevelModule(mappedNodeName);
        cModule *destination = findTopLevelModule(route.destinationName);
        if (node == nullptr || destination == nullptr)
            throw cRuntimeError("Route endpoint not found: %s -> %s",
                                mappedNodeName.c_str(), route.destinationName.c_str());
        cModule *macTableModule = node->getSubmodule("macTable");
        auto *interfaceTable = dynamic_cast<IInterfaceTable *>(node->getSubmodule("interfaceTable"));
        auto *destinationInterfaces = dynamic_cast<IInterfaceTable *>(destination->getSubmodule("interfaceTable"));
        if (macTableModule == nullptr || interfaceTable == nullptr || destinationInterfaces == nullptr)
            throw cRuntimeError("Missing MAC/interface table for route %s", route.nodeName.c_str());

        auto *macTable = check_and_cast<MacForwardingTable *>(macTableModule);
        if (clearedNodes.insert(mappedNodeName).second) {
            auto *emptyTable = new cValueArray();
            macTableModule->par("forwardingTable").copyIfShared();
            macTableModule->par("forwardingTable").setObjectValue(emptyTable);
        }

        NetworkInterface *destinationInterface = destinationInterfaces->findInterfaceByName("eth0");
        if (destinationInterface == nullptr) {
            for (int i = 0; i < destinationInterfaces->getNumInterfaces(); i++) {
                auto *candidate = destinationInterfaces->getInterface(i);
                if (candidate != nullptr && !candidate->isLoopback()) {
                    destinationInterface = candidate;
                    break;
                }
            }
        }
        auto *outputInterface = interfaceTable->findInterfaceByName(mappedOutputInterface.c_str());
        if (destinationInterface == nullptr || outputInterface == nullptr)
            throw cRuntimeError("Cannot resolve destination/output interface for route %s VID %d",
                                route.nodeName.c_str(), route.vlanId);
        MacAddress destinationMac = destinationInterface->getMacAddress();
        if (destinationMac.isUnspecified())
            throw cRuntimeError("Destination %s has no MAC address", route.destinationName.c_str());

        std::string key = mappedNodeName + "|" + std::to_string(route.vlanId) + "|" + destinationMac.str();
        auto previous = installedRules.find(key);
        if (previous != installedRules.end() && previous->second != mappedOutputInterface)
            throw cRuntimeError("Conflicting route %s: %s versus %s", key.c_str(),
                                previous->second.c_str(), mappedOutputInterface.c_str());
        if (previous == installedRules.end()) {
            installedRules[key] = mappedOutputInterface;
            macTable->setUnicastAddressForwardingInterface(outputInterface->getInterfaceId(),
                                                           destinationMac, route.vlanId);
            applied++;
        }
    }
    EV_INFO << "Applied " << applied << " static VLAN/MAC forwarding rules to "
            << clearedNodes.size() << " switches" << endl;
}

MatlabOfflineScheduleManager::GatePattern
MatlabOfflineScheduleManager::buildGatePattern(const WindowRecord& record) const
{
    constexpr double epsilon = 1e-7;
    std::vector<std::pair<double, double>> windows;
    for (const auto& window : record.windowsUs) {
        double open = std::max(0.0, std::min(cycleUs, window.first));
        double close = std::max(0.0, std::min(cycleUs, window.second));
        if (close > open + epsilon)
            windows.emplace_back(open, close);
    }
    std::sort(windows.begin(), windows.end());
    std::vector<std::pair<double, double>> merged;
    for (const auto& window : windows) {
        if (merged.empty() || window.first > merged.back().second + epsilon)
            merged.push_back(window);
        else
            merged.back().second = std::max(merged.back().second, window.second);
    }

    GatePattern pattern;
    if (merged.empty()) {
        pattern.staticState = true;
        pattern.firstStateActive = false;
        return pattern;
    }
    if (merged.size() == 1 && merged.front().first <= epsilon && merged.front().second >= cycleUs - epsilon) {
        pattern.staticState = true;
        pattern.firstStateActive = true;
        return pattern;
    }

    double canonicalStartUs;
    std::vector<double> durationsUs;
    if (merged.size() >= 2 && merged.front().first <= epsilon && merged.back().second >= cycleUs - epsilon) {
        canonicalStartUs = merged.back().first;
        durationsUs.push_back((cycleUs - merged.back().first) + merged.front().second);
        double previousClose = merged.front().second;
        for (size_t i = 1; i + 1 < merged.size(); i++) {
            durationsUs.push_back(merged[i].first - previousClose);
            durationsUs.push_back(merged[i].second - merged[i].first);
            previousClose = merged[i].second;
        }
        durationsUs.push_back(merged.back().first - previousClose);
    }
    else {
        canonicalStartUs = merged.front().first;
        for (size_t i = 0; i < merged.size(); i++) {
            durationsUs.push_back(merged[i].second - merged[i].first);
            double nextOpen = i + 1 < merged.size() ? merged[i + 1].first : cycleUs + merged.front().first;
            durationsUs.push_back(nextOpen - merged[i].second);
        }
    }

    if (durationsUs.empty() || durationsUs.size() % 2 != 0)
        throw cRuntimeError("Cannot form an even periodic GCL for %s %s",
                            record.nodeName.c_str(), record.outInterface.c_str());
    double totalUs = 0;
    for (double durationUs : durationsUs) {
        if (durationUs <= epsilon)
            throw cRuntimeError("Non-positive GCL duration for %s %s",
                                record.nodeName.c_str(), record.outInterface.c_str());
        pattern.durationsSeconds.push_back(durationUs * 1e-6);
        totalUs += durationUs;
    }
    if (std::abs(totalUs - cycleUs) > std::max(1e-4, cycleUs * 1e-9))
        throw cRuntimeError("GCL period mismatch for %s %s: %.12g us versus %.12g us",
                            record.nodeName.c_str(), record.outInterface.c_str(), totalUs, cycleUs);
    pattern.firstStateActive = true;
    pattern.offsetSeconds = std::fmod(cycleUs - canonicalStartUs, cycleUs) * 1e-6;
    return pattern;
}

omnetpp::cModule *MatlabOfflineScheduleManager::findTransmissionGate(const WindowRecord& record,
                                                                     int gateIndex) const
{
    std::string mappedNodeName = mapSatelliteName(record.nodeName, true);
    if (mappedNodeName.empty())
        return nullptr;
    cModule *node = findTopLevelModule(mappedNodeName);
    if (node == nullptr)
        throw cRuntimeError("GCL node %s (mapped from %s) not found",
                            mappedNodeName.c_str(), record.nodeName.c_str());
    if (record.outInterface.rfind("eth", 0) != 0)
        throw cRuntimeError("Invalid GCL interface %s", record.outInterface.c_str());
    int interfaceIndex;
    try {
        interfaceIndex = std::stoi(record.outInterface.substr(3));
    }
    catch (const std::exception&) {
        throw cRuntimeError("Invalid GCL interface %s", record.outInterface.c_str());
    }
    cModule *eth = node->getSubmodule("eth", interfaceIndex);
    cModule *macLayer = eth != nullptr ? eth->getSubmodule("macLayer") : nullptr;
    cModule *queue = macLayer != nullptr ? macLayer->getSubmodule("queue") : nullptr;
    cModule *gate = queue != nullptr ? queue->getSubmodule("transmissionGate", gateIndex) : nullptr;
    if (gate == nullptr)
        throw cRuntimeError("Transmission gate %d not found at %s.%s",
                            gateIndex, record.nodeName.c_str(), record.outInterface.c_str());
    return gate;
}

void MatlabOfflineScheduleManager::applyGatePattern(const WindowRecord& record, int gateIndex,
                                                     bool firstStateOpen, bool secondStateOpen,
                                                     const GatePattern& pattern)
{
    cModule *gate = findTransmissionGate(record, gateIndex);
    if (gate == nullptr)
        return;

    auto *durations = new cValueArray();
    bool statesDiffer = firstStateOpen != secondStateOpen;
    if (!pattern.staticState && statesDiffer) {
        for (double duration : pattern.durationsSeconds)
            durations->add(cValue(duration, "s"));
    }
    cPar& durationsParameter = gate->par("durations");
    durationsParameter.copyIfShared();
    durationsParameter.setObjectValue(durations);
    bool initiallyOpen = statesDiffer
            ? (pattern.firstStateActive ? firstStateOpen : secondStateOpen)
            : firstStateOpen;
    gate->par("initiallyOpen").setBoolValue(initiallyOpen);
    gate->par("offset").setDoubleValue(pattern.staticState || !statesDiffer ? 0 : pattern.offsetSeconds);
}

void MatlabOfflineScheduleManager::applyGateSlice(int slice)
{
    if (slice < 0 || slice >= sliceCount)
        throw cRuntimeError("Invalid GCL slice %d", slice);
    for (const auto& record : sliceWindows[slice]) {
        GatePattern pattern = buildGatePattern(record);
        int gateCount = static_cast<int>(firstQueueState.size());
        for (int gateIndex = 0; gateIndex < gateCount; gateIndex++) {
            // QUEUE_STATES is written from the highest queue down to queue 0.
            int stateIndex = gateCount - 1 - gateIndex;
            applyGatePattern(record, gateIndex, firstQueueState[stateIndex] == '1',
                             secondQueueState[stateIndex] == '1', pattern);
        }
    }
}

void MatlabOfflineScheduleManager::applyDrainGateState()
{
    int gateCount = static_cast<int>(firstQueueState.size());
    if (drainGateIndex < 0 || drainGateIndex >= gateCount)
        throw cRuntimeError("drainGateIndex %d is outside the configured gate range 0..%d",
                            drainGateIndex, gateCount - 1);

    // A port may occur in every slice. Apply the static-open state once to
    // each managed satellite/interface pair, including ports that are absent
    // from the final slice in compatibility files.
    std::set<std::pair<std::string, std::string>> appliedPorts;
    for (const auto& records : sliceWindows) {
        for (const auto& record : records) {
            auto key = std::make_pair(record.nodeName, record.outInterface);
            if (!appliedPorts.insert(key).second)
                continue;

            cModule *gate = findTransmissionGate(record, drainGateIndex);
            if (gate == nullptr)
                continue;
            auto *packetGate = dynamic_cast<inet::queueing::IPacketGate *>(gate);
            if (packetGate == nullptr)
                throw cRuntimeError("Transmission gate %d at %s.%s does not implement IPacketGate",
                                    drainGateIndex, record.nodeName.c_str(), record.outInterface.c_str());

            // Opening through IPacketGate notifies the downstream scheduler.
            // Directly changing initiallyOpen would update only the gate's
            // internal state and could leave an already queued packet asleep.
            if (!packetGate->isOpen())
                packetGate->open();

            // Make the open state permanent after the schedule ends. Set
            // initiallyOpen first so subsequent parameter-change callbacks
            // preserve the state established by open().
            gate->par("initiallyOpen").setBoolValue(true);
            auto *durations = new cValueArray();
            cPar& durationsParameter = gate->par("durations");
            durationsParameter.copyIfShared();
            durationsParameter.setObjectValue(durations);
            gate->par("offset").setDoubleValue(0);
        }
    }
}

} // namespace offline
} // namespace satnet3d
