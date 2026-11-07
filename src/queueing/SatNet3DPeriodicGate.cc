#include "SatNet3DPeriodicGate.h"

#include "inet/common/ModuleAccess.h"
#include "PeriodicGateManager.h"

using namespace omnetpp;
using namespace inet;

namespace satnet3d {
namespace queueing {

Define_Module(SatNet3DPeriodicGate);

simsignal_t SatNet3DPeriodicGate::guardBandStateChangedSignal = registerSignal("guardBandStateChanged");

void SatNet3DPeriodicGate::initialize(int stage)
{
    ClockUserModuleMixin::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        initiallyOpen = par("initiallyOpen");
        initialOffset = par("offset");
        readDurationsPar();
        scheduleForAbsoluteTime = par("scheduleForAbsoluteTime");
        enableImplicitGuardBand = par("enableImplicitGuardBand");
        openSchedulingPriority = par("openSchedulingPriority");
        closeSchedulingPriority = par("closeSchedulingPriority");
        usePeriodicGateManager = par("usePeriodicGateManager");
        changeTimer = new ClockEvent("ChangeTimer");
        WATCH(isInGuardBand_);
    }
    else if (stage == INITSTAGE_QUEUEING) {
        registerWithManager();
        initializeGating();
    }
    else if (stage == INITSTAGE_LAST)
        emit(guardBandStateChangedSignal, isInGuardBand_);
}

SatNet3DPeriodicGate::~SatNet3DPeriodicGate()
{
    if (periodicGateManager != nullptr && PeriodicGateManager::isLiveManager(periodicGateManager))
        periodicGateManager->unregisterGate(this);
    cancelAndDeleteClockEvent(changeTimer);
}

void SatNet3DPeriodicGate::finish()
{
    emit(guardBandStateChangedSignal, isInGuardBand_);
}

void SatNet3DPeriodicGate::registerWithManager()
{
    if (!usePeriodicGateManager)
        return;

    const char *managerPath = par("periodicGateManagerModule");
    if (managerPath == nullptr || *managerPath == '\0')
        return;

    periodicGateManager = getModuleFromPar<PeriodicGateManager>(par("periodicGateManagerModule"), this);
    if (periodicGateManager != nullptr && periodicGateManager->isEnabled())
        periodicGateManager->registerGate(this);
    else
        periodicGateManager = nullptr;
}

void SatNet3DPeriodicGate::handleParameterChange(const char *name)
{
    if (name == nullptr)
        return;
    if (!strcmp(name, "offset"))
        initialOffset = par("offset");
    else if (!strcmp(name, "initiallyOpen"))
        initiallyOpen = par("initiallyOpen");
    else if (!strcmp(name, "durations"))
        readDurationsPar();
    initializeGating();
}

void SatNet3DPeriodicGate::handleMessage(cMessage *message)
{
    if (message == changeTimer) {
        scheduleChangeTimer();
        processChangeTimer();
    }
    else
        throw cRuntimeError("Unknown message");
}

void SatNet3DPeriodicGate::readDurationsPar()
{
    auto durationsPar = check_and_cast<cValueArray *>(par("durations").objectValue());
    size_t size = durationsPar->size();
    if (size % 2 != 0)
        throw cRuntimeError("The duration parameter must contain an even number of values");
    totalDuration = CLOCKTIME_ZERO;
    durations.resize(size);
    for (size_t i = 0; i < size; i++) {
        clocktime_t duration = durationsPar->get(i).doubleValueInUnit("s");
        if (duration <= CLOCKTIME_ZERO)
            throw cRuntimeError("Unaccepted duration value (%s) at position %u", durationsPar->get(i).str().c_str(), (unsigned int)i);
        durations[i] = duration;
        totalDuration += duration;
    }
}

void SatNet3DPeriodicGate::initializeGating()
{
    index = 0;
    isOpen_ = initiallyOpen;
    if (totalDuration.raw() == 0)
        offset.setRaw(0);
    else
        offset.setRaw((initialOffset.raw() % totalDuration.raw() + totalDuration.raw()) % totalDuration.raw());
    while (offset > CLOCKTIME_ZERO) {
        clocktime_t duration = durations[index];
        if (offset >= duration) {
            isOpen_ = !isOpen_;
            offset -= duration;
            index = (index + 1) % durations.size();
        }
        else
            break;
    }

    managedChangeScheduled = false;
    if (changeTimer->isScheduled())
        cancelClockEvent(changeTimer);
    if (durations.size() > 0)
        scheduleChangeTimer();
    updateIsInGuardBand();
}

void SatNet3DPeriodicGate::scheduleChangeTimer()
{
    ASSERT(0 <= index && (size_t)index < durations.size());
    clocktime_t duration = durations[index];
    index = (index + 1) % durations.size();

    auto priority = isOpen_ ? closeSchedulingPriority : openSchedulingPriority;
    if (periodicGateManager != nullptr) {
        nextChangeClockTime = getClockTime() + duration - offset;
        managedChangeScheduled = true;
        periodicGateManager->requestReschedule();
    }
    else {
        changeTimer->setSchedulingPriority(priority);
        if (scheduleForAbsoluteTime)
            scheduleClockEventAt(getClockTime() + duration - offset, changeTimer);
        else
            scheduleClockEventAfter(duration - offset, changeTimer);
    }
    offset = 0;
}

void SatNet3DPeriodicGate::processManagedChangeTimer()
{
    if (!managedChangeScheduled)
        return;
    managedChangeScheduled = false;
    scheduleChangeTimer();
    processChangeTimer();
}

void SatNet3DPeriodicGate::processChangeTimer()
{
    EV_TRACE << "Processing change timer" << EV_ENDL;
    if (isOpen_)
        close();
    else
        open();
}

void SatNet3DPeriodicGate::open()
{
    PacketGateBase::open();
    updateIsInGuardBand();
}

void SatNet3DPeriodicGate::close()
{
    PacketGateBase::close();
    updateIsInGuardBand();
}

bool SatNet3DPeriodicGate::canPacketFlowThrough(Packet *packet) const
{
    ASSERT(isOpen_);
    if (std::isnan(bitrate.get()))
        return PacketGateBase::canPacketFlowThrough(packet);
    else if (packet == nullptr)
        return false;
    else {
        if (enableImplicitGuardBand) {
            clocktime_t flowEndTime = getClockTime() + ((packet->getDataLength() + extraLength) / bitrate).get<s>() + SIMTIME_AS_CLOCKTIME(extraDuration);
            if (periodicGateManager != nullptr)
                return !managedChangeScheduled || flowEndTime <= nextChangeClockTime;
            else
                return !isScheduledClockEvent(changeTimer) || flowEndTime <= getArrivalClockTime(changeTimer);
        }
        else
            return PacketGateBase::canPacketFlowThrough(packet);
    }
}

void SatNet3DPeriodicGate::updateIsInGuardBand()
{
    bool newIsInGuardBand = false;
    if (isOpen_) {
        auto packet = provider != nullptr ? provider.canPullPacket() : nullptr;
        newIsInGuardBand = packet != nullptr && !canPacketFlowThrough(packet);
    }
    if (isInGuardBand_ != newIsInGuardBand) {
        isInGuardBand_ = newIsInGuardBand;
        EV_INFO << "Changing guard band state" << EV_FIELD(isInGuardBand_) << EV_ENDL;
        emit(guardBandStateChangedSignal, isInGuardBand_);
    }
}

void SatNet3DPeriodicGate::handleCanPushPacketChanged(const cGate *gate)
{
    PacketGateBase::handleCanPushPacketChanged(gate);
    updateIsInGuardBand();
}

void SatNet3DPeriodicGate::handleCanPullPacketChanged(const cGate *gate)
{
    PacketGateBase::handleCanPullPacketChanged(gate);
    updateIsInGuardBand();
}

} // namespace queueing
} // namespace satnet3d
