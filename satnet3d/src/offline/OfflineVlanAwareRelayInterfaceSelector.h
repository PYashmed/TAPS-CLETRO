#ifndef __SATNET3D_OFFLINEVLANAWARERELAYINTERFACESELECTOR_H
#define __SATNET3D_OFFLINEVLANAWARERELAYINTERFACESELECTOR_H

#include "../linklayer/ethernet/common/SatNet3DRelayInterfaceSelector.h"

namespace satnet3d {
namespace offline {

class OfflineVlanAwareRelayInterfaceSelector : public SatNet3DRelayInterfaceSelector
{
  protected:
    virtual void pushPacket(inet::Packet *packet, const omnetpp::cGate *gate) override;
    unsigned int getVlanId(const inet::Packet *packet) const;
};

} // namespace offline
} // namespace satnet3d

#endif
