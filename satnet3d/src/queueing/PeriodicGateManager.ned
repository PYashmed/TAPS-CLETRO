package satnet3d.queueing;

simple PeriodicGateManager
{
    parameters:
        string clockModule = default(""); // Relative path of a module that implements IClock; optional
        bool enabled = default(true);
        int schedulingPriority = default(0);
        @class(satnet3d::queueing::PeriodicGateManager);
        @display("i=block/timer");
}
