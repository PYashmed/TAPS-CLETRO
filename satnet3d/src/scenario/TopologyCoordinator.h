#ifndef __SATNET3D_SCENARIO_TOPOLOGYCOORDINATOR_H
#define __SATNET3D_SCENARIO_TOPOLOGYCOORDINATOR_H

#include <string>
#include <vector>

#include <omnetpp.h>

using namespace omnetpp;

namespace satnet3d {

class NetworkStateManager;

class TopologyCoordinator : public cSimpleModule
{
  public:
    enum class CommandType { CONNECT, DISCONNECT };

    struct LinkCommand {
        CommandType type = CommandType::CONNECT;
        std::string sourceNode;
        std::string sourceGate;
        std::string destinationNode;
        std::string destinationGate;
        std::string linkColor;
    };

    struct ApplyResult {
        int applied = 0;
        int noops = 0;
        int rejected = 0;
    };

  protected:
    cModule *networkModule = nullptr;
    std::string linkGateName;
    std::string channelTypeName;
    std::string datarateValue;
    std::string propagationSpeedValue;
    std::string linkColor;
    NetworkStateManager *networkStateManager = nullptr;

  protected:
    virtual void initialize() override;
    virtual cModule *findNode(const std::string& fullName) const;
    virtual bool parseGate(const std::string& gate, int& index) const;
    virtual bool isConnectedPair(cModule *source, int sourceIndex, cModule *destination, int destinationIndex) const;
    virtual bool connect(const LinkCommand& command, std::string& reason);
    virtual bool disconnect(const LinkCommand& command, std::string& reason);
    virtual cChannel *createChannel() const;
    virtual void initializeChannelsRecursively(cModule *module) const;

  public:
    virtual ApplyResult applyCommands(const std::vector<LinkCommand>& commands);
};

} // namespace satnet3d

#endif
