#include "EarthFixedMobility.h"

#include <cmath>

#include "inet/common/INETMath.h"

using namespace inet;

namespace satnet3d {

Define_Module(EarthFixedMobility);

void EarthFixedMobility::initialize(int stage)
{
    GeodeticMobilityBase::initialize(stage);

    if (stage == INITSTAGE_LOCAL) {
        auto latitude = deg(par("latitude"));
        auto longitude = deg(par("longitude"));
        altitude = par("altitude");
        auto initialEarthRotation = deg(par("initialEarthRotation"));
        earthRotationSpeed = par("earthRotationSpeed").doubleValue();

        double latitudeDeg = latitude.get<units::values::deg>();
        double longitudeDeg = longitude.get<units::values::deg>();

        if (latitudeDeg < -90 || latitudeDeg > 90)
            throw cRuntimeError("latitude must be in [-90,90] deg, got %g", latitudeDeg);
        if (longitudeDeg < -180 || longitudeDeg > 180)
            throw cRuntimeError("longitude must be in [-180,180] deg, got %g", longitudeDeg);
        if (altitude < 0)
            throw cRuntimeError("altitude must be >= 0, got %g", altitude);
        if (earthRotationSpeed < 0)
            throw cRuntimeError("earthRotationSpeed must be >= 0, got %g", earthRotationSpeed);

        latitudeRad = rad(latitude).get<units::values::rad>();
        longitudeRad = rad(longitude).get<units::values::rad>();
        initialEarthRotationRad = rad(initialEarthRotation).get<units::values::rad>();
        radius = EARTH_RADIUS_M + altitude;

        maxSpeed = radius * std::cos(latitudeRad) * earthRotationSpeed;
        stationary = (earthRotationSpeed == 0);
        lastAngularVelocity = Quaternion::IDENTITY;

        WATCH(radius);
        WATCH(earthRotationSpeed);
        WATCH(maxSpeed);
    }
}

void EarthFixedMobility::setInitialPosition()
{
    move();
    orient();
}

void EarthFixedMobility::move()
{
    double t = simTime().dbl();
    double lambda = longitudeRad + initialEarthRotationRad + earthRotationSpeed * t;
    constexpr double RAD_TO_DEG = 180.0 / 3.14159265358979323846;

    double latitudeDeg = latitudeRad * RAD_TO_DEG;
    double longitudeDeg = lambda * RAD_TO_DEG;
    double futureLongitudeDeg = (lambda + earthRotationSpeed) * RAD_TO_DEG;

    lastPosition = computeSceneCoordinate(latitudeDeg, longitudeDeg, altitude);
    lastVelocity = computeSceneVelocityFromGeo(latitudeDeg, longitudeDeg, altitude, latitudeDeg, futureLongitudeDeg, altitude, 1.0);

    nextChange = -1;
}

void EarthFixedMobility::orient()
{
    if (lastPosition != Coord::ZERO) {
        Coord up = lastPosition;
        up.normalize();
        lastOrientation = Quaternion::rotationFromTo(Coord::Z_AXIS, up);
    }
}

} // namespace satnet3d
