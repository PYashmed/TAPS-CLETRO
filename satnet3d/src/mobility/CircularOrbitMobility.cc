#include "CircularOrbitMobility.h"

#include <cmath>

#include "inet/common/INETMath.h"

using namespace inet;

namespace satnet3d {

Define_Module(CircularOrbitMobility);

void CircularOrbitMobility::initialize(int stage)
{
    GeodeticMobilityBase::initialize(stage);

    if (stage == inet::INITSTAGE_LOCAL) {
        auto orbitalInclination = inet::deg(par("orbitalInclination"));
        auto raan = inet::deg(par("raan"));
        double orbitalAltitude = par("orbitalAltitude");
        auto initialPhase = inet::deg(par("initialPhase"));
        mobilityEnabled = par("mobilityEnabled").boolValue();
        double orbitalInclinationDeg = orbitalInclination.get<inet::units::values::deg>();
        double raanDeg = raan.get<inet::units::values::deg>();
        double initialPhaseDeg = initialPhase.get<inet::units::values::deg>();

        if (orbitalInclinationDeg < 0 || orbitalInclinationDeg > 180)
            throw cRuntimeError("orbitalInclination must be in [0,180] deg, got %g", orbitalInclinationDeg);
        if (raanDeg < -180 || raanDeg > 180)
            throw cRuntimeError("raan must be in [-180,180] deg, got %g", raanDeg);
        if (orbitalAltitude < 0)
            throw cRuntimeError("orbitalAltitude must be >= 0, got %g", orbitalAltitude);
        if (initialPhaseDeg < 0 || initialPhaseDeg > 360)
            throw cRuntimeError("initialPhase must be in [0,360] deg, got %g", initialPhaseDeg);

        orbitalInclinationRad = inet::rad(orbitalInclination).get<inet::units::values::rad>();
        raanRad = inet::rad(raan).get<inet::units::values::rad>();
        phase0Rad = inet::rad(initialPhase).get<inet::units::values::rad>();

        ///////TJH
        // Initialize the ascending/descending state from the initial phase.
        currentPhaseDeg = std::fmod(initialPhaseDeg, 360.0);
        if (currentPhaseDeg < 0)
            currentPhaseDeg += 360.0;
        isAscending = !(currentPhaseDeg > 90.0 && currentPhaseDeg <= 270.0);
        ///////

        orbitalRadius = EARTH_RADIUS_M + orbitalAltitude;
        angularSpeed = std::sqrt(EARTH_MU / (orbitalRadius * orbitalRadius * orbitalRadius));
        linearSpeed = angularSpeed * orbitalRadius;

        stationary = (angularSpeed == 0) || !mobilityEnabled;
        lastAngularVelocity = inet::Quaternion(inet::EulerAngles(inet::rad(0), inet::rad(0), inet::rad(angularSpeed)));

        WATCH(orbitalRadius);
        WATCH(angularSpeed);
        WATCH(linearSpeed);
        WATCH(mobilityEnabled);
        WATCH(currentPhaseDeg);
        WATCH(isAscending);
    }
}

void CircularOrbitMobility::setInitialPosition()
{
    move();
    orient();
}

void CircularOrbitMobility::move()
{
    constexpr double RAD_TO_DEG = 180.0 / 3.14159265358979323846;
    auto computeEcefPosition = [&](double u, Coord& position) {
        double cosO = std::cos(raanRad);
        double sinO = std::sin(raanRad);
        double cosI = std::cos(orbitalInclinationRad);
        double sinI = std::sin(orbitalInclinationRad);
        double cosU = std::cos(u);
        double sinU = std::sin(u);

        position.x = orbitalRadius * (cosO * cosU - sinO * sinU * cosI);
        position.y = orbitalRadius * (sinO * cosU + cosO * sinU * cosI);
        position.z = orbitalRadius * (sinU * sinI);
    };
    auto ecefToGeo = [&](const Coord& ecef, double& latitudeDeg, double& longitudeDeg, double& altitudeMeters) {
        double r = ecef.length();
        latitudeDeg = std::asin(ecef.z / r) * RAD_TO_DEG;
        longitudeDeg = std::atan2(ecef.y, ecef.x) * RAD_TO_DEG;
        altitudeMeters = r - EARTH_RADIUS_M;
    };

    double t = mobilityEnabled ? simTime().dbl() : 0.0;
    double u = phase0Rad + angularSpeed * t;
    double uNext = mobilityEnabled ? phase0Rad + angularSpeed * (t + 1.0) : u;
    ///////TJH
    // Normalize the current orbital phase to [0, 360) degrees. A satellite is
    // descending in (90, 270] degrees and ascending everywhere else.
    currentPhaseDeg = std::fmod(u * RAD_TO_DEG, 360.0);
    if (currentPhaseDeg < 0)
        currentPhaseDeg += 360.0;
    isAscending = !(currentPhaseDeg > 90.0 && currentPhaseDeg <= 270.0);
    ///////

    Coord ecef;
    computeEcefPosition(u, ecef);

    Coord ecefNext;
    computeEcefPosition(uNext, ecefNext);

    double latitudeDeg;
    double longitudeDeg;
    double altitudeMeters;
    ecefToGeo(ecef, latitudeDeg, longitudeDeg, altitudeMeters);

    double nextLatitudeDeg;
    double nextLongitudeDeg;
    double nextAltitudeMeters;
    ecefToGeo(ecefNext, nextLatitudeDeg, nextLongitudeDeg, nextAltitudeMeters);

        lastPosition = computeSceneCoordinate(latitudeDeg, longitudeDeg, altitudeMeters);
        lastVelocity = computeSceneVelocityFromGeo(latitudeDeg, longitudeDeg, altitudeMeters,
            nextLatitudeDeg, nextLongitudeDeg, nextAltitudeMeters, 1.0);

    nextChange = -1;
    // EV << "Moved to position: " << lastPosition << " with velocity: " << lastVelocity << endl;
}

Coord CircularOrbitMobility::getPositionAt(simtime_t time) const
{
    constexpr double RAD_TO_DEG = 180.0 / 3.14159265358979323846;
    double u = phase0Rad + angularSpeed * time.dbl();
    double cosO = std::cos(raanRad);
    double sinO = std::sin(raanRad);
    double cosI = std::cos(orbitalInclinationRad);
    double sinI = std::sin(orbitalInclinationRad);
    double cosU = std::cos(u);
    double sinU = std::sin(u);

    Coord ecef(
        orbitalRadius * (cosO * cosU - sinO * sinU * cosI),
        orbitalRadius * (sinO * cosU + cosO * sinU * cosI),
        orbitalRadius * (sinU * sinI));
    double radius = ecef.length();
    double latitudeDeg = std::asin(ecef.z / radius) * RAD_TO_DEG;
    double longitudeDeg = std::atan2(ecef.y, ecef.x) * RAD_TO_DEG;
    double altitudeMeters = radius - EARTH_RADIUS_M;
    return computeSceneCoordinate(latitudeDeg, longitudeDeg, altitudeMeters);
}

void CircularOrbitMobility::orient()
{
    // Keep satellite body axis pointing to the orbit center (scene origin).
    if (lastPosition != Coord::ZERO) {
        Coord towardCenter = -lastPosition;
        towardCenter.normalize();
        lastOrientation = Quaternion::rotationFromTo(Coord::X_AXIS, towardCenter);
    }
}

} // namespace satnet3d
