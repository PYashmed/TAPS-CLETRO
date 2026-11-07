package satnet3d.offline;

simple MatlabOfflineScheduleManager
{
    parameters:
        string networkModule = default("^");
        string configurationFile;
        int schedulingPriority = default(-85);
        bool applyGateSchedules = default(true);
        // When enabled, permanently open queue 7 when the final offline slice
        // ends so queued packets and retransmissions can drain.
        bool openPcp7AtScheduleEnd = default(false);
        int drainGateIndex = default(7);
        // Optional compatibility maps. An empty list keeps the file's module
        // and interface names unchanged (the existing Wuyuan behaviour).
        // satelliteIndexMap maps file satellite indices to network indices;
        // negative entries denote compatibility-only WINDOWS records.
        string satelliteIndexMap = default("");
        // The following lists are indexed by VLAN ID - 1. They restore the
        // per-flow access switches and satellite GSL ports when an exporter
        // has aggregated several ground links onto a mapped groundAccess/eth4.
        string sourceAccessIndexByVlan = default("");
        string destinationAccessIndexByVlan = default("");
        string satelliteGroundInterfaceByVlan = default("");
        @class(satnet3d::offline::MatlabOfflineScheduleManager);
        @display("i=block/control");
}
