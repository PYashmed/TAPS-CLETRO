package satnet3d.mobility;

import inet.mobility.base.MovingMobilityBase;

//
// Satellite circular orbit mobility around the scene origin.
// The scene origin is expected to be near Earth's center for Earth-centered simulation.
//
simple CircularOrbitMobility extends MovingMobilityBase
{
    parameters:
        double orbitalInclination @unit(deg) @mutable = default(53deg); // 0..180 deg
        double raan @unit(deg) @mutable = default(0deg); // Right ascension of ascending node, -180..180 deg
        double orbitalAltitude @unit(m) @mutable = default(550km); // Altitude above Earth's surface
        double initialPhase @unit(deg) @mutable = default(0deg); // Initial orbital phase angle, 0..360 deg
        bool mobilityEnabled = default(true); // Enable analytic orbital movement
        bool useMobilityManager = default(false);
        string mobilityManagerModule = default("^.^.mobilityManager");
        @class(satnet3d::CircularOrbitMobility);
}
