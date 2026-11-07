package satnet3d.wireless;

//
// Batches delay refreshes for DynamicChannel instances.
//
simple DynamicChannelManager
{
    parameters:
        bool enabled = default(true);
        double updateInterval @unit(s) = default(0.1s);
        int schedulingPriority = default(-90);
        bool reportManagedChannels = default(true);
        @class(satnet3d::DynamicChannelManager);
        @display("i=block/cogwheel");
}
