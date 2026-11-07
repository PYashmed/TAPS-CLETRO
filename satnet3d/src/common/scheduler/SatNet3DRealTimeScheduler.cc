#include "SatNet3DRealTimeScheduler.h"
#include "SatNet3DExternalPollable.h"

#ifdef __linux__
#include <poll.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <iostream>

using namespace inet;

namespace satnet3d {

Register_Class(SatNet3DRealTimeScheduler);

void SatNet3DRealTimeScheduler::startRun()
{
    RealTimeScheduler::startRun();
    lastBehindPollNs = 0;
    if (const char *value = std::getenv("SATNET3D_BEHIND_POLL_US")) {
        char *end = nullptr;
        long long us = std::strtoll(value, &end, 10);
        if (end != value && *end == '\0' && us > 0)
            behindPollIntervalNs = us * 1000;
    }
    if (const char *value = std::getenv("SATNET3D_EXTERNAL_BATCH_SIZE")) {
        char *end = nullptr;
        long batch = std::strtol(value, &end, 10);
        if (end != value && *end == '\0' && batch > 0)
            maxBatchSize = static_cast<int>(batch);
    }
    EV_INFO << "[SatNet3D RT] behindPollInterval=" << behindPollIntervalNs
            << " ns, maxBatchSize=" << maxBatchSize << "\n";
}

void SatNet3DRealTimeScheduler::setBehindPollInterval(int64_t nanoseconds)
{
    if (nanoseconds <= 0)
        throw omnetpp::cRuntimeError("behind poll interval must be positive");
    behindPollIntervalNs = nanoseconds;
}

void SatNet3DRealTimeScheduler::setMaxBatchSize(int packets)
{
    if (packets <= 0)
        throw omnetpp::cRuntimeError("external batch size must be positive");
    maxBatchSize = packets;
}

bool SatNet3DRealTimeScheduler::receiveWithTimeout(int64_t timeout)
{
#ifdef __linux__
    std::vector<Entry> entries = callbackEntries;
    if (entries.empty()) {
        if (timeout > 0) {
            timespec ts{timeout / 1000000000, timeout % 1000000000};
            nanosleep(&ts, nullptr);
        }
        return false;
    }
    std::vector<pollfd> fds;
    fds.reserve(entries.size());
    for (const auto& entry : entries)
        fds.push_back({entry.fd, POLLIN | POLLERR, 0});
    int timeoutMs = timeout <= 0 ? 0 : static_cast<int>(std::min<int64_t>((timeout + 999999) / 1000000, INT32_MAX));
    int result;
    do { result = ::poll(fds.data(), fds.size(), timeoutMs); } while (result < 0 && errno == EINTR);
    if (result < 0)
        throw omnetpp::cRuntimeError("poll() failed: %s", strerror(errno));
    advanceSimTime();
    bool inserted = false;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!(fds[i].revents & (POLLIN | POLLERR)))
            continue;
        if (auto *batch = dynamic_cast<SatNet3DExternalPollable *>(entries[i].callback))
            inserted = batch->pollExternal(maxBatchSize) || inserted;
        else
            inserted = entries[i].callback->notify(entries[i].fd) || inserted;
    }
    return inserted;
#else
    return RealTimeScheduler::receiveWithTimeout(timeout);
#endif
}

bool SatNet3DRealTimeScheduler::pollBehind()
{
    int64_t now = opp_get_monotonic_clock_nsecs();
    if (lastBehindPollNs != 0 && now - lastBehindPollNs < behindPollIntervalNs)
        return false;
    lastBehindPollNs = now;
    return receiveWithTimeout(0);
}

omnetpp::cEvent *SatNet3DRealTimeScheduler::takeNextEvent()
{
    while (true) {
        int64_t targetTime;
        omnetpp::cEvent *event = sim->getFES()->peekFirst();
        if (!event)
            targetTime = INT64_MAX;
        else
            targetTime = baseTime + event->getArrivalTime().inUnit(SIMTIME_NS);
        int64_t curTime = opp_get_monotonic_clock_nsecs();
        if (targetTime > curTime) {
            switch (receiveUntil(targetTime)) {
                case -1: return nullptr;
                case 1: event = sim->getFES()->peekFirst(); break;
                default: break;
            }
        }
        else {
            // Unlike INET's base implementation, do not skip external input
            // when the simulation is behind wall clock time.
            if (pollBehind())
                event = sim->getFES()->peekFirst();
        }
        if (!event)
            continue;
        omnetpp::cEvent *tmp = sim->getFES()->removeFirst();
        ASSERT(tmp == event);
        if (tmp->isStale()) { delete tmp; continue; }
        return event;
    }
}

} // namespace satnet3d
