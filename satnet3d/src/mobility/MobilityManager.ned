package satnet3d.mobility;

//
// Batches periodic updates for managed satellite mobility modules.
// Mobility modules with the same next update time are updated by one self-message.
//
simple MobilityManager
{
    parameters:
        bool enabled = default(true);
        int schedulingPriority = default(-100);
        @class(satnet3d::MobilityManager);
        @display("i=block/cogwheel");
}
