package satnet3d.simulations.xw;

import inet.visualizer.common.IntegratedVisualizer;
import satnet3d.app.reliable.ReliableFlowCoordinator;
import satnet3d.common.AdaptiveGeographicCoordinateSystem;
import satnet3d.mobility.MobilityManager;
import satnet3d.mobility.InitialConstellationRecorder;
import satnet3d.node.EarthTsnDevice;
import satnet3d.offline.MatlabOfflineScheduleManager;
import satnet3d.scenario.TopologyCoordinator;
import satnet3d.scenario.TopologyManager;
import satnet3d.scenario.WalkerConstellationManager;
import satnet3d.wireless.DynamicChannelManager;
import satnet3d.wireless.LinkDelayRecorder;

// XW migration scenario. The complete Walker-Delta constellation is created
// dynamically; every endpoint owns one GSL connected directly to a satellite.
network Xw
{
    parameters:
        int numStations = default(22);
        bool enableVisualizer = default(false);
        bool enableMatlabOfflineManager = default(false);
        bool enableLinkDelayRecorder = default(false);
        bool enableInitialConstellationRecorder = default(false);

    submodules:
        visualizer: IntegratedVisualizer if enableVisualizer {
            @display("p=80,60");
        }
        coordinateSystem: AdaptiveGeographicCoordinateSystem {
            @display("p=80,130");
        }
        mobilityManager: MobilityManager {
            @display("p=80,200;is=s");
        }
        dynamicChannelManager: DynamicChannelManager {
            @display("p=80,270;is=s");
        }
        linkDelayRecorder: LinkDelayRecorder if enableLinkDelayRecorder {
            networkModule = "^";
            @display("p=80,340;is=s");
        }
        initialConstellationRecorder: InitialConstellationRecorder if enableInitialConstellationRecorder {
            networkModule = "^";
            coordinateSystemModule = "^.coordinateSystem";
            walkerManagerModule = "^.walkerDeltaScenarioManager";
            @display("p=80,410;is=s");
        }
        topologyCoordinator: TopologyCoordinator {
            networkModule = "^";
            networkStateManagerModule = "";
            @display("p=230,200;is=s");
        }
        reliableFlowCoordinator: ReliableFlowCoordinator {
            @display("p=230,270;is=s");
        }
        matlabOfflineManager: MatlabOfflineScheduleManager if enableMatlabOfflineManager {
            networkModule = "^";
            @display("p=230,340;is=s");
        }
        stations[numStations]: EarthTsnDevice {
            @display("p=760,80");
            mobility.altitude = default(0m);
        }
        walkerDeltaScenarioManager: WalkerConstellationManager {
            targetNetworkModule = "^";
            satelliteModuleType = "satnet3d.node.SatelliteTsnSwitch";
            topologyCoordinatorModule = "^.topologyCoordinator";
            propagationSpeed = default(299792458mps);
            @display("p=420,80;is=s");
        }
        groundToSatTopologyManager: TopologyManager {
            primaryLayerModule = "^";
            primaryNodeTypePattern = "satnet3d.node.EarthTsnDevice";
            primaryNodeNamePattern = "stations*";
            peerLayerModule = "^";
            peerNodeTypePattern = "satnet3d.node.SatelliteTsnSwitch";
            peerNodeNamePattern = "satellite*";
            maxLinksPerPrimaryNode = 1;
            keepExistingLinks = true;
            initialUpdateOffset = 0s;
            updateInterval = 60s;
            primaryFirstGateIndex = 0;
            primaryNumGateInterfaces = 1;
            peerFirstGateIndex = default(4);
            peerNumGateInterfaces = default(3);
            // This manager creates its own 1 Gbit/s GSL channels.
            topologyCoordinatorModule = "";
            datarate = 1Gbps;
            propagationSpeed = 299792458mps;
            @display("p=420,160;is=s");
        }

    connections allowunconnected:
}
