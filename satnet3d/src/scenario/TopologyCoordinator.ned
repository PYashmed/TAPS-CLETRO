package satnet3d.scenario;

simple TopologyCoordinator
{
    parameters:
        @class(satnet3d::TopologyCoordinator);
        string networkModule = default("^");
        string linkGateName = default("ethg");
        string channelType = default("satnet3d.wireless.DynamicChannel");
        double datarate @unit(bps) = default(1Gbps);
        double propagationSpeed @unit(mps) = default(299792458mps);
        string linkColor = default("cyan");
        string networkStateManagerModule = default("");
        @display("i=block/cogwheel");
}
