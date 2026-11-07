package satnet3d.app;

import inet.applications.contract.IApp;

// Raw-Ethernet receiver that records one CSV row for every delivered frame.
simple RecordFileEthernetSink like IApp
{
    parameters:
        string interfaceTableModule;
        string interface = default("eth0");
        string protocol = default("ieee8021qctag");
        string localAddress = default("");
        string remoteAddress = default("");
        string recordFile = default("record_file_ethernet_sink.csv");
        bool appendFile = default(false);
        bool writeHeader = default(true);
        bool steal = default(false);
        double stopOperationExtraTime @unit(s) = default(-1s);
        double stopOperationTimeout @unit(s) = default(2s);
        @class(satnet3d::RecordFileEthernetSink);
        @lifecycleSupport;
        @display("i=block/sink");
        @signal[packetReceived](type=inet::Packet);
        @statistic[packetReceived](source=packetReceived; record=count,"sum(packetBytes)","vector(packetBytes)"; interpolationmode=none);
    gates:
        input socketIn;
        output socketOut;
}
