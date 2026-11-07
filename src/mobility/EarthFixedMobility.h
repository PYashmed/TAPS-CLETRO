#ifndef __SATNET3D_EARTHFIXEDMOBILITY_H
#define __SATNET3D_EARTHFIXEDMOBILITY_H

#include "GeodeticMobilityBase.h"

using namespace inet;

namespace satnet3d {

class EarthFixedMobility : public GeodeticMobilityBase
{
  protected:
    static constexpr double EARTH_RADIUS_M = 6378137.0;

    double latitudeRad = 0;
    double longitudeRad = 0;
    double altitude = 0;
    double earthRotationSpeed = 0;
    double initialEarthRotationRad = 0;
    double radius = EARTH_RADIUS_M;
    double maxSpeed = 0;

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void setInitialPosition() override;
    virtual void move() override;
    virtual void orient() override;

  public:
    virtual double getMaxSpeed() const override { return maxSpeed; }
};

} // namespace satnet3d

#endif
