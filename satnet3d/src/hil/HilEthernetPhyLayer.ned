package satnet3d.hil;

import inet.physicallayer.wired.ethernet.IEthernetPhyLayer;
import inet.physicallayer.wired.ethernet.EthernetPhyLayer;
import inet.protocolelement.common.PacketDeserializer;
import inet.protocolelement.common.InterpacketGapInserter;
import inet.physicallayer.wired.ethernet.EthernetPhyHeaderInserter;
import inet.queueing.common.PacketMultiplexer;
import inet.queueing.common.PacketPushToSend;
import satnet3d.linklayer.emulation.ethernet.SatNet3DExtLowerPathSelector;

// Keep LayeredEthernetInterface and macLayer.queue intact. Only the PHY path
// changes, so the existing offline gate lookup and CBS configuration still work.
module HilEthernetPhyLayer like IEthernetPhyLayer
{
    parameters:
        double bitrate @unit(bps);
        bool externalEnabled = default(true);
        string device = default("");
        string networkNamespace = default("");
        bool promiscuousMode = default(true);
        string acceptedMacPrefix = default("02-58-57");
        bool emulatePropagationDelay = default(true);
        bool enableShadowGuard = default(false);
        string shadowSatellites = default("");
        *.bitrate = default(this.bitrate);
        @display("i=block/layer");
    gates:
        input upperLayerIn;
        output upperLayerOut;
        input lowerLayerIn;
        output lowerLayerOut;
    submodules:
        shadowGuard: HilShadowTrafficGuard if enableShadowGuard {
            shadowSatellites = parent.shadowSatellites;
            @display("p=100,360");
        }
        simulatedPhy: EthernetPhyLayer {
            @display("p=250,240");
        }
        pathSelector: SatNet3DExtLowerPathSelector {
            @display("p=250,80");
        }
        externalPhyHeaderInserter: EthernetPhyHeaderInserter {
            @display("p=330,80");
        }
        externalInterFrameGapInserter: InterpacketGapInserter {
            duration = 96s / dropUnit(parent.bitrate);
            @display("p=380,80");
        }
        externalTransmitter: HilPacketTransmitter {
            datarate = parent.bitrate;
            @display("p=420,80");
        }
        transmitDelay: HilPropagationDelayer {
            enabled = parent.emulatePropagationDelay;
            @display("p=430,80");
        }
        externalAdapter: PacketPushToSend {
            @display("p=570,80");
        }
        socket: HilEthernetSocket {
            device = parent.externalEnabled ? parent.device : "";
            namespace = parent.networkNamespace;
            promiscuousMode = parent.promiscuousMode;
            acceptedMacPrefix = parent.acceptedMacPrefix;
            preserveExternalRxTiming = true;
            @display("p=720,160");
        }
        receiveDelay: HilPropagationDelayer {
            enabled = parent.emulatePropagationDelay;
            @display("p=720,270");
        }
        deserializer: PacketDeserializer {
            @display("p=570,270");
        }
        ingressMultiplexer: PacketMultiplexer {
            @display("p=430,360");
        }
    connections:
        upperLayerIn --> pathSelector.in;
        pathSelector.out++ --> simulatedPhy.upperLayerIn;
        pathSelector.out++ --> externalPhyHeaderInserter.in;
        externalPhyHeaderInserter.out --> externalInterFrameGapInserter.in;
        externalInterFrameGapInserter.out --> externalTransmitter.in;
        externalTransmitter.out --> transmitDelay.in;
        transmitDelay.out --> externalAdapter.in;
        externalAdapter.out --> socket.upperLayerIn;
        socket.upperLayerOut --> receiveDelay.in;
        receiveDelay.out --> deserializer.in;
        deserializer.out --> ingressMultiplexer.in++;
        simulatedPhy.upperLayerOut --> ingressMultiplexer.in++;
        ingressMultiplexer.out --> upperLayerOut;
        lowerLayerIn --> simulatedPhy.lowerLayerIn;
        simulatedPhy.lowerLayerOut --> lowerLayerOut;
}
