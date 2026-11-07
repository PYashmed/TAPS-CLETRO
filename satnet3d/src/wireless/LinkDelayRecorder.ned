package satnet3d.wireless;

// Periodically records the currently effective propagation delay of every
// directed DynamicChannel in the target network.
simple LinkDelayRecorder
{
    parameters:
        bool enabled = default(false);
        string networkModule = default("^");
        string outputFile = default("link-delays.csv");
        double sampleInterval @unit(s) = default(0.1s);
        double startTime @unit(s) = default(0s);
        double stopTime @unit(s) = default(-1s); // Exclusive; negative means no recorder-specific limit
        int schedulingPriority = default(-60);
        bool appendFile = default(false);
        bool writeHeader = default(true);
        bool flushEverySample = default(true);
        @class(satnet3d::LinkDelayRecorder);
        @display("i=block/table");
}
