//
// Copyright (C) 2026
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#ifndef __SATNET3D_ADAPTIVEGEOGRAPHICCOORDINATESYSTEM_H
#define __SATNET3D_ADAPTIVEGEOGRAPHICCOORDINATESYSTEM_H

#include "inet/common/geometry/common/GeographicCoordinateSystem.h"

#if defined(WITH_OSGEARTH) && defined(INET_WITH_VISUALIZATIONOSG)
#include <osgEarth/GeoTransform>
#endif

namespace satnet3d {

#if defined(WITH_OSGEARTH) && defined(INET_WITH_VISUALIZATIONOSG)

class AdaptiveGeographicCoordinateSystem : public inet::OsgGeographicCoordinateSystem
{
  protected:
    static constexpr double metersPerDegree = 111320;

  protected:
    virtual void initialize(int stage) override;

  public:
    virtual inet::Coord computeSceneCoordinate(const inet::GeoCoord& geographicCoordinate) const override;
    virtual inet::GeoCoord computeGeographicCoordinate(const inet::Coord& sceneCoordinate) const override;
};

#endif

} // namespace satnet3d

#endif