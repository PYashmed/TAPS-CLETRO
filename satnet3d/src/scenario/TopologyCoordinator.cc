#include "TopologyCoordinator.h"
#include "NetworkStateManager.h"

#include <regex>

namespace satnet3d {

Define_Module(TopologyCoordinator);

void TopologyCoordinator::initialize()
{
    networkModule = getModuleByPath(par("networkModule").stringValue());
    if (networkModule == nullptr)
        throw cRuntimeError("networkModule not found");
    linkGateName = par("linkGateName").stringValue();
    channelTypeName = par("channelType").stringValue();
    datarateValue = par("datarate").str();
    propagationSpeedValue = par("propagationSpeed").str();
    linkColor = par("linkColor").stringValue();
    const char *stateManagerPath = par("networkStateManagerModule").stringValue();
    if (stateManagerPath[0] != '\0') {
        networkStateManager = dynamic_cast<NetworkStateManager *>(getModuleByPath(stateManagerPath));
        if (networkStateManager == nullptr)
            throw cRuntimeError("networkStateManagerModule %s not found", stateManagerPath);
    }
}

cModule *TopologyCoordinator::findNode(const std::string& fullName) const
{
    for (cModule::SubmoduleIterator it(networkModule); !it.end(); ++it) {
        cModule *node = *it;
        if (std::string(node->getFullName()) == fullName)
            return node;
    }
    return nullptr;
}

bool TopologyCoordinator::parseGate(const std::string& gate, int& index) const
{
    std::regex pattern("^" + linkGateName + R"(\[(\d+)\]$)");
    std::smatch match;
    if (!std::regex_match(gate, match, pattern))
        return false;
    index = std::stoi(match[1].str());
    return true;
}

bool TopologyCoordinator::isConnectedPair(cModule *source, int sourceIndex, cModule *destination, int destinationIndex) const
{
    auto reachesEndpoint = [this](cGate *output, cModule *expectedNode, int expectedIndex) {
        if (!output->isConnectedOutside())
            return false;
        for (cGate *gate = output->getPathEndGate(); gate != nullptr; gate = gate->getPreviousGate()) {
            if (gate->getOwnerModule() == expectedNode && gate->isVector() &&
                std::string(gate->getBaseName()) == linkGateName && gate->getIndex() == expectedIndex)
                return true;
        }
        return false;
    };
    return reachesEndpoint(source->gateHalf(linkGateName.c_str(), cGate::OUTPUT, sourceIndex), destination, destinationIndex) &&
           reachesEndpoint(destination->gateHalf(linkGateName.c_str(), cGate::OUTPUT, destinationIndex), source, sourceIndex);
}

cChannel *TopologyCoordinator::createChannel() const
{
    cChannelType *channelType = cChannelType::get(channelTypeName.c_str());
    if (channelType == nullptr)
        throw cRuntimeError("Channel type '%s' not found", channelTypeName.c_str());
    cChannel *channel = channelType->create("channel");
    if (channel->hasPar("datarate"))
        channel->par("datarate").parse(datarateValue.c_str());
    if (channel->hasPar("propagationSpeed"))
        channel->par("propagationSpeed").parse(propagationSpeedValue.c_str());
    return channel;
}

void TopologyCoordinator::initializeChannelsRecursively(cModule *module) const
{
    if (module == nullptr)
        return;
    for (cModule::SubmoduleIterator it(module); !it.end(); ++it)
        initializeChannelsRecursively(*it);
    for (cModule::GateIterator it(module); !it.end(); ++it) {
        cChannel *channel = (*it)->getChannel();
        if (channel != nullptr && !channel->initialized())
            channel->callInitialize();
    }
}

bool TopologyCoordinator::connect(const LinkCommand& command, std::string& reason)
{
    int sourceIndex = -1;
    int destinationIndex = -1;
    cModule *source = findNode(command.sourceNode);
    cModule *destination = findNode(command.destinationNode);
    if (source == nullptr || destination == nullptr || !parseGate(command.sourceGate, sourceIndex) || !parseGate(command.destinationGate, destinationIndex)) {
        reason = "invalid endpoint";
        return false;
    }
    if (!source->hasGate(linkGateName.c_str()) || !destination->hasGate(linkGateName.c_str()) ||
        sourceIndex >= source->gateSize(linkGateName.c_str()) || destinationIndex >= destination->gateSize(linkGateName.c_str())) {
        reason = "gate not found";
        return false;
    }
    if (isConnectedPair(source, sourceIndex, destination, destinationIndex)) {
        reason = "already connected";
        return true;
    }

    cGate *sourceOut = source->gateHalf(linkGateName.c_str(), cGate::OUTPUT, sourceIndex);
    cGate *sourceIn = source->gateHalf(linkGateName.c_str(), cGate::INPUT, sourceIndex);
    cGate *destinationOut = destination->gateHalf(linkGateName.c_str(), cGate::OUTPUT, destinationIndex);
    cGate *destinationIn = destination->gateHalf(linkGateName.c_str(), cGate::INPUT, destinationIndex);
    if (sourceOut->isConnectedOutside() || sourceIn->isConnectedOutside() || destinationOut->isConnectedOutside() || destinationIn->isConnectedOutside()) {
        reason = "port already in use";
        return false;
    }

    cChannel *forward = createChannel();
    cChannel *reverse = createChannel();
    sourceOut->connectTo(destinationIn, forward, true);
    destinationOut->connectTo(sourceIn, reverse, true);
    for (auto *channel : {forward, reverse}) {
        channel->getDisplayString().setTagArg("ls", 0, (command.linkColor.empty() ? linkColor : command.linkColor).c_str());
        if (!channel->parametersFinalized())
            channel->finalizeParameters();
        if (!channel->initialized())
            channel->callInitialize();
    }
    initializeChannelsRecursively(source);
    initializeChannelsRecursively(destination);
    return true;
}

bool TopologyCoordinator::disconnect(const LinkCommand& command, std::string& reason)
{
    int sourceIndex = -1;
    int destinationIndex = -1;
    cModule *source = findNode(command.sourceNode);
    cModule *destination = findNode(command.destinationNode);
    if (source == nullptr || destination == nullptr || !parseGate(command.sourceGate, sourceIndex) || !parseGate(command.destinationGate, destinationIndex)) {
        reason = "invalid endpoint";
        return false;
    }
    if (!isConnectedPair(source, sourceIndex, destination, destinationIndex)) {
        reason = "already disconnected";
        return true;
    }
    source->gateHalf(linkGateName.c_str(), cGate::OUTPUT, sourceIndex)->disconnect();
    destination->gateHalf(linkGateName.c_str(), cGate::OUTPUT, destinationIndex)->disconnect();
    return true;
}

TopologyCoordinator::ApplyResult TopologyCoordinator::applyCommands(const std::vector<LinkCommand>& commands)
{
    Enter_Method("apply topology commands");
    ApplyResult result;
    for (const auto& command : commands) {
        std::string reason;
        bool success = command.type == CommandType::CONNECT ? connect(command, reason) : disconnect(command, reason);
        if (!success) {
            result.rejected++;
            EV_WARN << "Rejected topology command: " << reason << endl;
        }
        else if (reason.rfind("already", 0) == 0)
            result.noops++;
        else
            result.applied++;
    }
    EV_INFO << "Topology commands: applied=" << result.applied << " noops=" << result.noops << " rejected=" << result.rejected << endl;
    if (result.applied > 0 && networkStateManager != nullptr)
        networkStateManager->refreshTopologyState();
    return result;
}

} // namespace satnet3d
