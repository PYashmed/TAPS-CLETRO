#include "NetworkStateManager.h"
///////TJH
#include "../mobility/CircularOrbitMobility.h"
#include "../wireless/DynamicChannel.h"
///////

#include <sstream>
#include <cmath>
#include <regex>

#include "inet/common/InitStages.h"
#include "omnetpp/cmodelchange.h"
#include "inet/common/geometry/common/GeographicCoordinateSystem.h"
#include "inet/networklayer/contract/IInterfaceTable.h"
#include "inet/networklayer/common/NetworkInterface.h"
#include "inet/linklayer/common/MacAddress.h"

namespace satnet3d {

using namespace omnetpp;
using namespace inet;

Define_Module(NetworkStateManager);

namespace {

int extractDestPortFromPacketFilter(const cValue& packetFilter)
{
    std::string filter = packetFilter.getType() == cValue::POINTER && packetFilter.objectValue() != nullptr
        ? packetFilter.objectValue()->str()
        : packetFilter.str();
    std::regex destPortRegex(R"(\bdestPort\b\D+(\d+)\b)");
    std::smatch match;
    return std::regex_search(filter, match, destPortRegex) ? std::stoi(match[1].str()) : -1;
}

std::string findStreamNameForDestPort(cModule *host, int destPort)
{
    cModule *identifier = host->findModuleByPath(".bridging.streamIdentifier.identifier");
    if (identifier == nullptr || !identifier->hasPar("mapping"))
        return "";

    auto *mapping = dynamic_cast<cValueArray *>(identifier->par("mapping").objectValue());
    if (mapping == nullptr)
        return "";

    for (int i = 0; i < mapping->size(); ++i) {
        auto *element = dynamic_cast<cValueMap *>(mapping->get(i).objectValue());
        if (element == nullptr || !element->containsKey("stream") || !element->containsKey("packetFilter"))
            continue;
        if (extractDestPortFromPacketFilter(element->get("packetFilter")) == destPort)
            return element->get("stream").stringValue();
    }
    return "";
}

int findPcpForStream(cModule *host, const std::string& streamName)
{
    cModule *encoder = host->findModuleByPath(".bridging.streamCoder.encoder");
    if (encoder == nullptr || !encoder->hasPar("mapping"))
        return -1;

    auto *mapping = dynamic_cast<cValueArray *>(encoder->par("mapping").objectValue());
    if (mapping == nullptr)
        return -1;

    for (int i = 0; i < mapping->size(); ++i) {
        auto *element = dynamic_cast<cValueMap *>(mapping->get(i).objectValue());
        if (element == nullptr || !element->containsKey("stream"))
            continue;
        if (streamName == element->get("stream").stringValue())
            return element->containsKey("pcp") ? element->get("pcp").intValue() : -1;
    }
    return -1;
}

int resolveFlowPriority(cModule *host, cModule *app, cModule *io)
{
    int destPort = -1;
    if (app->hasPar("destPort"))
        destPort = app->par("destPort").intValue();
    else if (io != nullptr && io->hasPar("destPort"))
        destPort = io->par("destPort").intValue();

    if (destPort >= 0) {
        const auto streamName = findStreamNameForDestPort(host, destPort);
        if (!streamName.empty()) {
            int pcp = findPcpForStream(host, streamName);
            if (pcp >= 0)
                return pcp;
        }
    }

    if (app->hasPar("dscp"))
        return app->par("dscp").intValue();
    if (io != nullptr && io->hasPar("dscp"))
        return io->par("dscp").intValue();
    return -1;
}

double getFreshPropagationDelay(cDatarateChannel *channel)
{
    if (channel == nullptr)
        return 0;
    if (auto *dynamicChannel = dynamic_cast<DynamicChannel *>(channel))
        return dynamicChannel->refreshAndGetDelay().dbl();
    return channel->getDelay().dbl();
}

} // namespace

int NetworkStateManager::numInitStages() const
{
    return NUM_INIT_STAGES;
}

void NetworkStateManager::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL) {
        networkModule = getModuleByPath(par("networkModule").stringValue());
        if (networkModule == nullptr)
            throw cRuntimeError("networkModule not found");
        mobilitySubmoduleName = par("mobilitySubmoduleName").stringValue();
        linkGateName = par("linkGateName").stringValue();
        delayRefreshInterval = par("delayRefreshInterval");
        if (delayRefreshInterval <= SIMTIME_ZERO)
            throw cRuntimeError("delayRefreshInterval must be positive");
        delayRefreshTimer = new cMessage("network-state-delay-refresh");
        switchTypePatterns = splitPatterns("switchTypePatterns");
        switchNamePatterns = splitPatterns("switchNamePatterns");
        hostTypePatterns = splitPatterns("hostTypePatterns");
        hostNamePatterns = splitPatterns("hostNamePatterns");
        auto *coordinateModule = networkModule->getSubmodule("coordinateSystem");
        coordinateSystem = dynamic_cast<IGeographicCoordinateSystem *>(coordinateModule);
    }
    else if (stage == INITSTAGE_LAST) {
        rebuildSnapshot();
        // TopologyCoordinator will publish committed link changes explicitly.
        scheduleAt(simTime() + delayRefreshInterval, delayRefreshTimer);

    }
}

void NetworkStateManager::handleMessage(cMessage *message)
{
    if (message != delayRefreshTimer)
        throw cRuntimeError("Unexpected message %s", message->getName());
    refreshLinkDelays();
    scheduleAt(simTime() + delayRefreshInterval, delayRefreshTimer);
}

void NetworkStateManager::finish()
{

    cancelAndDelete(delayRefreshTimer);
    delayRefreshTimer = nullptr;
    for (auto *mobility : subscribedMobilities)
        mobility->unsubscribe(IMobility::mobilityStateChangedSignal, this);
    subscribedMobilities.clear();
}

std::vector<std::string> NetworkStateManager::splitPatterns(const char *parameterName) const
{
    std::vector<std::string> result;
    std::istringstream stream(par(parameterName).stringValue());
    std::string value;
    while (std::getline(stream, value, ','))
        result.push_back(value);
    return result;
}

bool NetworkStateManager::matchesAny(const std::string& value, const std::vector<std::string>& patterns) const
{
    for (const auto& pattern : patterns) {
        if (pattern.empty() || pattern == "*")
            return true;
        cPatternMatcher matcher(pattern.c_str(), false, true, true);
        if (matcher.matches(value.c_str()))
            return true;
    }
    return patterns.empty();
}

IMobility *NetworkStateManager::mobilityOf(cModule *node) const
{
    if (node == nullptr)
        return nullptr;
    return dynamic_cast<IMobility *>(node->getSubmodule(mobilitySubmoduleName.c_str()));
}

void NetworkStateManager::rebuildSnapshot()
{
    snapshot.nodes.clear();
    snapshot.links.clear();
    snapshot.flows.clear();
    nodeIndexById.clear();
    collectNodes();
    collectLinks();
    collectFlows();
    snapshot.topologyVersion++;
    snapshot.positionVersion++;
    snapshot.delayVersion++;
    snapshot.linkMetricVersion++;
    snapshot.simulationTime = simTime();
    EV_INFO << "Network state snapshot rebuilt: nodes=" << snapshot.nodes.size()
            << " links=" << snapshot.links.size()
            << " flows=" << snapshot.flows.size() << endl;
}

void NetworkStateManager::collectNodes()
{
    for (cModule::SubmoduleIterator iterator(networkModule); !iterator.end(); ++iterator) {
        cModule *node = *iterator;
        const bool isSwitch = matchesAny(node->getNedTypeName(), switchTypePatterns) && matchesAny(node->getFullName(), switchNamePatterns);
        const bool isHost = matchesAny(node->getNedTypeName(), hostTypePatterns) && matchesAny(node->getFullName(), hostNamePatterns);
        if (!isSwitch && !isHost)
            continue;
        IMobility *mobility = mobilityOf(node);
        if (mobility == nullptr)
            continue;

        NetworkNodeState state;
        state.moduleId = node->getId();
        state.name = node->getFullName();
        state.role = isSwitch ? "switch" : "host";
        cModule *interfaceTableModule = node->getSubmodule("interfaceTable");
        if (interfaceTableModule != nullptr) {
            auto *interfaceTable = check_and_cast<IInterfaceTable *>(interfaceTableModule);
            for (int index = 0; index < interfaceTable->getNumInterfaces(); ++index) {
                auto *networkInterface = interfaceTable->getInterface(index);
                if (networkInterface != nullptr && !networkInterface->isLoopback())
                    state.interfaces.push_back({networkInterface->getInterfaceName(), networkInterface->getMacAddress().str()});
            }
        }
        nodeIndexById[state.moduleId] = snapshot.nodes.size();
        snapshot.nodes.push_back(state);
        updateNodePosition(node, mobility);
        auto *mobilityComponent = node->getSubmodule(mobilitySubmoduleName.c_str());
        if (subscribedMobilities.insert(mobilityComponent).second)
            mobilityComponent->subscribe(IMobility::mobilityStateChangedSignal, this);
    }
}

void NetworkStateManager::updateNodePosition(cModule *node, IMobility *mobility)
{
    auto index = nodeIndexById.find(node->getId());
    if (index == nodeIndexById.end())
        return;
    auto& state = snapshot.nodes[index->second];
    if (coordinateSystem != nullptr) {
        GeoCoord position = coordinateSystem->computeGeographicCoordinate(mobility->getCurrentPosition());
        state.latitude = position.latitude.get<deg>();
        state.longitude = position.longitude.get<deg>();
        state.altitude = position.altitude.get<m>();
    }
    ///////TJH
    //只有模块是卫星时才读取相关属性
    auto *orbitMobility = dynamic_cast<CircularOrbitMobility *>(mobility);
    state.isSatellite = orbitMobility != nullptr;
    if (orbitMobility != nullptr) {
        state.orbitalInclination = inet::deg(inet::rad(orbitMobility->getOrbitalInclinationRad())).get<inet::units::values::deg>();
        state.raan = inet::deg(inet::rad(orbitMobility->getRaanRad())).get<inet::units::values::deg>();
        state.phase = orbitMobility->getCurrentPhaseDeg();
        state.isAscending = orbitMobility->getIsAscending();
    }
    ///////
}

void NetworkStateManager::collectLinks()
{
    std::set<std::pair<std::string, std::string>> visited;
    for (const auto& node : snapshot.nodes) {
        cModule *source = getSimulation()->getModule(node.moduleId);
        if (source == nullptr || !source->hasGate(linkGateName.c_str()))
            continue;
        for (int index = 0; index < source->gateSize(linkGateName.c_str()); ++index) {
            cGate *output = source->gateHalf(linkGateName.c_str(), cGate::OUTPUT, index);
            if (!output->isConnectedOutside())
                continue;
            cGate *end = output->getPathEndGate();
            cModule *destination = end != nullptr ? end->getOwnerModule() : nullptr;
            while (destination != nullptr && nodeIndexById.count(destination->getId()) == 0)
                destination = destination->getParentModule();
            if (destination == nullptr || nodeIndexById.count(destination->getId()) == 0)
                continue;
            int destinationIndex = -1;
            for (cGate *gate = end; gate != nullptr; gate = gate->getPreviousGate()) {
                if (gate->getOwnerModule() == destination && gate->isVector() &&
                    std::string(gate->getBaseName()) == linkGateName) {
                    destinationIndex = gate->getIndex();
                    break;
                }
            }
            if (destinationIndex < 0)
                continue;
            const std::string sourceEndpoint = std::string(source->getFullName()) + ":" + linkGateName + "[" + std::to_string(index) + "]";
            const std::string destinationEndpoint = std::string(destination->getFullName()) + ":" + linkGateName + "[" + std::to_string(destinationIndex) + "]";
            const auto key = std::minmax(sourceEndpoint, destinationEndpoint);
            if (!visited.insert(key).second)
                continue;
            NetworkLinkState link;
            link.sourceModuleId = source->getId();
            link.destinationModuleId = destination->getId();
            link.sourceNode = source->getFullName();
            link.destinationNode = destination->getFullName();
            link.sourceGate = linkGateName + "[" + std::to_string(index) + "]";
            link.destinationGate = linkGateName + "[" + std::to_string(destinationIndex) + "]";
            if (auto *channel = dynamic_cast<cDatarateChannel *>(output->getChannel())) {
                link.sourceToDestinationDelay = getFreshPropagationDelay(channel);
                link.sourceToDestinationDatarate = channel->getDatarate();
            }
            if (auto *channel = dynamic_cast<cDatarateChannel *>(destination->gateHalf(linkGateName.c_str(), cGate::OUTPUT, destinationIndex)->getChannel())) {
                link.destinationToSourceDelay = getFreshPropagationDelay(channel);
                link.destinationToSourceDatarate = channel->getDatarate();
            }
            snapshot.links.push_back(link);
        }
    }
}

void NetworkStateManager::refreshLinkDelays()
{
    bool delayChanged = false;
    bool metricChanged = false;
    for (auto& link : snapshot.links) {
        cModule *source = getSimulation()->getModule(link.sourceModuleId);
        cModule *destination = getSimulation()->getModule(link.destinationModuleId);
        if (source == nullptr || destination == nullptr)
            continue;
        const int sourceIndex = std::stoi(link.sourceGate.substr(link.sourceGate.find("[") + 1));
        const int destinationIndex = std::stoi(link.destinationGate.substr(link.destinationGate.find("[") + 1));
        cGate *sourceOutput = source->gateHalf(linkGateName.c_str(), cGate::OUTPUT, sourceIndex);
        cGate *destinationOutput = destination->gateHalf(linkGateName.c_str(), cGate::OUTPUT, destinationIndex);
        if (!sourceOutput->isConnectedOutside() || !destinationOutput->isConnectedOutside())
            continue;
        auto *forward = dynamic_cast<cDatarateChannel *>(sourceOutput->getChannel());
        auto *reverse = dynamic_cast<cDatarateChannel *>(destinationOutput->getChannel());
        const double forwardDelay = getFreshPropagationDelay(forward);
        const double reverseDelay = getFreshPropagationDelay(reverse);
        const double forwardRate = forward != nullptr ? forward->getDatarate() : -1;
        const double reverseRate = reverse != nullptr ? reverse->getDatarate() : -1;
        bool linkMetricChanged = false;
        if (link.sourceToDestinationDelay != forwardDelay || link.destinationToSourceDelay != reverseDelay) {
            link.sourceToDestinationDelay = forwardDelay;
            link.destinationToSourceDelay = reverseDelay;
            delayChanged = true;
            metricChanged = true;
            linkMetricChanged = true;
        }
        if (link.sourceToDestinationDatarate != forwardRate || link.destinationToSourceDatarate != reverseRate) {
            metricChanged = true;
            linkMetricChanged = true;
        }
        if (linkMetricChanged) {
            link.sourceToDestinationDatarate = forwardRate;
            link.destinationToSourceDatarate = reverseRate;
        }
    }
    if (delayChanged)
        snapshot.delayVersion++;
    if (metricChanged) {
        snapshot.linkMetricVersion++;
        snapshot.simulationTime = simTime();
    }
}

void NetworkStateManager::collectFlows()
{
    for (const auto& nodeState : snapshot.nodes) {
        if (nodeState.role != "host")
            continue;
        cModule *node = getSimulation()->getModule(nodeState.moduleId);
        if (node == nullptr || !node->hasSubmoduleVector("app"))
            continue;
        for (int index = 0; index < node->getSubmoduleVectorSize("app"); ++index) {
            cModule *app = node->getSubmodule("app", index);
            if (app == nullptr)
                continue;
            cModule *io = app->getSubmodule("io");
            cModule *source = app->getSubmodule("source");

            std::string destination;
            if (app->hasPar("destAddresses"))
                destination = app->par("destAddresses").stringValue();
            else if (io != nullptr && io->hasPar("destAddress"))
                destination = io->par("destAddress").stringValue();
            if (destination.empty())
                continue;

            TrafficFlowState flow;
            flow.flowId = std::string(node->getFullName()) + "_" + app->getFullName();
            flow.sourceNode = node->getFullName();
            flow.destinationNode = destination;
            flow.priority = resolveFlowPriority(node, app, io);
            if (app->hasPar("messageLength"))
                flow.packetSize = app->par("messageLength").intValue();
            else if (source != nullptr && source->hasPar("packetLength"))
                flow.packetSize = static_cast<int>(std::ceil(source->par("packetLength").doubleValueInUnit("B")));
            if (app->hasPar("sendInterval"))
                flow.period = app->par("sendInterval").doubleValueInUnit("s");
            else if (source != nullptr && source->hasPar("productionInterval"))
                flow.period = source->par("productionInterval").doubleValueInUnit("s");
            if (app->hasPar("startTime"))
                flow.startTime = app->par("startTime").doubleValueInUnit("s");
            else if (source != nullptr && source->hasPar("initialProductionOffset"))
                flow.startTime = source->par("initialProductionOffset").doubleValueInUnit("s");
            if (app->hasPar("stopTime")) flow.endTime = app->par("stopTime").doubleValueInUnit("s");
            if (app->hasPar("deadline")) flow.deadline = app->par("deadline").doubleValueInUnit("s");
            else if (source != nullptr && source->hasPar("deadline"))
                flow.deadline = source->par("deadline").doubleValueInUnit("s");
            if (app->hasPar("maxJitter")) flow.maxJitter = app->par("maxJitter").doubleValueInUnit("s");
            snapshot.flows.push_back(flow);
        }
    }
}

void NetworkStateManager::receiveSignal(cComponent *source, simsignal_t signal, cObject *object, cObject *details)
{
    if (signal == IMobility::mobilityStateChangedSignal) {
        for (const auto& node : snapshot.nodes) {
            cModule *module = getSimulation()->getModule(node.moduleId);
            if (module != nullptr && module->getSubmodule(mobilitySubmoduleName.c_str()) == source) {
                updateNodePosition(module, mobilityOf(module));
                snapshot.positionVersion++;
                snapshot.simulationTime = simTime();
                return;
            }
        }
    }
    else if (signal == POST_MODEL_CHANGE &&
             (dynamic_cast<cPostGateConnectNotification *>(object) != nullptr ||
              dynamic_cast<cPostGateDisconnectNotification *>(object) != nullptr ||
              dynamic_cast<cPostPathCreateNotification *>(object) != nullptr)) {
        rebuildSnapshot();
    }
}

} // namespace satnet3d
