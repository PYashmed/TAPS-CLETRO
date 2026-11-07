#include "DynamicChannel.h"
#include "inet/common/ModuleAccess.h"
#include "inet/mobility/contract/IMobility.h"
#include "inet/common/geometry/common/Coord.h"

#include "DynamicChannelManager.h"
#include "../mobility/CircularOrbitMobility.h"

#include <cmath>

namespace satnet3d {

Define_Channel(DynamicChannel);

DynamicChannel::DynamicChannel() {}
DynamicChannel::~DynamicChannel()
{
    if (managedByDynamicChannelManager && DynamicChannelManager::isLiveManager(dynamicChannelManager))
        dynamicChannelManager->unregisterChannel(this);
}


bool DynamicChannel::isDisabled() const
{
    if (!parametersFinalized())
        return false;
    return cDatarateChannel::isDisabled();
}

simtime_t DynamicChannel::getDelay() const
{
    if (!parametersFinalized())
        return SIMTIME_ZERO;
    return dynamicDelayValid ? dynamicDelay : cDatarateChannel::getDelay();
}

double DynamicChannel::getDatarate() const
{
    // A dynamically installed channel emits path-change notifications from
    // connectTo() before TopologyManager can finish and initialize the channel.
    // Basic Ethernet MACs synchronously query the nominal datarate from that
    // notification, so expose the already assigned parameter in this short
    // window instead of reporting a transient zero bitrate.
    if (!initialized())
        return hasPar("datarate") ? par("datarate").doubleValue() : 0;
    return cDatarateChannel::getDatarate();
}

double DynamicChannel::getBitErrorRate() const
{
    if (!parametersFinalized())
        return 0;
    return cDatarateChannel::getBitErrorRate();
}

double DynamicChannel::getPacketErrorRate() const
{
    if (!parametersFinalized())
        return 0;
    return cDatarateChannel::getPacketErrorRate();
}

bool DynamicChannel::initializeChannel(int stage) {
    bool status = cDatarateChannel::initializeChannel(stage);

    propagationSpeed = par("propagationSpeed").doubleValue();
    if (propagationSpeed <= 0)
        throw cRuntimeError("propagationSpeed must be positive");
    autoUpdateDelay = par("autoUpdateDelay").boolValue();
    useDynamicChannelManager = par("useDynamicChannelManager").boolValue();
    minUpdateInterval = par("minUpdateInterval");
    sourceModule = getMobilityModule(getSourceGate()->getOwnerModule());
    destinationModule = getMobilityModule(getSourceGate()->getPathEndGate()->getOwnerModule());
    sourceMobilityComponent = sourceModule;
    destinationMobilityComponent = destinationModule;

    if (!sourceModule || !destinationModule)
        throw cRuntimeError("DynamicChannel requires both source and destination modules");

    managedByDynamicChannelManager = registerWithDynamicChannelManager();

    if (autoUpdateDelay && !managedByDynamicChannelManager && !subscribed) {
        sourceModule->subscribe(IMobility::mobilityStateChangedSignal, this);
        destinationModule->subscribe(IMobility::mobilityStateChangedSignal, this);
        subscribed = true;
    }

    updateDelay();
    return status;
}

DynamicChannel::Result DynamicChannel::processMessage(cMessage *msg, const SendOptions& options, simtime_t t)
{
    if (autoUpdateDelay || managedByDynamicChannelManager)
        updateDelay();
    auto result = cDatarateChannel::processMessage(msg, options, t);
    if (dynamicDelayValid)
        result.delay = dynamicDelay;
    return result;
}

void DynamicChannel::receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details) {
    if (signalID == IMobility::mobilityStateChangedSignal) {
        if (!autoUpdateDelay || managedByDynamicChannelManager || updatingDelay)
            return;

        if (simTime() - lastDelayUpdateTime < minUpdateInterval)
            return;

        if (source == sourceMobilityComponent) {
            sourcePosition = sourceMobility->getCurrentPosition();
            sourcePositionValid = true;
        }
        else if (source == destinationMobilityComponent) {
            destinationPosition = destinationMobility->getCurrentPosition();
            destinationPositionValid = true;
        }
        else
            return;

        if (sourcePositionValid && destinationPositionValid) {
            double dist = sourcePosition.distance(destinationPosition);
            double delay = dist / propagationSpeed;
            setCachedDelay(delay, simTime());
        }
    }
}

void DynamicChannel::managedUpdateDelay()
{
    updateDelay();
}

simtime_t DynamicChannel::refreshAndGetDelay()
{
    updateDelay(true);
    if (!dynamicDelayValid)
        throw cRuntimeError("DynamicChannel could not refresh propagation delay");
    return dynamicDelay;
}

simtime_t DynamicChannel::predictDelayAt(simtime_t targetTime) const
{
    if (!sourceMobility || !destinationMobility)
        throw cRuntimeError("DynamicChannel mobility endpoints are not initialized");

    auto predictPosition = [this, targetTime](IMobility *mobility) {
        if (auto *orbitMobility = dynamic_cast<CircularOrbitMobility *>(mobility))
            return orbitMobility->getPositionAt(targetTime);
        simtime_t deltaTime = targetTime - simTime();
        return mobility->getCurrentPosition() + mobility->getCurrentVelocity() * deltaTime.dbl();
    };

    Coord predictedSourcePosition = predictPosition(sourceMobility);
    Coord predictedDestinationPosition = predictPosition(destinationMobility);
    return simtime_t(predictedSourcePosition.distance(predictedDestinationPosition) / propagationSpeed);
}

bool DynamicChannel::collectManagedDelayUpdateInput(DelayUpdateInput& input, bool force)
{
    input = DelayUpdateInput();

    if (updatingDelay)
        return false;

    if (!force && simTime() - lastDelayUpdateTime < minUpdateInterval)
        return false;

    updatingDelay = true;
    resolveMobilities();
    if (!sourceMobility || !destinationMobility) {
        updatingDelay = false;
        return false;
    }

    sourcePosition = sourceMobility->getCurrentPosition();
    sourcePositionValid = true;
    destinationPosition = destinationMobility->getCurrentPosition();
    destinationPositionValid = true;

    input.channel = this;
    input.sourcePosition = sourcePosition;
    input.destinationPosition = destinationPosition;
    input.propagationSpeed = propagationSpeed;
    input.valid = sourcePositionValid && destinationPositionValid && propagationSpeed > 0 &&
        std::isfinite(sourcePosition.x) && std::isfinite(sourcePosition.y) && std::isfinite(sourcePosition.z) &&
        std::isfinite(destinationPosition.x) && std::isfinite(destinationPosition.y) && std::isfinite(destinationPosition.z);

    updatingDelay = false;
    return input.valid;
}

void DynamicChannel::applyManagedDelay(double delay, simtime_t updateTime)
{
    setCachedDelay(delay, updateTime);
}

cModule *DynamicChannel::getSourceNodeModule() const
{
    return sourceModule != nullptr ? sourceModule->getParentModule() : nullptr;
}

cModule *DynamicChannel::getDestinationNodeModule() const
{
    return destinationModule != nullptr ? destinationModule->getParentModule() : nullptr;
}

bool DynamicChannel::registerWithDynamicChannelManager()
{
    if (!useDynamicChannelManager)
        return false;

    const char *managerPath = par("dynamicChannelManagerModule").stringValue();
    cModule *contextModule = getSourceGate() != nullptr ? getSourceGate()->getOwnerModule() : nullptr;
    cModule *managerModule = contextModule != nullptr ? contextModule->getModuleByPath(managerPath) : nullptr;
    dynamicChannelManager = dynamic_cast<DynamicChannelManager *>(managerModule);
    if (dynamicChannelManager == nullptr) {
        EV_WARN << "DynamicChannelManager module '" << managerPath
                << "' not found, falling back to signal-based delay updates" << endl;
        return false;
    }

    return dynamicChannelManager->registerChannel(this);
}

void DynamicChannel::updateDelay(bool force) {
    if (updatingDelay)
        return;

    if (!force && simTime() - lastDelayUpdateTime < minUpdateInterval)
        return;

    updatingDelay = true;
    resolveMobilities();
    if (!sourceMobility || !destinationMobility) {
        updatingDelay = false;
        return;
    }

    sourcePosition = sourceMobility->getCurrentPosition();
    sourcePositionValid = true;
    destinationPosition = destinationMobility->getCurrentPosition();
    destinationPositionValid = true;

    if (sourcePositionValid && destinationPositionValid) {
        double dist = sourcePosition.distance(destinationPosition);
        if (std::isfinite(dist)) {
            double delay = dist / propagationSpeed;
            setCachedDelay(delay, simTime());
        }
    }

    updatingDelay = false;
}

void DynamicChannel::setCachedDelay(double delay, simtime_t updateTime)
{
    if (!std::isfinite(delay) || delay < 0)
        throw cRuntimeError("Invalid dynamic channel delay %g", delay);

    dynamicDelay = delay;
    dynamicDelayValid = true;
    lastDelayUpdateTime = updateTime;
}

void DynamicChannel::resolveMobilities()
{
    if (!sourceMobility && sourceModule != nullptr) {
        sourceMobility = dynamic_cast<IMobility *>(sourceModule);
        if (!sourceMobility) {
            EV_WARN << "Source module " << sourceModule->getFullPath() << " does not have a mobility submodule!" << endl;
        }
    }
    if (!destinationMobility && destinationModule != nullptr) {
        destinationMobility = dynamic_cast<IMobility *>(destinationModule);
        if (!destinationMobility) {
            EV_WARN << "Destination module " << destinationModule->getFullPath() << " does not have a mobility submodule!" << endl;
        }
    }
}

cModule* DynamicChannel::getMobilityModule(cModule *module) {
    if (module == nullptr) {
        return nullptr;
    }
    cModule *mobilityModule = module->getSubmodule("mobility");
    if (mobilityModule && dynamic_cast<IMobility*>(mobilityModule)) {
        return mobilityModule;
    }

    if (module->getParentModule()) {
        return getMobilityModule(module->getParentModule());
    }

    return nullptr;
}

} // namespace satnet3d
