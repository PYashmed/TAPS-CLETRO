#include "PeriodicGateManager.h"

#include <algorithm>
#include <chrono>

#include "SatNet3DPeriodicGate.h"

using namespace omnetpp;
using namespace inet;

namespace satnet3d {
namespace queueing {

Define_Module(PeriodicGateManager);

std::set<const PeriodicGateManager *> PeriodicGateManager::liveManagers;

void PeriodicGateManager::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL) {
        liveManagers.insert(this);
        enabled = par("enabled");
        schedulingPriority = par("schedulingPriority");
        batchTimer = new cMessage("periodic-gate-batch");
        batchTimer->setSchedulingPriority(schedulingPriority);
    }
    else if (stage == INITSTAGE_LAST)
        scheduleNextBatch();
}

PeriodicGateManager::~PeriodicGateManager()
{
    liveManagers.erase(this);
    cancelAndDelete(batchTimer);
}

void PeriodicGateManager::finish()
{
    recordScalar("wallBatchTotalSeconds", wallBatchTotalSeconds);
    recordScalar("wallDueScanSeconds", wallDueScanSeconds);
    recordScalar("wallProcessSeconds", wallProcessSeconds);
    recordScalar("wallScheduleScanSeconds", wallScheduleScanSeconds);
    if (batchTimer != nullptr && batchTimer->isScheduled())
        cancelEvent(batchTimer);
}

bool PeriodicGateManager::isLiveManager(const PeriodicGateManager *manager)
{
    return liveManagers.find(manager) != liveManagers.end();
}

void PeriodicGateManager::registerGate(SatNet3DPeriodicGate *gate)
{
    if (std::find(gates.begin(), gates.end(), gate) == gates.end())
        gates.push_back(gate);
    scheduleNextBatch();
}

void PeriodicGateManager::unregisterGate(SatNet3DPeriodicGate *gate)
{
    auto it = std::remove(gates.begin(), gates.end(), gate);
    if (it != gates.end())
        gates.erase(it, gates.end());
    scheduleNextBatch();
}

void PeriodicGateManager::requestReschedule()
{
    // A managed gate requests rescheduling after every state transition. While
    // processing a batch, defer these requests and scan all gates just once at
    // the end of the batch.
    if (!processingBatch)
        scheduleNextBatch();
}

void PeriodicGateManager::handleMessage(cMessage *message)
{
    if (message != batchTimer)
        throw cRuntimeError("Unknown message");

    auto batchStart = std::chrono::steady_clock::now();
    auto now = SIMTIME_AS_CLOCKTIME(simTime());
    auto dueScanStart = std::chrono::steady_clock::now();
    std::vector<SatNet3DPeriodicGate *> dueGates;
    for (auto gate : gates) {
        if (gate->hasScheduledManagedChange() && gate->getNextChangeClockTime() <= now)
            dueGates.push_back(gate);
    }

    wallDueScanSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - dueScanStart).count();

    EV_TRACE << "Processing periodic gate batch" << EV_FIELD(dueGates.size()) << EV_ENDL;
    auto processStart = std::chrono::steady_clock::now();
    processingBatch = true;
    for (auto gate : dueGates)
        gate->processManagedChangeTimer();
    processingBatch = false;
    wallProcessSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - processStart).count();

    scheduleNextBatch();
    wallBatchTotalSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - batchStart).count();
}

void PeriodicGateManager::scheduleNextBatch()
{
    cContextSwitcher contextSwitcher(this);
    if (batchTimer == nullptr || !enabled)
        return;

    if (batchTimer->isScheduled())
        cancelEvent(batchTimer);

    bool found = false;
    clocktime_t nextTime = CLOCKTIME_ZERO;
    auto now = SIMTIME_AS_CLOCKTIME(simTime());
    auto scheduleScanStart = std::chrono::steady_clock::now();
    for (auto gate : gates) {
        if (!gate->hasScheduledManagedChange())
            continue;
        auto gateTime = gate->getNextChangeClockTime();
        if (gateTime < now)
            gateTime = now;
        if (!found || gateTime < nextTime) {
            found = true;
            nextTime = gateTime;
        }
    }

    wallScheduleScanSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - scheduleScanStart).count();

    if (found) {
        batchTimer->setSchedulingPriority(schedulingPriority);
        scheduleAt(CLOCKTIME_AS_SIMTIME(nextTime), batchTimer);
    }
}

} // namespace queueing
} // namespace satnet3d
