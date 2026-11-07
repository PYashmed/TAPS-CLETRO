#ifndef __SATNET3D_RELAYINTERFACESELECTOR_H
#define __SATNET3D_RELAYINTERFACESELECTOR_H

#include "inet/linklayer/ethernet/common/RelayInterfaceSelector.h"

using namespace inet;

namespace satnet3d {

class SatNet3DRelayInterfaceSelector : public RelayInterfaceSelector
{
  protected:
    virtual bool isForwardingInterface(NetworkInterface *networkInterface) const override;
    virtual void pushPacket(Packet *packet, const cGate *gates) override;
};

} // namespace satnet3d

#endif
