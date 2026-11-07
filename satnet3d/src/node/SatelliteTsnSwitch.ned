package satnet3d.node;

import inet.mobility.contract.IMobility;
import inet.node.tsn.TsnSwitch;
import satnet3d.queueing.PeriodicGateManager;

module SatelliteTsnSwitch extends TsnSwitch
{
    parameters:
        @display("i=device/satellite");
        string osgModel = default("../../data/satellite.osgb.50000.scale.0,-90,0.rot");
        string osgModelColor = default("");
        string canvasImage = default("");
        string canvasImageColor = default("");
        numEthInterfaces = default(5);
        bridging.interfaceRelay.selector.typename = default("SatNet3DRelayInterfaceSelector");

    submodules:
        periodicGateManager: PeriodicGateManager {
            @display("p=125,230;is=s");
        }
        mobility: <default("CircularOrbitMobility")> like IMobility if typename != "" {
            @display("p=125,160;is=s");
            coordinateSystemModule = default("coordinateSystem");
            updateInterval = default(0.1s);
        }
}
