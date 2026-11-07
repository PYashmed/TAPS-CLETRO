#include "SatNet3DExtEthernetSocket.h"

#ifdef __linux__
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/net_tstamp.h>
#include <netinet/ether.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/uio.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <ctime>

#include "inet/common/ProtocolTag_m.h"
#include "inet/common/checksum/Checksum.h"
#include "inet/common/packet/Packet.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/networklayer/common/NetworkInterface.h"

using namespace inet;
using namespace omnetpp;

namespace satnet3d {

namespace {
struct FrameTimestamp {
    std::string key;
    int64_t txNs = 0;
    int64_t rxNs = 0;
};
std::deque<FrameTimestamp> timestampTable;
constexpr size_t maxTimestampEntries = 100000;
int64_t timestampMatched = 0;
int64_t timestampUnmatched = 0;
}

Define_Module(SatNet3DExtEthernetSocket);

std::string SatNet3DExtEthernetSocket::makeFrameKey(const uint8_t *frame, int length) const
{
    // The complete Ethernet payload is deliberately used in the first version:
    // it distinguishes repeated UDP frames while remaining independent of INET tags.
    std::ostringstream out;
    int keyLength = std::min(length, 4096);
    for (int i = 0; i < keyLength; ++i)
        out << std::hex << std::setw(2) << std::setfill('0') << int(frame[i]);
    return out.str();
}

void SatNet3DExtEthernetSocket::rememberTransmit(const std::string& key)
{
    while (timestampTable.size() >= maxTimestampEntries)
        timestampTable.pop_front();
    timestampTable.push_back({key, 0, 0});
}

void SatNet3DExtEthernetSocket::recordReceive(const std::string& key, int64_t rxNs)
{
    for (auto& entry : timestampTable) {
        if (entry.key == key && entry.rxNs == 0) {
            entry.rxNs = rxNs;
            ++timestampMatched;
            EV_INFO << "[ExtLower TIMESTAMP MATCH] " << getFullPath()
                    << " txNs=" << entry.txNs << " rxNs=" << entry.rxNs
                    << " transitNs=" << (entry.txNs > 0 ? entry.rxNs - entry.txNs : -1) << "\n";
            return;
        }
    }
    ++timestampUnmatched;
}

void SatNet3DExtEthernetSocket::drainTransmitTimestamps()
{
#ifdef __linux__
    if (fd == INVALID_SOCKET)
        return;
    uint8_t payload[1 << 16];
    char control[CMSG_SPACE(sizeof(timespec) * 3) + 64];
    for (;;) {
        iovec iov{payload, sizeof(payload)};
        msghdr message{};
        message.msg_iov = &iov;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        int n = ::recvmsg(fd, &message, MSG_ERRQUEUE | MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            EV_WARN << "[ExtLower TX TIMESTAMP] error queue read failed on " << device
                    << ": " << strerror(errno) << "\n";
            return;
        }
        int64_t txNs = 0;
        for (cmsghdr *controlMessage = CMSG_FIRSTHDR(&message); controlMessage != nullptr;
             controlMessage = CMSG_NXTHDR(&message, controlMessage)) {
            if (controlMessage->cmsg_level == SOL_SOCKET &&
                controlMessage->cmsg_type == SCM_TIMESTAMPING &&
                controlMessage->cmsg_len >= CMSG_LEN(sizeof(timespec) * 3)) {
                auto *stamps = reinterpret_cast<const timespec *>(CMSG_DATA(controlMessage));
                if (stamps[0].tv_sec != 0 || stamps[0].tv_nsec != 0)
                    txNs = int64_t(stamps[0].tv_sec) * 1000000000LL + stamps[0].tv_nsec;
            }
        }
        if (txNs == 0 || n <= 0)
            continue;
        std::string key = makeFrameKey(payload, n);
        for (auto it = timestampTable.begin(); it != timestampTable.end(); ++it) {
            if (it->key == key && it->txNs == 0) {
                it->txNs = txNs;
                EV_INFO << "[ExtLower TX TIMESTAMP] " << getFullPath()
                        << " ns=" << txNs << " bytes=" << n << "\n";
                break;
            }
        }
    }
#endif
}

void SatNet3DExtEthernetSocket::enqueueReceived(Packet *packet, int64_t rxNs)
{
    simtime_t releaseTime = simTime();
    if (rxNs > 0) {
        if (pendingPackets.empty()) {
            batchBaseRxNs = rxNs;
            batchBaseSimTime = simTime();
            lastReleaseTime = simTime();
        }
        int64_t deltaNs = std::max<int64_t>(0, rxNs - batchBaseRxNs);
        releaseTime = batchBaseSimTime + SimTime(deltaNs, SIMTIME_NS);
        if (releaseTime < lastReleaseTime)
            releaseTime = lastReleaseTime;
        lastReleaseTime = releaseTime;
    }
    if (externalRxQueueLimit <= 0)
        throw cRuntimeError("externalRxQueueLimit must be positive");
    if (static_cast<int>(pendingPackets.size()) >= externalRxQueueLimit) {
        ++timingQueueDrops;
        EV_WARN << "[ExtLower RX QUEUE DROP] " << getFullPath()
                << " queueLimit=" << externalRxQueueLimit << "\n";
        delete packet;
        return;
    }
    pendingPackets.push_back({packet, releaseTime});
    if (!releaseTimer->isScheduled()) {
        simtime_t nextTime = std::max(releaseTime, simTime());
        scheduleAt(nextTime, releaseTimer);
    }
}

void SatNet3DExtEthernetSocket::releaseDuePackets()
{
    while (!pendingPackets.empty() && pendingPackets.front().releaseTime <= simTime()) {
        Packet *packet = pendingPackets.front().packet;
        pendingPackets.pop_front();
        emit(packetReceivedSignal, packet);
        numReceived++;
        EV_INFO << "[ExtLower RX RELEASE] " << getFullPath()
                << " packet=" << packet->getName() << " simTime=" << simTime() << "\n";
        send(packet, "upperLayerOut");
        emit(packetSentToUpperSignal, packet);
    }
    if (!pendingPackets.empty())
        scheduleAt(pendingPackets.front().releaseTime, releaseTimer);
    else {
        batchBaseRxNs = 0;
        lastReleaseTime = simTime();
    }
}

void SatNet3DExtEthernetSocket::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL) {
        // INET's base class unconditionally requires RealTimeScheduler at the
        // local stage. Most instances remain simulated, so defer that check
        // until a non-empty device is actually opened.
        SimpleModule::initialize(stage);
        device = par("device");
        packetNameFormat = par("packetNameFormat");
        rtScheduler = dynamic_cast<RealTimeScheduler *>(getSimulation()->getScheduler());
        numSent = numReceived = 0;
        WATCH(numSent);
        WATCH(numReceived);
        readFilterParameters();
        preserveExternalRxTiming = par("preserveExternalRxTiming");
        externalRxQueueLimit = par("externalRxQueueLimit");
        releaseTimer = new cMessage("external-release-timer");
        WATCH(timingQueueDrops);
    }
    else
        ExtEthernetSocket::initialize(stage);
}

void SatNet3DExtEthernetSocket::handleMessage(cMessage *message)
{
    if (message == releaseTimer) {
        releaseDuePackets();
        return;
    }
    // Inspect and remember the exact serialized frame before the base class
    // sends it. The kernel TX timestamp is collected asynchronously later.
    auto *packet = check_and_cast<Packet *>(message);
    if (device != nullptr && device[0] != '\0') {
        const auto frameBytes = packet->peekAllAsBytes();
        const auto& frame = frameBytes->getBytes();
        const auto length = frame.size();
        const uint16_t etherType = length >= 14 ? (uint16_t(frame[12]) << 8) | frame[13] : 0;
        const bool vlanTagged = (etherType == 0x8100 || etherType == 0x88a8) && length >= 18;
        const uint16_t vlanTci = vlanTagged ? (uint16_t(frame[14]) << 8) | frame[15] : 0;
        EV_INFO << "[ExtLower TX HEADER] " << getFullPath() << " device=" << device
                << " etherType=0x" << std::hex << etherType << std::dec
                << " vlan=" << (vlanTagged ? int(vlanTci & 0x0fff) : -1)
                << " pcp=" << (vlanTagged ? int(vlanTci >> 13) : -1) << "\n";
        rememberTransmit(makeFrameKey(frame.data(), static_cast<int>(frame.size())));
    }
    ExtEthernetSocket::handleMessage(message);
    drainTransmitTimestamps();
}

void SatNet3DExtEthernetSocket::readFilterParameters()
{
        promiscuousMode = par("promiscuousMode");
        acceptedMacPrefix.clear();
        std::string text = par("acceptedMacPrefix").stdstringValue();
        text.erase(std::remove_if(text.begin(), text.end(), [](char c) {
            return c == ':' || c == '-' || std::isspace(static_cast<unsigned char>(c));
        }), text.end());
        if (text.size() % 2 != 0 || text.size() > 12)
            throw cRuntimeError("acceptedMacPrefix must contain 0 to 6 hexadecimal bytes");
        for (size_t i = 0; i < text.size(); i += 2) {
            unsigned int value;
            std::istringstream input(text.substr(i, 2));
            input >> std::hex >> value;
            if (input.fail() || !input.eof())
                throw cRuntimeError("Invalid acceptedMacPrefix: %s", text.c_str());
            acceptedMacPrefix.push_back(static_cast<uint8_t>(value));
        }
}

void SatNet3DExtEthernetSocket::openSocket()
{
    // Bindings are installed after dynamic topology creation; use the values
    // in force immediately before INET opens the raw socket.
    device = par("device");
    packetNameFormat = par("packetNameFormat");
    readFilterParameters();
    if (device != nullptr && device[0] != '\0' && rtScheduler == nullptr)
        rtScheduler = check_and_cast<RealTimeScheduler *>(getSimulation()->getScheduler());
    ExtEthernetSocket::openSocket();
#ifdef __linux__
    if (fd != INVALID_SOCKET) {
        // A NIC may strip an 802.1Q header before AF_PACKET receives the
        // frame. Preserve the original VLAN ID and PCP using packet auxdata.
        int enabled = 1;
        if (setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &enabled, sizeof(enabled)) < 0)
            throw cRuntimeError("Cannot enable VLAN packet auxdata on '%s': %s", device, strerror(errno));
    }
    if (fd != INVALID_SOCKET) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0)
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int timestampEnabled = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPNS, &timestampEnabled, sizeof(timestampEnabled)) < 0)
            EV_WARN << "[ExtLower TIMESTAMP] cannot enable SO_TIMESTAMPNS on " << device << ": " << strerror(errno) << "\n";
        int timestampingFlags = SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
        if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &timestampingFlags, sizeof(timestampingFlags)) < 0)
            EV_WARN << "[ExtLower TIMESTAMP] cannot enable SO_TIMESTAMPING on " << device << ": " << strerror(errno) << "\n";
    }
    if (promiscuousMode && fd != INVALID_SOCKET) {
        struct packet_mreq membership;
        std::memset(&membership, 0, sizeof(membership));
        membership.mr_ifindex = ifindex;
        membership.mr_type = PACKET_MR_PROMISC;
        if (setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &membership, sizeof(membership)) < 0)
            throw cRuntimeError("Cannot enable promiscuous packet membership on '%s': %s", device, strerror(errno));
        EV_INFO << "[ExtLower PROMISC] " << getFullPath() << " device=" << device << " enabled\n";
    }
#endif
}

void SatNet3DExtEthernetSocket::finish()
{
    if (releaseTimer != nullptr) {
        cancelAndDelete(releaseTimer);
        releaseTimer = nullptr;
    }
    for (auto& pending : pendingPackets)
        delete pending.packet;
    pendingPackets.clear();
    drainTransmitTimestamps();
    EV_INFO << "[ExtLower TIMESTAMP SUMMARY] " << getFullPath()
            << " matched=" << timestampMatched
            << " unmatchedRx=" << timestampUnmatched
            << " pending=" << timestampTable.size()
            << " timingQueueDrops=" << timingQueueDrops << "\n";
    if (device != nullptr && device[0] != '\0')
        ExtEthernetSocket::finish();
    else
        SimpleModule::finish();
}

bool SatNet3DExtEthernetSocket::matchesAcceptedMacPrefix(const uint8_t *frame, int length) const
{
    if (acceptedMacPrefix.empty())
        return true;
    if (length < 12)
        return false;
    bool destinationMatches = std::equal(acceptedMacPrefix.begin(), acceptedMacPrefix.end(), frame);
    bool sourceMatches = std::equal(acceptedMacPrefix.begin(), acceptedMacPrefix.end(), frame + 6);
    return destinationMatches || sourceMatches;
}

bool SatNet3DExtEthernetSocket::pollExternal(int maxPackets)
{
#ifndef __linux__
    return notify(fd);
#else
    bool inserted = false;
    for (int i = 0; i < maxPackets; ++i) {
        bool got = notify(fd);
        inserted = got || inserted;
        if (!got)
            break;
    }
    return inserted;
#endif
}

bool SatNet3DExtEthernetSocket::notify(int callbackFd)
{
#ifndef __linux__
    return ExtEthernetSocket::notify(callbackFd);
#else
    Enter_Method("notify");
    ASSERT(fd == callbackFd);
    drainTransmitTimestamps();
    uint8_t buffer[1 << 16] = {};
    // Leave space for a stripped VLAN header and the simulated Ethernet FCS.
    struct iovec iov = {buffer, sizeof(buffer) - 8};
    union {
        struct cmsghdr align;
        char bytes[CMSG_SPACE(sizeof(struct tpacket_auxdata)) + CMSG_SPACE(sizeof(timespec)) + 64];
    } ancillary = {};
    struct msghdr message = {};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = ancillary.bytes;
    message.msg_controllen = sizeof(ancillary.bytes);
    int n = ::recvmsg(fd, &message, MSG_DONTWAIT);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return false;
        throw cRuntimeError("Calling recvmsg on '%s' failed: %s", device, strerror(errno));
    }
    if (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
        EV_WARN << "[ExtLower RX DROP] " << getFullPath() << " device=" << device
                << " truncated frame or VLAN metadata\n";
        return true;
    }
    int64_t rxTimestampNs = 0;
    if (n >= 14) {
        for (struct cmsghdr *control = CMSG_FIRSTHDR(&message); control != nullptr;
             control = CMSG_NXTHDR(&message, control)) {
            if (control->cmsg_level == SOL_SOCKET && control->cmsg_type == SCM_TIMESTAMPNS &&
                control->cmsg_len >= CMSG_LEN(sizeof(timespec))) {
                const auto *stamp = reinterpret_cast<const timespec *>(CMSG_DATA(control));
                rxTimestampNs = int64_t(stamp->tv_sec) * 1000000000LL + stamp->tv_nsec;
                EV_INFO << "[ExtLower RX TIMESTAMP] " << getFullPath() << " device=" << device
                        << " ns=" << rxTimestampNs << "\n";
                continue;
            }
            if (control->cmsg_level != SOL_PACKET || control->cmsg_type != PACKET_AUXDATA ||
                control->cmsg_len < CMSG_LEN(sizeof(struct tpacket_auxdata)))
                continue;
            const auto *aux = reinterpret_cast<const struct tpacket_auxdata *>(CMSG_DATA(control));
            if (!(aux->tp_status & TP_STATUS_VLAN_VALID))
                continue;
            const uint16_t existingType = (uint16_t(buffer[12]) << 8) | buffer[13];
            if (existingType == 0x8100 || existingType == 0x88a8)
                break; // The driver retained the tag in the frame already.
            const uint16_t tpid = (aux->tp_status & TP_STATUS_VLAN_TPID_VALID) ? aux->tp_vlan_tpid : 0x8100;
            const uint16_t tci = aux->tp_vlan_tci;
            std::memmove(buffer + 16, buffer + 12, n - 12);
            buffer[12] = tpid >> 8;
            buffer[13] = tpid & 0xff;
            buffer[14] = tci >> 8;
            buffer[15] = tci & 0xff;
            n += 4;
            EV_INFO << "[ExtLower RX VLAN RESTORE] " << getFullPath() << " device=" << device
                    << " vlan=" << (tci & 0x0fff) << " pcp=" << (tci >> 13) << "\n";
            break;
        }
    }
    if (!matchesAcceptedMacPrefix(buffer, n)) {
        EV_INFO << "[ExtLower FILTER DROP] " << getFullPath() << " device=" << device
                << " bytes=" << n << "\n";
        return true;
    }
    if (rxTimestampNs != 0)
        recordReceive(makeFrameKey(buffer, n), rxTimestampNs);
    const int wireLength = n;
    const uint16_t etherType = wireLength >= 14 ? (uint16_t(buffer[12]) << 8) | buffer[13] : 0;
    const bool vlanTagged = (etherType == 0x8100 || etherType == 0x88a8) && wireLength >= 18;
    const uint16_t vlanTci = vlanTagged ? (uint16_t(buffer[14]) << 8) | buffer[15] : 0;
    n = std::max(n, ETHER_MIN_LEN - 4);
    uint32_t checksum = htonl(ethernetFcs(buffer, n));
    std::memcpy(&buffer[n], &checksum, sizeof(checksum));
    auto data = makeShared<BytesChunk>(static_cast<const uint8_t *>(buffer), n + 4);
    auto packet = new Packet(nullptr, data);
    auto networkInterface = check_and_cast<NetworkInterface *>(getContainingNicModule(this));
    packet->addTag<InterfaceInd>()->setInterfaceId(networkInterface->getInterfaceId());
    packet->addTag<PacketProtocolTag>()->setProtocol(&Protocol::ethernetMac);
    packet->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ethernetMac);
    packet->setName(packetPrinter.printPacketToString(packet, packetNameFormat).c_str());
    EV_INFO << "[ExtLower RX QUEUE] " << getFullPath() << " device=" << device
            << " packet=" << packet->getName() << " bytes=" << packet->getDataLength()
            << " etherType=0x" << std::hex << etherType << std::dec
            << " vlan=" << (vlanTagged ? int(vlanTci & 0x0fff) : -1)
            << " pcp=" << (vlanTagged ? int(vlanTci >> 13) : -1)
            << " rxNs=" << rxTimestampNs << "\n";
    enqueueReceived(packet, preserveExternalRxTiming ? rxTimestampNs : 0);
    return true;
#endif
}

} // namespace satnet3d
