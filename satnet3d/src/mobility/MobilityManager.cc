#include "MobilityManager.h"

#include <algorithm>
#include <chrono>


#include "GeodeticMobilityBase.h"

namespace satnet3d {

Define_Module(MobilityManager);

MobilityManager::~MobilityManager()
{
    cancelAndDelete(updateTimer);
}

void MobilityManager::initialize()
{
    enabled = par("enabled").boolValue();
    schedulingPriority = par("schedulingPriority").intValue();
    updateTimer = new cMessage("managed-mobility-update");
    updateTimer->setSchedulingPriority(schedulingPriority);
}

void MobilityManager::handleMessage(cMessage *msg)
{
    if (msg != updateTimer)
        throw cRuntimeError("Unexpected message '%s'", msg->getName());

    auto totalStart = std::chrono::steady_clock::now();
    simtime_t now = simTime();
    std::vector<GeodeticMobilityBase *> dueMobilities;
    auto collectStart = std::chrono::steady_clock::now();

    while (!updateBuckets.empty() && updateBuckets.begin()->first <= now) {
        auto due = updateBuckets.begin();
        dueMobilities.insert(dueMobilities.end(), due->second.begin(), due->second.end());
        updateBuckets.erase(due);
    }

    std::vector<Registration> dueRegistrations;
    dueRegistrations.reserve(dueMobilities.size());
    for (auto *mobility : dueMobilities) {
        auto it = registrations.find(mobility);
        if (it != registrations.end() && it->second.mobility != nullptr)
            dueRegistrations.push_back(it->second);
    }

    wallCollectSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - collectStart).count();

    std::vector<simtime_t> nextUpdateTimes(dueRegistrations.size());
    for (size_t i = 0; i < dueRegistrations.size(); ++i)
        nextUpdateTimes[i] = now + dueRegistrations[i].interval;

    auto moveStart = std::chrono::steady_clock::now();
    std::vector<unsigned char> mobilityChanged(dueRegistrations.size(), 0);
    for (int i = 0; i < static_cast<int>(dueRegistrations.size()); ++i) {
        mobilityChanged[i] = dueRegistrations[i].mobility->managedMove() ? 1 : 0;
    }

    for (size_t i = 0; i < dueRegistrations.size(); ++i) {
        if (mobilityChanged[i])
            dueRegistrations[i].mobility->emitManagedMobilityStateChangedSignal();
    }

    wallMoveSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - moveStart).count();

    auto scheduleStart = std::chrono::steady_clock::now();
    for (size_t i = 0; i < dueRegistrations.size(); ++i)
        scheduleMobility(dueRegistrations[i].mobility, nextUpdateTimes[i]);

    scheduleNext();
    wallScheduleSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - scheduleStart).count();
    wallTotalSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - totalStart).count();
}

void MobilityManager::finish()
{
    recordScalar("wallTotalSeconds", wallTotalSeconds);
    recordScalar("wallCollectSeconds", wallCollectSeconds);
    recordScalar("wallMoveSeconds", wallMoveSeconds);
    recordScalar("wallScheduleSeconds", wallScheduleSeconds);
    registrations.clear();
    updateBuckets.clear();
    if (updateTimer != nullptr)
        cancelEvent(updateTimer);
}

bool MobilityManager::registerMobility(GeodeticMobilityBase *mobility, simtime_t interval)
{
    Enter_Method_Silent();

    if (!enabled || mobility == nullptr || interval <= SIMTIME_ZERO)
        return false;

    unregisterMobility(mobility);

    registrations[mobility] = {mobility, interval};
    scheduleMobility(mobility, simTime() + interval);
    scheduleNext();

    EV_INFO << "Registered managed mobility " << mobility->getFullPath()
            << " interval=" << interval << endl;
    return true;
}

void MobilityManager::unregisterMobility(GeodeticMobilityBase *mobility)
{
    Enter_Method_Silent();

    if (mobility == nullptr)
        return;

    registrations.erase(mobility);
    removeFromBuckets(mobility);
    scheduleNext();
}

void MobilityManager::scheduleMobility(GeodeticMobilityBase *mobility, simtime_t updateTime)
{
    updateBuckets[updateTime].push_back(mobility);
}

void MobilityManager::removeFromBuckets(GeodeticMobilityBase *mobility)
{
    for (auto it = updateBuckets.begin(); it != updateBuckets.end(); ) {
        auto& mobilities = it->second;
        mobilities.erase(std::remove(mobilities.begin(), mobilities.end(), mobility), mobilities.end());
        if (mobilities.empty())
            it = updateBuckets.erase(it);
        else
            ++it;
    }
}

void MobilityManager::scheduleNext()
{
    if (updateTimer == nullptr)
        return;

    cancelEvent(updateTimer);
    if (!enabled || updateBuckets.empty())
        return;

    simtime_t nextUpdate = updateBuckets.begin()->first;
    if (nextUpdate < simTime())
        nextUpdate = simTime();
    scheduleAt(nextUpdate, updateTimer);
}

} // namespace satnet3d
