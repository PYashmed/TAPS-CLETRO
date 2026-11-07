package satnet3d.hil;

import inet.queueing.common.PacketDelayer;

// Read the current delay of the retained logical link on every packet.
simple HilPropagationDelayer extends PacketDelayer
{
    parameters:
        bool enabled = default(true);
        delay = 0s;
        @class(satnet3d::hil::HilPropagationDelayer);
}
