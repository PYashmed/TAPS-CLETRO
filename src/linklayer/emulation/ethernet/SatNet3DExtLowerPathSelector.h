#ifndef __SATNET3D_EXTLOWERPATHSELECTOR_H
#define __SATNET3D_EXTLOWERPATHSELECTOR_H

#include "inet/queueing/base/PacketClassifierBase.h"

namespace satnet3d {

class SatNet3DExtLowerPathSelector : public inet::queueing::PacketClassifierBase
{
  protected:
    virtual int classifyPacket(inet::Packet *packet) override;
};

} // namespace satnet3d

#endif
