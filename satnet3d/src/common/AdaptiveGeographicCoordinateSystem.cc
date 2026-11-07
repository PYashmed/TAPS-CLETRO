//
// Copyright (C) 2026
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#include "AdaptiveGeographicCoordinateSystem.h"

#include <cmath>

#if defined(WITH_OSGEARTH) && defined(INET_WITH_VISUALIZATIONOSG)

#include <osg/PositionAttitudeTransform>
#include <osgEarth/GeoTransform>

using namespace omnetpp;
using namespace inet;

namespace satnet3d {

Define_Module(AdaptiveGeographicCoordinateSystem);

namespace {

constexpr double EARTH_RADIUS_M = 6378137.0;
constexpr double PI = 3.14159265358979323846;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / PI;

Coord sphericalGeoToEcef(const GeoCoord& geographicCoordinate)
{
    double latitudeRad = geographicCoordinate.latitude.get<deg>() * DEG_TO_RAD;
    double longitudeRad = geographicCoordinate.longitude.get<deg>() * DEG_TO_RAD;
    double radius = EARTH_RADIUS_M + geographicCoordinate.altitude.get<m>();

    double cosLatitude = std::cos(latitudeRad);
    return Coord(radius * cosLatitude * std::cos(longitudeRad),
                 radius * cosLatitude * std::sin(longitudeRad),
                 radius * std::sin(latitudeRad));
}

GeoCoord ecefToSphericalGeo(const Coord& ecefCoordinate)
{
    double radius = ecefCoordinate.length();
    if (radius == 0)
        return GeoCoord(deg(0), deg(0), m(-EARTH_RADIUS_M));

    double latitudeDeg = std::asin(ecefCoordinate.z / radius) * RAD_TO_DEG;
    double longitudeDeg = std::atan2(ecefCoordinate.y, ecefCoordinate.x) * RAD_TO_DEG;
    double altitudeMeters = radius - EARTH_RADIUS_M;
    return GeoCoord(deg(latitudeDeg), deg(longitudeDeg), m(altitudeMeters));
}

} // namespace

void AdaptiveGeographicCoordinateSystem::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL) {
        auto sceneLatitude = deg(par("sceneLatitude"));
        auto sceneLongitude = deg(par("sceneLongitude"));
        auto sceneAltitude = m(par("sceneAltitude"));
        auto sceneHeading = deg(par("sceneHeading"));
        auto sceneElevation = deg(par("sceneElevation"));
        auto sceneBank = deg(par("sceneBank"));
        scenePosition = GeoCoord(sceneLatitude, sceneLongitude, sceneAltitude);

        auto osgCanvas = getParentModule()->getOsgCanvas();
        auto mapScene = osgCanvas != nullptr ? osgCanvas->getScene() : nullptr;
        mapNode = mapScene != nullptr ? osgEarth::MapNode::findMapNode(mapScene) : nullptr;

        if (mapNode != nullptr) {
            // The parameter is conventional direction of heading and elevation (positive heading turns left,
            // positive elevation lifts nose), but the EulerAngles class has different expectations.
            sceneOrientation = Quaternion(EulerAngles(-rad(sceneHeading - deg(90)), -rad(sceneElevation), rad(sceneBank)));

            osg::ref_ptr<osgEarth::GeoTransform> geoTransform = new osgEarth::GeoTransform();
            osg::ref_ptr<osg::PositionAttitudeTransform> localTransform = new osg::PositionAttitudeTransform();

            geoTransform->addChild(localTransform);
            geoTransform->setPosition(osgEarth::GeoPoint(mapNode->getMapSRS()->getGeographicSRS(),
                scenePosition.longitude.get<deg>(), scenePosition.latitude.get<deg>(), scenePosition.altitude.get<m>()));

            localTransform->setAttitude(osg::Quat(osg::Vec4d(sceneOrientation.v.x, sceneOrientation.v.y, sceneOrientation.v.z, sceneOrientation.s)));

            osg::ref_ptr<osg::Group> child = new osg::Group();
            localTransform->addChild(child);

            auto matrices = child->getWorldMatrices();
            ASSERT(matrices.size() == 1);

            locatorMatrix = matrices[0];
            inverseLocatorMatrix.invert(locatorMatrix);
        }
        else {
            sceneOrientation = Quaternion::IDENTITY;
        }
    }
}

Coord AdaptiveGeographicCoordinateSystem::computeSceneCoordinate(const GeoCoord& geographicCoordinate) const
{
    Coord ecefCoordinate = sphericalGeoToEcef(geographicCoordinate);
    if (mapNode == nullptr)
        return ecefCoordinate;

    auto sceneCoordinate = osg::Vec4d(ecefCoordinate.x, ecefCoordinate.y, ecefCoordinate.z, 1.0) * inverseLocatorMatrix;
    return Coord(sceneCoordinate.x(), sceneCoordinate.y(), sceneCoordinate.z());
}

GeoCoord AdaptiveGeographicCoordinateSystem::computeGeographicCoordinate(const Coord& sceneCoordinate) const
{
    if (mapNode == nullptr)
        return ecefToSphericalGeo(sceneCoordinate);

    auto ecefCoordinate = osg::Vec4d(sceneCoordinate.x, sceneCoordinate.y, sceneCoordinate.z, 1.0) * locatorMatrix;
    return ecefToSphericalGeo(Coord(ecefCoordinate.x(), ecefCoordinate.y(), ecefCoordinate.z()));
}

} // namespace satnet3d

#endif
