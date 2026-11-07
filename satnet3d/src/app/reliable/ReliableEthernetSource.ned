package satnet3d.app.reliable;

import inet.applications.contract.IApp;

simple ReliableEthernetSource like IApp
{
    parameters:
        string interfaceTableModule;
        string interface = default("eth0");
        string localAddress = default("");
        string remoteAddress;
        string coordinatorModule;
        int flowId;
        int initialSequenceNumber = default(0);
        double startTime @unit(s) = default(0s);
        double stopTime @unit(s) = default(-1s); // stops generating new sequence numbers; outstanding packets may still be retransmitted
        double productionInterval @unit(s);
        double retransmissionTimeout @unit(s) = default(60ms);
        int maxRetransmissions = default(-1); // -1 means unlimited
        int packetLength @unit(B) = default(1430B); // complete application payload, including the 36 B reliability header
        int pcp = default(7); // -1 disables the PCP request tag
        int vlanId = default(1); // -1 disables the VLAN request tag
        bool steal = default(false);
        double stopOperationExtraTime @unit(s) = default(-1s);
        double stopOperationTimeout @unit(s) = default(2s);
        @class(satnet3d::ReliableEthernetSource);
        @lifecycleSupport;
        @display("i=block/source");
        @signal[packetSent](type=inet::Packet);
        @signal[packetGenerated](type=inet::Packet);
        @signal[packetRetransmitted](type=inet::Packet);
        @signal[deliveryConfirmed](type=long);
        @signal[packetFailed](type=long);
        @signal[outstandingPacketCount](type=long);
        @statistic[packetSent](source=packetSent; record=count,"sum(packetBytes)","vector(packetBytes)"; interpolationmode=none);
        @statistic[packetGenerated](source=packetGenerated; record=count; interpolationmode=none);
        @statistic[packetRetransmitted](source=packetRetransmitted; record=count; interpolationmode=none);
        @statistic[deliveryConfirmed](source=deliveryConfirmed; record=count; interpolationmode=none);
        @statistic[packetFailed](source=packetFailed; record=count; interpolationmode=none);
        @statistic[outstandingPacketCount](source=outstandingPacketCount; record=vector,timeavg,max; interpolationmode=sample-hold);
    gates:
        input socketIn;
        output socketOut;
        input deliveryIn;
}
