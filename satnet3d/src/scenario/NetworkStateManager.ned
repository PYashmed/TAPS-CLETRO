package satnet3d.scenario;

simple NetworkStateManager
{
    parameters:
        @class(satnet3d::NetworkStateManager);
        string networkModule = default("^");
        string switchTypePatterns = default("*");
        string switchNamePatterns = default("*");
        string hostTypePatterns = default("*");
        string hostNamePatterns = default("*");
        string mobilitySubmoduleName = default("mobility");
        string linkGateName = default("ethg");
        double delayRefreshInterval @unit(s) = default(0.1s);
        @display("i=block/cogwheel");
}
