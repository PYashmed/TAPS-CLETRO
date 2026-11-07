package satnet3d.mobility;

import inet.mobility.base.MovingMobilityBase;

//
// Ground mobility fixed to Earth surface coordinates (latitude/longitude),
// represented in an Earth-centered inertial frame by applying Earth rotation.
//
simple EarthFixedMobility extends MovingMobilityBase
{
    parameters:
        double latitude @unit(deg) @mutable = default(0deg);
        double longitude @unit(deg) @mutable = default(0deg);
        double altitude @unit(m) @mutable = default(0m);
        double earthRotationSpeed @unit(radps) @mutable = default(7.2921159e-5radps);
        double initialEarthRotation @unit(deg) @mutable = default(0deg);
        bool useMobilityManager = default(false);
        string mobilityManagerModule = default("^.^.mobilityManager");
        @class(satnet3d::EarthFixedMobility);
}
