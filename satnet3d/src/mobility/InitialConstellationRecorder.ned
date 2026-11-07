package satnet3d.mobility;

simple InitialConstellationRecorder
{
    parameters:
        bool enabled = default(true);
        string networkModule = default("^");
        string coordinateSystemModule = default("^.coordinateSystem");
        string walkerManagerModule = default("^.walkerDeltaScenarioManager");
        string nodeName = default("satellite");
        string mobilitySubmoduleName = default("mobility");
        string outputFile;
        @class(satnet3d::InitialConstellationRecorder);
        @display("i=block/table");
}
