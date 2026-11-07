#ifndef __SATNET3D_WIRELESS_LINKDELAYRECORDER_H
#define __SATNET3D_WIRELESS_LINKDELAYRECORDER_H

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <omnetpp.h>

#include "inet/common/InitStages.h"

namespace satnet3d {

class DynamicChannel;

class LinkDelayRecorder : public omnetpp::cSimpleModule
{
  protected:
    struct LinkRecord {
        DynamicChannel *channel = nullptr;
        omnetpp::cModule *sourceNode = nullptr;
        omnetpp::cModule *destinationNode = nullptr;
        int sourceInterfaceIndex = -1;
        int destinationInterfaceIndex = -1;
    };

    omnetpp::cModule *networkModule = nullptr;
    omnetpp::cMessage *sampleTimer = nullptr;
    omnetpp::simtime_t sampleInterval;
    omnetpp::simtime_t startTime;
    omnetpp::simtime_t stopTime;
    omnetpp::simtime_t nextSampleTime;
    int schedulingPriority = -60;
    bool enabled = false;
    bool appendFile = false;
    bool writeHeader = true;
    bool flushEverySample = true;
    std::string outputFile;
    std::ofstream outputStream;
    long sampleCount = 0;
    long recordCount = 0;
    long lastLinkCount = 0;

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(omnetpp::cMessage *message) override;
    virtual void finish() override;

    void collectLinks(omnetpp::cModule *module, std::set<DynamicChannel *>& seen,
                      std::vector<LinkRecord>& links) const;
    void recordSnapshot();
    void scheduleNextSample();

    static std::string interfaceName(int index);
    static std::string classifyLink(const omnetpp::cModule *sourceNode,
                                    const omnetpp::cModule *destinationNode);
    static bool isSatellite(const omnetpp::cModule *node);

  public:
    virtual ~LinkDelayRecorder();
};

} // namespace satnet3d

#endif
