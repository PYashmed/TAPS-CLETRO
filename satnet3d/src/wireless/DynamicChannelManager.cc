#include "DynamicChannelManager.h"

#include "DynamicChannel.h"

#include <chrono>
#include <map>
#include <set>
#include <vector>

#include "inet/common/InitStages.h"


namespace satnet3d {

static std::set<DynamicChannelManager *> liveManagers;

Define_Module(DynamicChannelManager);

int DynamicChannelManager::numInitStages() const
{
    return inet::NUM_INIT_STAGES;
}

DynamicChannelManager::~DynamicChannelManager()
{
    liveManagers.erase(this);
    cancelAndDelete(updateTimer);
}

void DynamicChannelManager::initialize(int stage)
{
    if (stage == inet::INITSTAGE_LOCAL) {
        liveManagers.insert(this);
        enabled = par("enabled").boolValue();
        updateInterval = par("updateInterval");
        schedulingPriority = par("schedulingPriority").intValue();
        reportManagedChannels = par("reportManagedChannels").boolValue();
        if (enabled && updateInterval <= SIMTIME_ZERO)
            throw cRuntimeError("updateInterval must be positive when DynamicChannelManager is enabled");

        updateTimer = new cMessage("managed-dynamic-channel-update");
        updateTimer->setSchedulingPriority(schedulingPriority);

        if (enabled && !channels.empty()) {
            updateChannels(true);
            scheduleNext();
        }
    }
    else if (stage == inet::INITSTAGE_LAST && enabled && !channels.empty()) {
        updateChannels(true);
        scheduleNext();
    }
}

void DynamicChannelManager::handleMessage(cMessage *msg)
{
    if (msg != updateTimer)
        throw cRuntimeError("Unexpected message '%s'", msg->getName());

    updateChannels();

    scheduleNext();
}

void DynamicChannelManager::updateChannels(bool force)
{
    auto totalStart = std::chrono::steady_clock::now();
    struct DelayUpdateResult {
        DynamicChannel *channel = nullptr;
        double delay = 0;
        bool valid = false;
    };

    std::vector<DynamicChannel::DelayUpdateInput> inputs;
    inputs.reserve(channels.size());

    auto collectStart = std::chrono::steady_clock::now();
    for (auto *channel : channels) {
        if (channel == nullptr)
            continue;

        DynamicChannel::DelayUpdateInput input;
        if (channel->collectManagedDelayUpdateInput(input, force))
            inputs.push_back(input);
    }

    wallCollectSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - collectStart).count();

    std::vector<DelayUpdateResult> results(inputs.size());
    auto computeStart = std::chrono::steady_clock::now();
    for (int i = 0; i < static_cast<int>(inputs.size()); ++i) {
        const auto& input = inputs[i];
        DelayUpdateResult result;
        result.channel = input.channel;
        result.valid = input.valid && input.propagationSpeed > 0;
        if (result.valid) {
            double distance = input.sourcePosition.distance(input.destinationPosition);
            result.delay = distance / input.propagationSpeed;
        }
        results[i] = result;
    }

    wallComputeSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - computeStart).count();

    auto applyStart = std::chrono::steady_clock::now();
    simtime_t now = simTime();
    for (const auto& result : results) {
        if (result.valid && result.channel != nullptr)
            result.channel->applyManagedDelay(result.delay, now);
    }
    wallApplySeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - applyStart).count();
    wallTotalSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - totalStart).count();
}

bool DynamicChannelManager::updateChannel(DynamicChannel *channel, bool force)
{
    if (channel == nullptr)
        return false;

    DynamicChannel::DelayUpdateInput input;
    if (!channel->collectManagedDelayUpdateInput(input, force))
        return false;

    if (!input.valid || input.propagationSpeed <= 0)
        return false;

    double distance = input.sourcePosition.distance(input.destinationPosition);
    double delay = distance / input.propagationSpeed;
    channel->applyManagedDelay(delay, simTime());
    return true;
}

void DynamicChannelManager::finish()
{
    recordScalar("wallTotalSeconds", wallTotalSeconds);
    recordScalar("wallCollectSeconds", wallCollectSeconds);
    recordScalar("wallComputeSeconds", wallComputeSeconds);
    recordScalar("wallApplySeconds", wallApplySeconds);
    if (reportManagedChannels)
        reportChannelCoverage();
    channels.clear();
    if (updateTimer != nullptr)
        cancelEvent(updateTimer);
}

bool DynamicChannelManager::isLiveManager(DynamicChannelManager *manager)
{
    return manager != nullptr && liveManagers.find(manager) != liveManagers.end();
}

bool DynamicChannelManager::registerChannel(DynamicChannel *channel)
{
    Enter_Method_Silent();

    if (!enabled || channel == nullptr)
        return false;

    channels.insert(channel);
    bool updated = updateChannel(channel, true);
    scheduleNext();
    EV_INFO << "Registered managed dynamic channel " << channel->getFullPath()
            << ", initialDelayUpdated=" << updated << endl;
    return true;
}

void DynamicChannelManager::unregisterChannel(DynamicChannel *channel)
{
    Enter_Method_Silent();

    if (channel == nullptr)
        return;

    channels.erase(channel);
    scheduleNext();
}

void DynamicChannelManager::scheduleNext()
{
    if (updateTimer == nullptr)
        return;

    cancelEvent(updateTimer);
    if (!enabled || channels.empty())
        return;

    scheduleAt(simTime() + updateInterval, updateTimer);
}

std::string DynamicChannelManager::classifyChannel(DynamicChannel *channel) const
{
    auto isSatellite = [](cModule *module) {
        return module != nullptr && strstr(module->getNedTypeName(), "Satellite") != nullptr;
    };
    auto isGround = [](cModule *module) {
        return module != nullptr && strstr(module->getNedTypeName(), "Earth") != nullptr;
    };

    cModule *sourceNode = channel != nullptr ? channel->getSourceNodeModule() : nullptr;
    cModule *destinationNode = channel != nullptr ? channel->getDestinationNodeModule() : nullptr;
    bool sourceSatellite = isSatellite(sourceNode);
    bool destinationSatellite = isSatellite(destinationNode);
    bool sourceGround = isGround(sourceNode);
    bool destinationGround = isGround(destinationNode);

    if (sourceSatellite && destinationSatellite)
        return "satellite-satellite";
    if ((sourceSatellite && destinationGround) || (sourceGround && destinationSatellite))
        return "ground-satellite";
    if (sourceGround && destinationGround)
        return "ground-ground";
    return "other";
}

void DynamicChannelManager::reportChannelCoverage()
{
    std::map<std::string, int> counts;
    for (auto *channel : channels)
        counts[classifyChannel(channel)]++;

    int total = channels.size();
    recordScalar("managedChannels", total);
    recordScalar("managedSatelliteSatelliteChannels", counts["satellite-satellite"]);
    recordScalar("managedGroundSatelliteChannels", counts["ground-satellite"]);
    recordScalar("managedGroundGroundChannels", counts["ground-ground"]);
    recordScalar("managedOtherChannels", counts["other"]);

    EV_WARN << "DynamicChannelManager coverage: total=" << total
            << " satellite-satellite=" << counts["satellite-satellite"]
            << " ground-satellite=" << counts["ground-satellite"]
            << " ground-ground=" << counts["ground-ground"]
            << " other=" << counts["other"] << endl;
}

} // namespace satnet3d
