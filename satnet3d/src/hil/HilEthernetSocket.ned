package satnet3d.hil;

import satnet3d.linklayer.emulation.ethernet.SatNet3DExtEthernetSocket;

// AF_PACKET supplies frame bytes without a software FCS; the NIC adds its own.
simple HilEthernetSocket extends SatNet3DExtEthernetSocket
{
    parameters:
        @class(satnet3d::hil::HilEthernetSocket);
}
