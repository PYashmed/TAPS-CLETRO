package satnet3d.scenario;

import inet.common.scenario.ScenarioManager;

simple TopologyManager extends ScenarioManager
{
    parameters:
        @class(satnet3d::TopologyManager);

        bool enabled = default(true);
        bool autoMaintain = default(true);
        double initialUpdateOffset @unit(s) = default(0s);
        double updateInterval @unit(s) = default(1s);
        int updateSchedulingPriority = default(0);

        string primaryLayerModule = default("^");
        string primaryNodeTypePattern = default("*");
        string primaryNodeNamePattern = default("*");
        string peerLayerModule = default("^");
        string peerNodeTypePattern = default("*");
        string peerNodeNamePattern = default("*");
        string mobilitySubmoduleName = default("mobility");

        int maxLinksPerPrimaryNode = default(1);
        double maxRange @unit(m) = default(-1m);
        double handoverMargin @unit(m) = default(0m);
        bool keepExistingLinks = default(true);

        string linkGateName = default("ethg");
        int primaryFirstGateIndex = default(0);
        int primaryNumGateInterfaces = default(-1);
        string topologyCoordinatorModule = default("");
        int peerFirstGateIndex = default(0);
        int peerNumGateInterfaces = default(-1);
        string interLayerChannelType = default("satnet3d.wireless.DynamicChannel");
        double datarate @unit(bps) = default(1Gbps);
        double propagationSpeed @unit(mps) = default(299792458mps);
        string linkColor = default("cyan");

        @display("i=block/cogwheel");
}