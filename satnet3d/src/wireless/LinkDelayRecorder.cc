#include "LinkDelayRecorder.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <tuple>

#include "DynamicChannel.h"

using namespace omnetpp;

namespace satnet3d {

Define_Module(LinkDelayRecorder);

LinkDelayRecorder::~LinkDelayRecorder()
{
    cancelAndDelete(sampleTimer);
}

void LinkDelayRecorder::initialize(int stage)
{
    if (stage == inet::INITSTAGE_LOCAL) {
        enabled = par("enabled").boolValue();
        sampleInterval = par("sampleInterval");
        startTime = par("startTime");
        stopTime = par("stopTime");
        schedulingPriority = par("schedulingPriority").intValue();
        appendFile = par("appendFile").boolValue();
        writeHeader = par("writeHeader").boolValue();
        flushEverySample = par("flushEverySample").boolValue();
        outputFile = par("outputFile").stringValue();

        if (sampleInterval <= SIMTIME_ZERO)
            throw cRuntimeError("sampleInterval must be positive");
        if (startTime < SIMTIME_ZERO)
            throw cRuntimeError("startTime must not be negative");
        if (stopTime >= SIMTIME_ZERO && stopTime <= startTime)
            throw cRuntimeError("stopTime must be greater than startTime, or negative");

        networkModule = getModuleByPath(par("networkModule").stringValue());
        if (networkModule == nullptr)
            throw cRuntimeError("networkModule not found");

        sampleTimer = new cMessage("link-delay-sample");
        sampleTimer->setSchedulingPriority(schedulingPriority);

        if (enabled) {
            std::ios::openmode mode = std::ios::out;
            mode |= appendFile ? std::ios::app : std::ios::trunc;
            outputStream.open(outputFile, mode);
            if (!outputStream.is_open())
                throw cRuntimeError("Cannot open link delay record file '%s'", outputFile.c_str());
            outputStream << std::fixed << std::setprecision(12);
            if (writeHeader && !appendFile)
                outputStream << "simTime,channelId,linkType,sourceNode,sourceInterface,"
                                "destinationNode,destinationInterface,delay,disabled\n";
        }
    }
    else if (stage == inet::INITSTAGE_LAST && enabled) {
        nextSampleTime = startTime;
        scheduleAt(nextSampleTime, sampleTimer);
    }
}

void LinkDelayRecorder::handleMessage(cMessage *message)
{
    if (message != sampleTimer)
        throw cRuntimeError("Unexpected message '%s'", message->getName());

    recordSnapshot();
    nextSampleTime += sampleInterval;
    scheduleNextSample();
}

void LinkDelayRecorder::finish()
{
    if (sampleTimer != nullptr && sampleTimer->isScheduled())
        cancelEvent(sampleTimer);
    if (outputStream.is_open()) {
        outputStream.flush();
        outputStream.close();
    }

    recordScalar("sampleCount", sampleCount);
    recordScalar("recordCount", recordCount);
    recordScalar("lastLinkCount", lastLinkCount);
}

void LinkDelayRecorder::scheduleNextSample()
{
    if (stopTime >= SIMTIME_ZERO && nextSampleTime >= stopTime)
        return;
    sampleTimer->setSchedulingPriority(schedulingPriority);
    scheduleAt(nextSampleTime, sampleTimer);
}

void LinkDelayRecorder::collectLinks(cModule *module, std::set<DynamicChannel *>& seen,
                                     std::vector<LinkRecord>& links) const
{
    if (module == nullptr)
        return;

    if (module->hasGate("ethg")) {
        int interfaceCount = module->gateSize("ethg");
        for (int interfaceIndex = 0; interfaceIndex < interfaceCount; interfaceIndex++) {
            cGate *sourceGate = module->gateHalf("ethg", cGate::OUTPUT, interfaceIndex);
            if (sourceGate == nullptr)
                continue;
            auto *channel = dynamic_cast<DynamicChannel *>(sourceGate->getChannel());
            if (channel == nullptr || !seen.insert(channel).second)
                continue;
            cGate *destinationGate = sourceGate->getNextGate();
            cModule *destinationNode = destinationGate != nullptr ? destinationGate->getOwnerModule() : nullptr;
            if (destinationNode == nullptr)
                continue;

            LinkRecord record;
            record.channel = channel;
            record.sourceNode = module;
            record.destinationNode = destinationNode;
            record.sourceInterfaceIndex = interfaceIndex;
            record.destinationInterfaceIndex = destinationGate->getIndex();
            links.push_back(record);
        }
    }

    for (cModule::SubmoduleIterator iterator(module); !iterator.end(); ++iterator)
        collectLinks(*iterator, seen, links);
}

void LinkDelayRecorder::recordSnapshot()
{
    std::set<DynamicChannel *> seen;
    std::vector<LinkRecord> links;
    collectLinks(networkModule, seen, links);

    std::sort(links.begin(), links.end(), [](const LinkRecord& left, const LinkRecord& right) {
        return std::make_tuple(left.sourceNode->getId(), left.sourceInterfaceIndex,
                               left.destinationNode->getId(), left.destinationInterfaceIndex) <
               std::make_tuple(right.sourceNode->getId(), right.sourceInterfaceIndex,
                               right.destinationNode->getId(), right.destinationInterfaceIndex);
    });

    for (const auto& link : links) {
        outputStream << simTime().dbl() << ","
                     << link.channel->getId() << ","
                     << classifyLink(link.sourceNode, link.destinationNode) << ","
                     << link.sourceNode->getFullName() << ","
                     << interfaceName(link.sourceInterfaceIndex) << ","
                     << link.destinationNode->getFullName() << ","
                     << interfaceName(link.destinationInterfaceIndex) << ","
                     << link.channel->getDelay().dbl() << ","
                     << (link.channel->isDisabled() ? 1 : 0) << "\n";
    }

    sampleCount++;
    recordCount += links.size();
    lastLinkCount = links.size();
    if (flushEverySample)
        outputStream.flush();

    EV_INFO << "Recorded " << links.size() << " directed link delays at " << simTime() << endl;
}

std::string LinkDelayRecorder::interfaceName(int index)
{
    return index >= 0 ? "eth" + std::to_string(index) : "";
}

bool LinkDelayRecorder::isSatellite(const cModule *node)
{
    return node != nullptr && !strcmp(node->getName(), "satellite");
}

std::string LinkDelayRecorder::classifyLink(const cModule *sourceNode, const cModule *destinationNode)
{
    bool sourceSatellite = isSatellite(sourceNode);
    bool destinationSatellite = isSatellite(destinationNode);
    if (sourceSatellite && destinationSatellite)
        return "ISL";
    if (sourceSatellite || destinationSatellite)
        return "GSL";
    return "GROUND";
}

} // namespace satnet3d
