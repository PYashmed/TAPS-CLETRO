#include "TopologyManager.h"
#include "TopologyCoordinator.h"
#include "inet/common/InitStages.h"

#include <algorithm>
#include <limits>

namespace satnet3d {

Define_Module(TopologyManager);

void TopologyManager::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL) {
        enabled = par("enabled").boolValue();
        autoMaintain = par("autoMaintain").boolValue();
        initialUpdateOffset = par("initialUpdateOffset");
        updateInterval = par("updateInterval");
        updateSchedulingPriority = par("updateSchedulingPriority").intValue();
        const char *coordinatorPath = par("topologyCoordinatorModule").stringValue();
        if (coordinatorPath[0] != '\0')
            topologyCoordinator = dynamic_cast<TopologyCoordinator *>(getModuleByPath(coordinatorPath));
        if (coordinatorPath[0] != '\0' && topologyCoordinator == nullptr)
            throw cRuntimeError("topologyCoordinatorModule %s not found", coordinatorPath);

        if (!enabled || !autoMaintain)
            return;
        if (updateInterval <= SIMTIME_ZERO)
            throw cRuntimeError("updateInterval must be positive when autoMaintain is enabled");

        updateTimer = new cMessage("topology-update");
        updateTimer->setSchedulingPriority(updateSchedulingPriority);
        scheduleNextUpdate(simTime() + initialUpdateOffset);
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        if (enabled && autoMaintain)
            maintainInterLayerLinks();
    }
}

void TopologyManager::handleMessage(cMessage *msg)
{
    if (msg == updateTimer) {
        maintainInterLayerLinks();
        scheduleNextUpdate(simTime() + updateInterval);
        return;
    }

    throw cRuntimeError("Unexpected message '%s' in %s", msg->getName(), getFullPath().c_str());
}

void TopologyManager::finish()
{
    if (updateTimer != nullptr) {
        cancelAndDelete(updateTimer);
        updateTimer = nullptr;
    }
}

void TopologyManager::scheduleNextUpdate(simtime_t fromTime)
{
    if (updateTimer == nullptr || !enabled || !autoMaintain)
        return;

    if (updateTimer->isScheduled())
        cancelEvent(updateTimer);

    simtime_t nextUpdate = fromTime;
    if (nextUpdate < initialUpdateOffset)
        nextUpdate = initialUpdateOffset;
    if (nextUpdate < simTime())
        nextUpdate = simTime();

    updateTimer->setSchedulingPriority(updateSchedulingPriority);
    scheduleAt(nextUpdate, updateTimer);
}

void TopologyManager::maintainInterLayerLinks()
{
    cModule *primaryLayer = getRequiredLayer(par("primaryLayerModule").stringValue());
    cModule *peerLayer = getRequiredLayer(par("peerLayerModule").stringValue());

    auto primaryNodes = collectLayerNodes(primaryLayer, par("primaryNodeTypePattern").stringValue(), par("primaryNodeNamePattern").stringValue());
    auto peerNodes = collectLayerNodes(peerLayer, par("peerNodeTypePattern").stringValue(), par("peerNodeNamePattern").stringValue());

    auto desiredLinks = buildDesiredLinks(primaryNodes, peerNodes);
    removeObsoleteLinks(desiredLinks);
    addMissingLinks(desiredLinks);
}

cModule *TopologyManager::getRequiredLayer(const char *path) const
{
    cModule *layer = getModuleByPath(path);
    if (layer == nullptr)
        throw cRuntimeError("Layer module '%s' not found from '%s'", path, getFullPath().c_str());
    return layer;
}

std::vector<cModule *> TopologyManager::collectLayerNodes(cModule *layerModule, const char *typePattern, const char *namePattern) const
{
    std::vector<cModule *> nodes;

    for (cModule::SubmoduleIterator it(layerModule); !it.end(); ++it) {
        cModule *submodule = *it;
        if (matchesNodeFilters(submodule, typePattern, namePattern) && findMobility(submodule) != nullptr)
            nodes.push_back(submodule);
    }

    return nodes;
}

bool TopologyManager::matchesNodeFilters(cModule *node, const char *typePattern, const char *namePattern) const
{
    if (node == nullptr)
        return false;

    auto matches = [](const char *pattern, const char *value) {
        if (pattern == nullptr || pattern[0] == '\0' || !strcmp(pattern, "*"))
            return true;
        cPatternMatcher matcher(pattern, false, true, true);
        return matcher.matches(value);
    };

    return matches(typePattern, node->getNedTypeName()) && matches(namePattern, node->getFullName());
}

IMobility *TopologyManager::findMobility(cModule *node) const
{
    if (node == nullptr)
        return nullptr;

    auto mobilitySubmoduleName = par("mobilitySubmoduleName").stringValue();
    cModule *mobilityModule = node->getSubmodule(mobilitySubmoduleName);
    if (mobilityModule == nullptr)
        return nullptr;

    return dynamic_cast<IMobility *>(mobilityModule);
}

std::map<std::pair<int, int>, TopologyManager::LinkInfo> TopologyManager::buildDesiredLinks(const std::vector<cModule *>& primaryNodes, const std::vector<cModule *>& peerNodes) const
{
    std::map<std::pair<int, int>, LinkInfo> desiredLinks;
    int maxLinksPerPrimaryNode = par("maxLinksPerPrimaryNode").intValue();
    if (maxLinksPerPrimaryNode <= 0)
        return desiredLinks;

    double handoverMargin = par("handoverMargin").doubleValue();
    std::map<int, int> desiredGateUse;
    std::map<int, int> gateBudget;

    for (auto *primaryNode : primaryNodes)
        gateBudget[primaryNode->getId()] = countFreeGates(primaryNode, true);
    for (auto *peerNode : peerNodes)
        gateBudget[peerNode->getId()] = countFreeGates(peerNode, false);
    for (const auto& entry : activeLinks) {
        gateBudget[entry.second.sourceModuleId]++;
        gateBudget[entry.second.targetModuleId]++;
    }

    for (auto *primaryNode : primaryNodes) {
        std::vector<std::pair<double, cModule *>> candidates;
        candidates.reserve(peerNodes.size());

        for (auto *peerNode : peerNodes) {
            if (peerNode == primaryNode)
                continue;
            if (primaryNode->getParentModule() != peerNode->getParentModule())
                continue;

            double distance = computeDistance(primaryNode, peerNode);
            if (!isWithinRange(distance))
                continue;

            candidates.push_back({distance, peerNode});
        }

        std::sort(candidates.begin(), candidates.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });

        std::set<int> selectedTargetIds;
        std::vector<std::pair<std::pair<int, int>, LinkInfo>> primaryActiveLinks;
        for (const auto& entry : activeLinks) {
            if (entry.second.sourceModuleId == primaryNode->getId())
                primaryActiveLinks.push_back(entry);
        }

        double bestDistance = candidates.empty() ? std::numeric_limits<double>::infinity() : candidates.front().first;

        if (par("keepExistingLinks").boolValue()) {
            for (const auto& entry : primaryActiveLinks) {
                const auto& key = entry.first;
                const auto& link = entry.second;
                if ((int)selectedTargetIds.size() >= maxLinksPerPrimaryNode)
                    break;

                cModule *peerNode = findModuleById(link.targetModuleId);
                if (peerNode == nullptr)
                    continue;

                double distance = computeDistance(primaryNode, peerNode);
                if (!isWithinRange(distance))
                    continue;
                if (distance > bestDistance + handoverMargin)
                    continue;

                desiredLinks[key] = link;
                selectedTargetIds.insert(link.targetModuleId);
                desiredGateUse[link.sourceModuleId]++;
                desiredGateUse[link.targetModuleId]++;
            }
        }

        for (const auto& candidate : candidates) {
            if ((int)selectedTargetIds.size() >= maxLinksPerPrimaryNode)
                break;

            cModule *peerNode = candidate.second;
            auto key = std::make_pair(primaryNode->getId(), peerNode->getId());
            if (selectedTargetIds.count(peerNode->getId()) > 0 || desiredLinks.count(key) > 0)
                continue;

            int peerBudget = gateBudget[peerNode->getId()];
            int peerDesired = desiredGateUse[peerNode->getId()];
            int primaryBudget = gateBudget[primaryNode->getId()];
            int primaryDesired = desiredGateUse[primaryNode->getId()];
            EV_INFO << "Candidate " << peerNode->getFullPath() << " peerBudget=" << peerBudget
                     << " peerDesired=" << peerDesired
                     << " primaryBudget=" << primaryBudget
                     << " primaryDesired=" << primaryDesired << endl;
            if (peerDesired >= peerBudget || primaryDesired >= primaryBudget)
                continue;

            LinkInfo link;
            link.sourceModuleId = primaryNode->getId();
            link.targetModuleId = peerNode->getId();
            desiredLinks[key] = link;
            selectedTargetIds.insert(peerNode->getId());
            desiredGateUse[peerNode->getId()]++;
            desiredGateUse[primaryNode->getId()]++;
        }

        if (selectedTargetIds.empty() && !primaryActiveLinks.empty()) {
            const auto& fallback = primaryActiveLinks.front();
            desiredLinks[fallback.first] = fallback.second;
            desiredGateUse[fallback.second.sourceModuleId]++;
            desiredGateUse[fallback.second.targetModuleId]++;
            EV_WARN << "No feasible replacement link for " << primaryNode->getFullPath()
                    << ", keeping existing link until a switch is possible" << endl;
        }
    }

    return desiredLinks;
}

void TopologyManager::removeObsoleteLinks(const std::map<std::pair<int, int>, LinkInfo>& desiredLinks)
{
    std::vector<std::pair<int, int>> toRemove;
    toRemove.reserve(activeLinks.size());

    for (const auto& entry : activeLinks) {
        if (desiredLinks.find(entry.first) == desiredLinks.end())
            toRemove.push_back(entry.first);
    }

    for (const auto& key : toRemove) {
        auto it = activeLinks.find(key);
        if (it == activeLinks.end())
            continue;
        disconnectLink(it->second);
        activeLinks.erase(it);
    }
}

void TopologyManager::addMissingLinks(const std::map<std::pair<int, int>, LinkInfo>& desiredLinks)
{
    for (const auto& entry : desiredLinks) {
        const auto& key = entry.first;
        if (activeLinks.find(key) != activeLinks.end())
            continue;

        LinkInfo link = entry.second;
        connectLink(link);
        activeLinks[key] = link;
    }
}

void TopologyManager::connectLink(LinkInfo& link)
{
    cModule *sourceNode = findModuleById(link.sourceModuleId);
    cModule *targetNode = findModuleById(link.targetModuleId);
    if (sourceNode == nullptr || targetNode == nullptr)
        return;

    link.sourceGateIndex = findOrCreateFreeGateIndex(sourceNode, true);
    link.targetGateIndex = findOrCreateFreeGateIndex(targetNode, false);

    auto gateName = std::string(par("linkGateName").stringValue());
    if (topologyCoordinator != nullptr) {
        TopologyCoordinator::LinkCommand command;
        command.type = TopologyCoordinator::CommandType::CONNECT;
        command.sourceNode = sourceNode->getFullName();
        command.sourceGate = gateName + "[" + std::to_string(link.sourceGateIndex) + "]";
        command.destinationNode = targetNode->getFullName();
        command.destinationGate = gateName + "[" + std::to_string(link.targetGateIndex) + "]";
        auto result = topologyCoordinator->applyCommands({command});
        if (result.rejected != 0)
            throw cRuntimeError("TopologyCoordinator rejected automatic connect");
        return;
    }

    cXMLElement command("connect");
    command.setAttribute("src-module", sourceNode->getFullPath().c_str());
    command.setAttribute("src-gate", (gateName + "[" + std::to_string(link.sourceGateIndex) + "]").c_str());
    command.setAttribute("dest-module", targetNode->getFullPath().c_str());
    command.setAttribute("dest-gate", (gateName + "[" + std::to_string(link.targetGateIndex) + "]").c_str());
    command.setAttribute("channel-type", par("interLayerChannelType").stringValue());

    std::string datarateValue = par("datarate").str();
    cXMLElement *datarateParam = new cXMLElement("param");
    datarateParam->setAttribute("name", "datarate");
    datarateParam->setAttribute("value", datarateValue.c_str());
    command.appendChild(datarateParam);

    std::string propagationSpeedValue = par("propagationSpeed").str();
    cXMLElement *propagationSpeedParam = new cXMLElement("param");
    propagationSpeedParam->setAttribute("name", "propagationSpeed");
    propagationSpeedParam->setAttribute("value", propagationSpeedValue.c_str());
    command.appendChild(propagationSpeedParam);

    cChannelType *channelType = cChannelType::get(par("interLayerChannelType").stringValue());
    cGate *srcOutGate = sourceNode->gateHalf(gateName.c_str(), cGate::OUTPUT, link.sourceGateIndex);
    cGate *srcInGate  = sourceNode->gateHalf(gateName.c_str(), cGate::INPUT,  link.sourceGateIndex);
    cGate *destOutGate = targetNode->gateHalf(gateName.c_str(), cGate::OUTPUT, link.targetGateIndex);
    cGate *destInGate  = targetNode->gateHalf(gateName.c_str(), cGate::INPUT,  link.targetGateIndex);

    // Create and connect output->input channel
    cChannel *outChannel = nullptr;
    if (channelType) {
        outChannel = channelType->create("channel");
        outChannel->par("datarate").parse(datarateValue.c_str());
        outChannel->par("propagationSpeed").parse(propagationSpeedValue.c_str());
    }
    srcOutGate->connectTo(destInGate, outChannel, true);
    if (outChannel && !outChannel->parametersFinalized())
        outChannel->finalizeParameters();

    // Create and connect input<-output channel (reverse direction)
    cChannel *inChannel = nullptr;
    if (channelType) {
        inChannel = channelType->create("channel");
        inChannel->par("datarate").parse(datarateValue.c_str());
        inChannel->par("propagationSpeed").parse(propagationSpeedValue.c_str());
    }
    destOutGate->connectTo(srcInGate, inChannel, true);
    if (inChannel && !inChannel->parametersFinalized())
        inChannel->finalizeParameters();

    // Set display string on the new channels
    if (outChannel)
        outChannel->getDisplayString().setTagArg("ls", 0, par("linkColor").stringValue());
    if (inChannel)
        inChannel->getDisplayString().setTagArg("ls", 0, par("linkColor").stringValue());

    // Initialize the channels
    if (outChannel && !outChannel->initialized())
        outChannel->callInitialize();
    if (inChannel && !inChannel->initialized())
        inChannel->callInitialize();
}

void TopologyManager::disconnectLink(const LinkInfo& link)
{
    cModule *sourceNode = findModuleById(link.sourceModuleId);
    cModule *targetNode = findModuleById(link.targetModuleId);
    if (sourceNode == nullptr || targetNode == nullptr)
        return;

    auto gateName = std::string(par("linkGateName").stringValue());
    if (topologyCoordinator != nullptr) {
        TopologyCoordinator::LinkCommand command;
        command.type = TopologyCoordinator::CommandType::DISCONNECT;
        command.sourceNode = sourceNode->getFullName();
        command.sourceGate = gateName + "[" + std::to_string(link.sourceGateIndex) + "]";
        command.destinationNode = targetNode->getFullName();
        command.destinationGate = gateName + "[" + std::to_string(link.targetGateIndex) + "]";
        auto result = topologyCoordinator->applyCommands({command});
        if (result.rejected != 0)
            throw cRuntimeError("TopologyCoordinator rejected automatic disconnect");
        return;
    }
    cGate *sourceOutGate = sourceNode->gateHalf(gateName.c_str(), cGate::OUTPUT, link.sourceGateIndex);
    cGate *targetOutGate = targetNode->gateHalf(gateName.c_str(), cGate::OUTPUT, link.targetGateIndex);

    if (sourceOutGate && sourceOutGate->isConnected())
        sourceOutGate->disconnect();
    if (targetOutGate && targetOutGate->isConnected())
        targetOutGate->disconnect();
}

int TopologyManager::findOrCreateFreeGateIndex(cModule *module, bool primarySide) const
{
    auto gateName = par("linkGateName").stringValue();
    if (!module->hasGate(gateName))
        throw cRuntimeError("Module '%s' does not have gate '%s'", module->getFullPath().c_str(), gateName);

    int start = getGateRangeStart(primarySide);
    int end = getGateRangeEnd(module, primarySide);
    for (int i = start; i < end; ++i) {
        cGate *outGate = module->gateHalf(gateName, cGate::OUTPUT, i);
        cGate *inGate  = module->gateHalf(gateName, cGate::INPUT,  i);
        if (!outGate->isConnectedOutside() && !inGate->isConnectedOutside())
            return i;
    }

    const char *role = primarySide ? "primary" : "peer";
    throw cRuntimeError("Module '%s' has no free %s '%s' gate in configured range [%d, %d). "
                         "Increase numEthInterfaces or adjust %s gate range parameters.",
                         module->getFullPath().c_str(), role, gateName, start, end, role);
}

int TopologyManager::countFreeGates(cModule *module, bool primarySide) const
{
    auto gateName = par("linkGateName").stringValue();
    if (!module->hasGate(gateName))
        return 0;

    int count = 0;
    int start = getGateRangeStart(primarySide);
    int end = getGateRangeEnd(module, primarySide);
    for (int i = start; i < end; ++i) {
        cGate *outGate = module->gateHalf(gateName, cGate::OUTPUT, i);
        cGate *inGate  = module->gateHalf(gateName, cGate::INPUT,  i);
        if (!outGate->isConnectedOutside() && !inGate->isConnectedOutside())
            count++;
    }
    return count;
}

int TopologyManager::getGateRangeStart(bool primarySide) const
{
    const char *parName = primarySide ? "primaryFirstGateIndex" : "peerFirstGateIndex";
    int start = par(parName).intValue();
    if (start < 0)
        throw cRuntimeError("%s must be non-negative", parName);
    return start;
}

int TopologyManager::getGateRangeEnd(cModule *module, bool primarySide) const
{
    auto gateName = par("linkGateName").stringValue();
    int currentSize = module->gateSize(gateName);
    int start = getGateRangeStart(primarySide);
    const char *numParName = primarySide ? "primaryNumGateInterfaces" : "peerNumGateInterfaces";
    int numGateInterfaces = par(numParName).intValue();
    if (numGateInterfaces < -1)
        throw cRuntimeError("%s must be -1 or non-negative", numParName);
    if (start > currentSize)
        throw cRuntimeError("Module '%s' %s gate range starts at %d, beyond '%s' gate size %d. "
                             "Increase numEthInterfaces or adjust %s.",
                             module->getFullPath().c_str(), primarySide ? "primary" : "peer",
                             start, gateName, currentSize,
                             primarySide ? "primaryFirstGateIndex" : "peerFirstGateIndex");

    int end = numGateInterfaces < 0 ? currentSize : start + numGateInterfaces;
    if (end > currentSize)
        throw cRuntimeError("Module '%s' %s gate range [%d, %d) exceeds '%s' gate size %d. "
                             "Increase numEthInterfaces or adjust %s.",
                             module->getFullPath().c_str(), primarySide ? "primary" : "peer",
                             start, end, gateName, currentSize, numParName);
    return end;
}

cModule *TopologyManager::findModuleById(int moduleId) const
{
    return getSimulation()->getModule(moduleId);
}

double TopologyManager::computeDistance(cModule *sourceNode, cModule *targetNode) const
{
    IMobility *sourceMobility = findMobility(sourceNode);
    IMobility *targetMobility = findMobility(targetNode);
    if (sourceMobility == nullptr || targetMobility == nullptr)
        return std::numeric_limits<double>::infinity();

    auto sourcePosition = sourceMobility->getCurrentPosition();
    auto targetPosition = targetMobility->getCurrentPosition();
    return sourcePosition.distance(targetPosition);
}

bool TopologyManager::isWithinRange(double distance) const
{
    double maxRange = par("maxRange").doubleValue();
    return maxRange < 0 || distance <= maxRange;
}

} // namespace satnet3d