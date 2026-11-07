#include "WalkerConstellationManager.h"

#include <cmath>
#include <algorithm>
#include <set>
#include <sstream>

#include "inet/common/InitStages.h"
#include "inet/common/ModuleAccess.h"

using namespace omnetpp;
using namespace inet;

namespace satnet3d {

Define_Module(WalkerConstellationManager);

WalkerConstellationManager::~WalkerConstellationManager()
{
    if (polarLinkUpdateTimer != nullptr)
        cancelAndDelete(polarLinkUpdateTimer);
}

void WalkerConstellationManager::initialize(int stage)
{

    if (stage == INITSTAGE_LOCAL)
    {
        // Create satellite modules as early as possible so they take part in the
        // full remaining initialization sequence, including interface registration
        // and IPv4 configuration.
        ScenarioManager::initialize();
        std::string constellationType = par("constellationType").stdstringValue();
        std::transform(constellationType.begin(), constellationType.end(), constellationType.begin(), ::tolower);
        if (constellationType != "delta" && constellationType != "star")
            throw cRuntimeError("constellationType must be 'delta' or 'star', got '%s'", constellationType.c_str());
        isWalkerStar = constellationType == "star";
        polarRegionLatitude = par("polarRegionLatitude").doubleValueInUnit("deg");
        polarLinkUpdateInterval = par("polarLinkUpdateInterval");
        if (isWalkerStar) {
            if (polarRegionLatitude <= 0 || polarRegionLatitude >= 90)
                throw cRuntimeError("polarRegionLatitude must be in (0,90) degrees for Walker-Star");
            if (polarLinkUpdateInterval <= SIMTIME_ZERO)
                throw cRuntimeError("polarLinkUpdateInterval must be positive for Walker-Star");
            coordinateSystem = getModuleFromPar<inet::IGeographicCoordinateSystem>(par("coordinateSystemModule"), this);
        }
        const char *coordinatorPath = par("topologyCoordinatorModule").stringValue();
        if (coordinatorPath[0] != 0) {
            topologyCoordinator = dynamic_cast<TopologyCoordinator *>(getModuleByPath(coordinatorPath));
            if (topologyCoordinator == nullptr)
                throw cRuntimeError("topologyCoordinatorModule %s not found", coordinatorPath);
        }

        if (!par("autoGenerateConstellation").boolValue())
            return;

        cModule *network = resolveTargetNetworkModule();
        createSatelliteNodes(network, satellites);

        // Create inter-satellite links now so the network configurator
        // (which runs at INITSTAGE_NETWORK_CONFIGURATION) will see the full
        // topology and assign addresses correctly.
        createSatelliteLinks(satellites);
        if (isWalkerStar) {
            polarLinkUpdateTimer = new cMessage("walker-star-polar-link-update");
            scheduleAt(SIMTIME_ZERO, polarLinkUpdateTimer);
        }

    }
}

cModule *WalkerConstellationManager::resolveTargetNetworkModule() const
{
    const char *targetPath = par("targetNetworkModule").stringValue();
    cModule *target = getModuleByPath(targetPath);
    if (target == nullptr)
        throw cRuntimeError("targetNetworkModule '%s' not found from '%s'", targetPath, getFullPath().c_str());
    return target;
}

void WalkerConstellationManager::generateConstellation(cModule *network)
{
    if (network == nullptr)
        throw cRuntimeError("Cannot generate constellation on a null network");

    satellites.clear();
    createSatelliteNodes(network, satellites);
}

void WalkerConstellationManager::createSatelliteNodes(cModule *network, std::vector<cModule *>& createdSatellites)
{
    int totalSatellites = 0;

    int numPlanes = par("numPlanes").intValue();
    int satsPerPlane = par("satsPerPlane").intValue();
    if (numPlanes <= 0 || satsPerPlane <= 0)
        throw cRuntimeError("numPlanes and satsPerPlane must be positive");
    totalSatellites = numPlanes * satsPerPlane;

    int numEthInterfaces = par("numEthInterfaces").intValue();
    if (numEthInterfaces < 0)
        throw cRuntimeError("numEthInterfaces must be non-negative");
    if (!strcmp(par("connectionType").stringValue(), "oneStarFourLinks") && numEthInterfaces < 4)
        throw cRuntimeError("oneStarFourLinks requires numEthInterfaces >= 4 for intra-layer links");

    std::string overridesText = par("numEthInterfacesOverrides").stdstringValue();
    std::replace(overridesText.begin(), overridesText.end(), ',', ' ');
    std::istringstream overridesInput(overridesText);
    std::map<int, int> interfaceOverrides;
    int satelliteIndex;
    int interfaceCount;
    while (overridesInput >> satelliteIndex) {
        if (!(overridesInput >> interfaceCount))
            throw cRuntimeError("numEthInterfacesOverrides must contain index/count pairs");
        if (satelliteIndex < 0 || satelliteIndex >= totalSatellites)
            throw cRuntimeError("Satellite override index %d is outside 0..%d", satelliteIndex, totalSatellites - 1);
        if (interfaceCount < 4)
            throw cRuntimeError("Satellite override %d must provide at least four ISL interfaces", satelliteIndex);
        if (!interfaceOverrides.emplace(satelliteIndex, interfaceCount).second)
            throw cRuntimeError("Duplicate numEthInterfacesOverrides entry for satellite %d", satelliteIndex);
    }
    if (!overridesInput.eof())
        throw cRuntimeError("Invalid numEthInterfacesOverrides value '%s'", overridesText.c_str());

    createdSatellites.reserve(totalSatellites);
    for (int i = 0; i < totalSatellites; ++i) {
        auto overrideIt = interfaceOverrides.find(i);
        int satelliteInterfaceCount = overrideIt == interfaceOverrides.end() ? numEthInterfaces : overrideIt->second;
        cModule *satellite = createSingleSatelliteNode(network, satelliteInterfaceCount);

        configureSatelliteMobility(satellite, i, totalSatellites);

        cPreModuleInitNotification pre;
        pre.module = satellite;
        emit(POST_MODEL_CHANGE, &pre);

        // Do NOT call satellite->callInitialize() here. Creating the module
        // early (during stage 0) lets the network configurator see it and
        // assign addresses when it runs. Calling initialize here previously
        // triggered configurator access too early and caused crashes.

        cPostModuleInitNotification post;
        post.module = satellite;
        emit(POST_MODEL_CHANGE, &post);

        createdSatellites.push_back(satellite);
    }
}

cModule *WalkerConstellationManager::createSingleSatelliteNode(cModule *network, int numEthInterfaces)
{
    const char *moduleTypeName = par("satelliteModuleType").stringValue();
    const char *moduleName = par("satelliteModuleName").stringValue();
    cModuleType *moduleType = cModuleType::get(moduleTypeName);
    if (moduleType == nullptr)
        throw cRuntimeError("Satellite module type '%s' not found", moduleTypeName);

    int submoduleVectorSize = network->hasSubmoduleVector(moduleName) ? network->getSubmoduleVectorSize(moduleName) : 0;
    if (!network->hasSubmoduleVector(moduleName))
        network->addSubmoduleVector(moduleName, submoduleVectorSize + 1);
    else
        network->setSubmoduleVectorSize(moduleName, submoduleVectorSize + 1);

    cModule *module = moduleType->create(moduleName, network, submoduleVectorSize);
    if (module->hasPar("numEthInterfaces"))
        module->par("numEthInterfaces") = numEthInterfaces;
    else
        throw cRuntimeError("Satellite module type '%s' does not expose numEthInterfaces", moduleTypeName);
    module->finalizeParameters();
    module->buildInside();
    module->scheduleStart(simTime());
    return module;
}

void WalkerConstellationManager::configureSatelliteMobility(cModule *satellite, int satelliteIndex, int totalSatellites)
{
    configureWalkerMobility(satellite, satelliteIndex, totalSatellites);
}

double WalkerConstellationManager::getRaanSpreadDegrees() const
{
    return isWalkerStar ? 180.0 : 360.0;
}

void WalkerConstellationManager::configureWalkerMobility(cModule *satellite, int satelliteIndex, int totalSatellites)
{
    int numPlanes = par("numPlanes").intValue();
    int satsPerPlane = par("satsPerPlane").intValue();
    int phaseFactor = par("phaseFactor").intValue();
    double initialPhaseOffset = par("initialPhaseOffset").doubleValueInUnit("deg");

    if (totalSatellites != numPlanes * satsPerPlane)
        throw cRuntimeError("Total satellite count does not match Walker settings");

    int plane = satelliteIndex / satsPerPlane;
    int indexInPlane = satelliteIndex % satsPerPlane;

    double orbitalAltitude = par("orbitalAltitude").doubleValue();
    double orbitalInclination = par("orbitalInclination").doubleValue();
    double raan = -getRaanSpreadDegrees() / 2.0 + getRaanSpreadDegrees() * plane / numPlanes;
    double basePhase = 360.0 * indexInPlane / satsPerPlane;
    double phaseSpread = getRaanSpreadDegrees();
    double phaseOffset = phaseSpread * phaseFactor * plane / (numPlanes * satsPerPlane);
    double initialPhase = std::fmod(basePhase + phaseOffset + initialPhaseOffset, 360.0);
    if (initialPhase < 0)
        initialPhase += 360.0;

    cModule *mobility = satellite->getSubmodule("mobility");
    if (mobility == nullptr)
        return;

    std::string mobilityPath = std::string(satellite->getFullPath()) + ".mobility";

    auto withUnit = [](double value, const char *unit) {
        std::ostringstream os;
        os.setf(std::ios::fixed);
        os.precision(12);
        os << value << unit;
        return os.str();
    };

    if (mobility->hasPar("orbitalAltitude"))
        setParamViaScenarioManager(mobilityPath.c_str(), "orbitalAltitude", withUnit(orbitalAltitude, "m"));
    if (mobility->hasPar("orbitalInclination"))
        setParamViaScenarioManager(mobilityPath.c_str(), "orbitalInclination", withUnit(orbitalInclination, "deg"));
    if (mobility->hasPar("raan"))
        setParamViaScenarioManager(mobilityPath.c_str(), "raan", withUnit(raan, "deg"));
    if (mobility->hasPar("initialPhase"))
        setParamViaScenarioManager(mobilityPath.c_str(), "initialPhase", withUnit(initialPhase, "deg"));
}

std::vector<WalkerConstellationManager::LinkSpec> WalkerConstellationManager::buildLinkTable(int totalSatellites) const
{
    const char *connectionType = par("connectionType").stringValue();
    if (!strcmp(connectionType, "oneStarFourLinks"))
        return buildOneStarFourLinks(totalSatellites);

    throw cRuntimeError("Unsupported connectionType '%s'", connectionType);
}

std::vector<WalkerConstellationManager::LinkSpec> WalkerConstellationManager::buildOneStarFourLinks(int totalSatellites) const
{
    int numPlanes = par("numPlanes").intValue();
    int satsPerPlane = par("satsPerPlane").intValue();
    int phaseFactor = par("phaseFactor").intValue();
    if (totalSatellites != numPlanes * satsPerPlane)
        throw cRuntimeError("Total satellite count does not match Walker settings");

    std::vector<LinkSpec> links;
    links.reserve(totalSatellites * 4);

    auto wrapSlot = [satsPerPlane](int slot) {
        return (slot % satsPerPlane + satsPerPlane) % satsPerPlane;
    };

    for (int plane = 0; plane < numPlanes; ++plane) {
        for (int slot = 0; slot < satsPerPlane; ++slot) {
            int cur = plane * satsPerPlane + slot;
            int prevInPlane = plane * satsPerPlane + (slot - 1 + satsPerPlane) % satsPerPlane;
            int nextInPlane = plane * satsPerPlane + (slot + 1) % satsPerPlane;
            bool isStar = isWalkerStar;
            int prevPlaneSlot = plane == 0 ? wrapSlot(slot - phaseFactor) : slot;
            int nextPlaneSlot = plane == numPlanes - 1 ? wrapSlot(slot + phaseFactor) : slot;
            int prevPlane = ((plane - 1 + numPlanes) % numPlanes) * satsPerPlane + prevPlaneSlot;
            int nextPlane = ((plane + 1) % numPlanes) * satsPerPlane + nextPlaneSlot;

            // Match leolab's order: up, right, down, left.
            links.push_back({cur, 0, nextInPlane, 2, true});
            if (!isStar || plane < numPlanes - 1)
                links.push_back({cur, 1, nextPlane, 3, false});
            links.push_back({cur, 2, prevInPlane, 0, true});
            if (!isStar || plane > 0)
                links.push_back({cur, 3, prevPlane, 1, false});
        }
    }

    return links;
}

void WalkerConstellationManager::createSatelliteLinks(const std::vector<cModule *>& createdSatellites)
{
    int totalSatellites = (int)createdSatellites.size();
    auto rawLinks = buildLinkTable(totalSatellites);

    std::set<std::pair<int, int>> connectedPairs;

    for (const auto& link : rawLinks) {
        int a = link.srcIndex;
        int b = link.destIndex;

        if (a < 0 || b < 0 || a >= totalSatellites || b >= totalSatellites)
            throw cRuntimeError("Connection index out of range: (%d, %d) with %d satellites", a, b, totalSatellites);
        if (a == b)
            continue;

        auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
        if (connectedPairs.find(key) != connectedPairs.end())
            continue;

        int gateA = link.srcGateIndex;
        int gateB = link.destGateIndex;
        bool initiallyConnected = true;
        if (isWalkerStar && !link.intraPlane) {
            initiallyConnected = !isInPolarRegionAtStart(a) && !isInPolarRegionAtStart(b);
            polarLinks.push_back({a, gateA, b, gateB, initiallyConnected});
        }
        if (initiallyConnected)
            connectPair(createdSatellites[a], gateA, createdSatellites[b], gateB, link.intraPlane);
        connectedPairs.insert(key);
    }
}


void WalkerConstellationManager::connectPair(cModule *a, int gateIndexA, cModule *b, int gateIndexB, bool intraPlane)
{
    int requiredA = gateIndexA + 1;
    int requiredB = gateIndexB + 1;
    if (a->gateSize("ethg") < requiredA)
        a->setGateSize("ethg", requiredA);
    if (b->gateSize("ethg") < requiredB)
        b->setGateSize("ethg", requiredB);

    if (topologyCoordinator != nullptr) {
        TopologyCoordinator::LinkCommand command;
        command.type = TopologyCoordinator::CommandType::CONNECT;
        command.sourceNode = a->getFullName();
        command.sourceGate = "ethg[" + std::to_string(gateIndexA) + "]";
        command.destinationNode = b->getFullName();
        command.destinationGate = "ethg[" + std::to_string(gateIndexB) + "]";
        command.linkColor = intraPlane ? par("intraPlaneLinkColor").stringValue() : par("interPlaneLinkColor").stringValue();
        auto result = topologyCoordinator->applyCommands({command});
        if (result.rejected != 0)
            throw cRuntimeError("TopologyCoordinator rejected inter-satellite connect");
        return;
    }

    std::string datarateValue = par("datarate").str();
    std::string propagationSpeedValue = par("propagationSpeed").str();

    cXMLElement command("connect");
    command.setAttribute("src-module", a->getFullPath().c_str());
    command.setAttribute("src-gate", (std::string("ethg[") + std::to_string(gateIndexA) + "]").c_str());
    command.setAttribute("dest-module", b->getFullPath().c_str());
    command.setAttribute("dest-gate", (std::string("ethg[") + std::to_string(gateIndexB) + "]").c_str());
    command.setAttribute("channel-type", par("interSatelliteChannelType").stringValue());

    cXMLElement *datarateParam = new cXMLElement("param");
    datarateParam->setAttribute("name", "datarate");
    datarateParam->setAttribute("value", datarateValue.c_str());
    command.appendChild(datarateParam);

    cXMLElement *propagationSpeedParam = new cXMLElement("param");
    propagationSpeedParam->setAttribute("name", "propagationSpeed");
    propagationSpeedParam->setAttribute("value", propagationSpeedValue.c_str());
    command.appendChild(propagationSpeedParam);

    processConnectCommand(&command);

    const char *linkColor = intraPlane ? par("intraPlaneLinkColor").stringValue() : par("interPlaneLinkColor").stringValue();

    auto configureChannel = [&](cGate *gate) {
        if (!gate) return;
        cChannel *channel = gate->getChannel();
        if (channel != nullptr) {
            channel->getDisplayString().setTagArg("ls", 0, linkColor);
            channel->callInitialize();
        }
    };

    configureChannel(a->gateHalf("ethg", cGate::OUTPUT, gateIndexA));
    configureChannel(a->gateHalf("ethg", cGate::INPUT, gateIndexA));
    configureChannel(b->gateHalf("ethg", cGate::OUTPUT, gateIndexB));
    configureChannel(b->gateHalf("ethg", cGate::INPUT, gateIndexB));

    initializeChannelsRecursively(a);
    initializeChannelsRecursively(b);
}

void WalkerConstellationManager::handleMessage(cMessage *message)
{
    if (message != polarLinkUpdateTimer)
        throw cRuntimeError("Unexpected message '%s' in %s", message->getName(), getFullPath().c_str());

    updatePolarLinks();
    scheduleAt(simTime() + polarLinkUpdateInterval, polarLinkUpdateTimer);
}

void WalkerConstellationManager::finish()
{
    if (polarLinkUpdateTimer != nullptr) {
        cancelAndDelete(polarLinkUpdateTimer);
        polarLinkUpdateTimer = nullptr;
    }
}

bool WalkerConstellationManager::isInPolarRegion(int satelliteIndex) const
{
    if (satelliteIndex < 0 || satelliteIndex >= (int)satellites.size())
        throw cRuntimeError("Invalid satellite index %d while checking polar region", satelliteIndex);
    cModule *mobilityModule = satellites[satelliteIndex]->getSubmodule("mobility");
    auto mobility = dynamic_cast<inet::IMobility *>(mobilityModule);
    if (mobility == nullptr)
        throw cRuntimeError("Satellite %s has no IMobility submodule", satellites[satelliteIndex]->getFullPath().c_str());
    auto geographicPosition = coordinateSystem->computeGeographicCoordinate(mobility->getCurrentPosition());
    double latitude = geographicPosition.latitude.get<inet::units::values::deg>();
    return std::abs(latitude) >= polarRegionLatitude;
}

bool WalkerConstellationManager::isInPolarRegionAtStart(int satelliteIndex) const
{
    int numPlanes = par("numPlanes").intValue();
    int satsPerPlane = par("satsPerPlane").intValue();
    int plane = satelliteIndex / satsPerPlane;
    int slot = satelliteIndex % satsPerPlane;
    double inclination = par("orbitalInclination").doubleValueInUnit("deg");
    double initialOffset = par("initialPhaseOffset").doubleValueInUnit("deg");
    double phaseSpread = getRaanSpreadDegrees();
    double phaseOffset = phaseSpread * par("phaseFactor").intValue() * plane / (numPlanes * satsPerPlane);
    double phaseDeg = std::fmod(360.0 * slot / satsPerPlane + phaseOffset + initialOffset, 360.0);
    double radians = std::acos(-1.0) / 180.0;
    double latitudeSine = std::sin(inclination * radians) * std::sin(phaseDeg * radians);
    double latitude = std::asin(std::max(-1.0, std::min(1.0, latitudeSine))) / radians;
    return std::abs(latitude) >= polarRegionLatitude;
}

void WalkerConstellationManager::setPolarLinkConnected(PolarLink& link, bool connected)
{
    if (link.connected == connected)
        return;
    cModule *source = satellites[link.srcIndex];
    cModule *destination = satellites[link.destIndex];
    if (connected) {
        connectPair(source, link.srcGateIndex, destination, link.destGateIndex, false);
    }
    else if (topologyCoordinator != nullptr) {
        TopologyCoordinator::LinkCommand command;
        command.type = TopologyCoordinator::CommandType::DISCONNECT;
        command.sourceNode = source->getFullName();
        command.sourceGate = "ethg[" + std::to_string(link.srcGateIndex) + "]";
        command.destinationNode = destination->getFullName();
        command.destinationGate = "ethg[" + std::to_string(link.destGateIndex) + "]";
        auto result = topologyCoordinator->applyCommands({command});
        if (result.rejected != 0)
            throw cRuntimeError("TopologyCoordinator rejected Walker-Star polar ISL disconnect");
    }
    else {
        cGate *sourceOut = source->gateHalf("ethg", cGate::OUTPUT, link.srcGateIndex);
        cGate *destinationOut = destination->gateHalf("ethg", cGate::OUTPUT, link.destGateIndex);
        if (sourceOut != nullptr && sourceOut->isConnected())
            sourceOut->disconnect();
        if (destinationOut != nullptr && destinationOut->isConnected())
            destinationOut->disconnect();
    }
    link.connected = connected;
}

void WalkerConstellationManager::updatePolarLinks()
{
    for (auto& link : polarLinks) {
        bool shouldBeConnected = !isInPolarRegion(link.srcIndex) && !isInPolarRegion(link.destIndex);
        setPolarLinkConnected(link, shouldBeConnected);
    }
}

void WalkerConstellationManager::setParamViaScenarioManager(const char *modulePath, const char *parName, const std::string& value)
{
    cXMLElement command("set-param");
    command.setAttribute("module", modulePath);
    command.setAttribute("par", parName);
    command.setAttribute("value", value.c_str());
    processSetParamCommand(&command);
}

int WalkerConstellationManager::findOrCreateFreeGateIndex(cModule *module) const
{
    const char *gateName = "ethg";
    if (!module->hasGate(gateName))
        throw cRuntimeError("Module '%s' does not have gate '%s'", module->getFullPath().c_str(), gateName);

    int currentSize = module->gateSize(gateName);
    for (int i = 0; i < currentSize; ++i) {
        cGate *outGate = module->gateHalf(gateName, cGate::OUTPUT, i);
        cGate *inGate = module->gateHalf(gateName, cGate::INPUT, i);
        bool outputFree = outGate == nullptr || outGate->getNextGate() == nullptr;
        bool inputFree = inGate == nullptr || inGate->getPreviousGate() == nullptr;
        if (outputFree && inputFree)
            return i;
    }

    module->setGateSize(gateName, currentSize + 1);
    return currentSize;
}

void WalkerConstellationManager::initializeChannelsRecursively(cModule *module) const
{
    if (module == nullptr)
        return;

    for (cModule::SubmoduleIterator it(module); !it.end(); ++it) {
        initializeChannelsRecursively(*it);
    }

    for (cModule::GateIterator git(module); !git.end(); ++git) {
        cGate *gate = *git;
        if (gate == nullptr)
            continue;

        cChannel *channel = gate->getChannel();
        if (channel != nullptr)
            channel->callInitialize();
    }
}

} // namespace satnet3d
