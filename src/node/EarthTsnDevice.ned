package satnet3d.node;

import inet.node.tsn.TsnDevice;

module EarthTsnDevice extends TsnDevice
{
    parameters:
        @display("i=device/receiverdish");
        osgModel = default("../../data/dishlow.osgb.50000.scale");
        numEthInterfaces = default(1);

        mobility.typename = default("EarthFixedMobility");
        mobility.coordinateSystemModule = default("coordinateSystem");
        mobility.updateInterval = default(0.1s);
        mobility.latitude = default(0deg);
        mobility.longitude = default(0deg);
        mobility.altitude = default(0m);
        mobility.earthRotationSpeed = default(7.2921159e-5radps);
        mobility.initialEarthRotation = default(0deg);
}
