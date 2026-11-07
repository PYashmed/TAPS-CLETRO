package satnet3d.common;

import inet.common.geometry.common.IGeographicCoordinateSystem;

simple AdaptiveGeographicCoordinateSystem like IGeographicCoordinateSystem
{
    parameters:
        @class(satnet3d::AdaptiveGeographicCoordinateSystem);
        @display("i=block/table2");
        double sceneLatitude @unit(deg);
        double sceneLongitude @unit(deg);
        double sceneAltitude @unit(m) = default(0m);
        double sceneHeading @unit(deg) = default(90deg);
        double sceneElevation @unit(deg) = default(0deg);
        double sceneBank @unit(deg) = default(0deg);
}