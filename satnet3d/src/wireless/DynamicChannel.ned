package satnet3d.wireless;

import ned.DatarateChannel;
import inet.mobility.contract.IMobility;

// A dynamic channel that updates delay based on node distance (INET-style)
channel DynamicChannel extends DatarateChannel
{
    parameters:
        @class(satnet3d::DynamicChannel);
        @display("i=block/cable");
        delay @unit(s) = default(0s); // Initial delay
        datarate @unit(bps) = default(100Mbps);
        bool autoUpdateDelay = default(true); // If true, updates delay on mobility change
        bool useDynamicChannelManager = default(false);
        string dynamicChannelManagerModule = default("^.dynamicChannelManager");
        double propagationSpeed @unit(mps) = default(299792458mps); // light speed in vacuum
        double minUpdateInterval @unit(s) = default(0.1s); // Minimum update interval
}
