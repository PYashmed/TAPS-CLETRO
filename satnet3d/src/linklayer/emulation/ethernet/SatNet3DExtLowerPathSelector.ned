package satnet3d.linklayer.emulation.ethernet;

import inet.queueing.base.PacketClassifierBase;
import inet.queueing.contract.IPacketClassifier;

// Output 0 is the simulated Ethernet path and output 1 is the host socket.
// The parent interface's mutable externalEnabled parameter selects the path.
simple SatNet3DExtLowerPathSelector extends PacketClassifierBase like IPacketClassifier
{
    parameters:
        @class(satnet3d::SatNet3DExtLowerPathSelector);
}
