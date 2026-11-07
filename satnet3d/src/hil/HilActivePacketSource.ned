package satnet3d.hil;

import satnet3d.source.ActivePacketSourceMultipleInterval;

// Same packet length and generation behavior; metadata occupies 36 payload bytes.
simple HilActivePacketSource extends ActivePacketSourceMultipleInterval
{
    parameters:
        @class(satnet3d::hil::HilActivePacketSource);
}
