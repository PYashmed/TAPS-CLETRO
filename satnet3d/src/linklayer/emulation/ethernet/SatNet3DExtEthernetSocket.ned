package satnet3d.linklayer.emulation.ethernet;

import inet.emulation.linklayer.ethernet.ExtEthernetSocket;

simple SatNet3DExtEthernetSocket extends ExtEthernetSocket
{
    parameters:
        @class(satnet3d::SatNet3DExtEthernetSocket);
        device @mutable;
        bool promiscuousMode @mutable = default(false);
        string acceptedMacPrefix @mutable = default("");
        bool preserveExternalRxTiming @mutable = default(true);
        int externalRxQueueLimit @mutable = default(100000);
}
