package satnet3d.app.reliable;

import inet.applications.contract.IApp;

simple ReliableEthernetSink like IApp
{
    parameters:
        string interfaceTableModule;
        string interface = default("eth0");
        string protocol = default(""); // empty accepts untagged traffic; use ieee8021qctag for tagged flows
        string localAddress = default("");
        string remoteAddress = default("");
        string coordinatorModule;
        int acceptedFlowId = default(-1); // -1 accepts all registered flows
        string lossModel = default("legacyBlock"); // none, legacyBlock, bernoulli
        double firstAttemptDropProbability = default(0.04);
        int legacyBlockSize = default(50);
        int legacyDropsPerBlock = default(2);
        int legacyFirstSequenceNumber = default(0);
        int legacyMaxSequenceNumber = default(-1); // -1 means that all subsequent sequence numbers are eligible
        bool legacyAllowDuplicateSelections = default(false); // false guarantees exactly legacyDropsPerBlock distinct selections per block
        int rngIndex = default(0);
        string recordFile = default("reliable_ethernet_sink.csv");
        bool appendFile = default(false);
        bool writeHeader = default(true);
        bool steal = default(false);
        double stopOperationExtraTime @unit(s) = default(-1s);
        double stopOperationTimeout @unit(s) = default(2s);
        @class(satnet3d::ReliableEthernetSink);
        @lifecycleSupport;
        @display("i=block/sink");
        @signal[packetReceived](type=inet::Packet);
        @signal[simulatedDrop](type=inet::Packet);
        @signal[duplicatePacket](type=inet::Packet);
        @signal[packetDelivered](type=inet::Packet);
        @signal[reliableEndToEndDelay](type=simtime_t);
        @signal[attemptNetworkDelay](type=simtime_t);
        @signal[deliveryAttempt](type=long);
        @statistic[packetReceived](source=packetReceived; record=count,"sum(packetBytes)","vector(packetBytes)"; interpolationmode=none);
        @statistic[simulatedDrop](source=simulatedDrop; record=count; interpolationmode=none);
        @statistic[duplicatePacket](source=duplicatePacket; record=count; interpolationmode=none);
        @statistic[packetDelivered](source=packetDelivered; record=count,"sum(packetBytes)"; interpolationmode=none);
        @statistic[reliableEndToEndDelay](source=reliableEndToEndDelay; unit=s; record=histogram,vector; interpolationmode=none);
        @statistic[attemptNetworkDelay](source=attemptNetworkDelay; unit=s; record=histogram,vector; interpolationmode=none);
        @statistic[deliveryAttempt](source=deliveryAttempt; record=histogram,vector; interpolationmode=none);
    gates:
        input socketIn;
        output socketOut;
}
