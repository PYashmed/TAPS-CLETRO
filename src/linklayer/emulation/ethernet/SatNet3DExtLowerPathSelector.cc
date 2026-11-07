#include "SatNet3DExtLowerPathSelector.h"

namespace satnet3d {

Define_Module(SatNet3DExtLowerPathSelector);

int SatNet3DExtLowerPathSelector::classifyPacket(inet::Packet *packet)
{
    (void)packet;
    return getParentModule()->par("externalEnabled").boolValue() ? 1 : 0;
}

} // namespace satnet3d
