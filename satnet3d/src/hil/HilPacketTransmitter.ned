package satnet3d.hil;

import inet.protocolelement.transceiver.PacketTransmitter;

// Preserve standard busy time and transmission signals (including CBS), but
// deliver a MAC frame to AF_PACKET at transmission start instead of a Signal.
simple HilPacketTransmitter extends PacketTransmitter
{
    parameters:
        @class(satnet3d::hil::HilPacketTransmitter);
}
