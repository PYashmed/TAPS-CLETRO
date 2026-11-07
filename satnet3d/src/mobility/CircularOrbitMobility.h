#ifndef __SATNET3D_CIRCULARORBITMOBILITY_H
#define __SATNET3D_CIRCULARORBITMOBILITY_H

#include "GeodeticMobilityBase.h"

using namespace inet;

namespace satnet3d {

class CircularOrbitMobility : public GeodeticMobilityBase
{
  protected:
    static constexpr double EARTH_RADIUS_M = 6378137.0;
    static constexpr double EARTH_MU = 3.986004418e14; // m^3/s^2

    double orbitalInclinationRad = 0;
    double raanRad = 0;
    double orbitalRadius = 0;
    double phase0Rad = 0;
    double angularSpeed = 0;
    double linearSpeed = 0;
    bool mobilityEnabled = true;
    ///////TJH
    //加入当前相位、飞行状态
    double currentPhaseDeg = 0;
    bool isAscending = true;
    ///////

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void setInitialPosition() override;
    virtual void move() override;
    virtual void orient() override;

  public:
    virtual double getMaxSpeed() const override { return mobilityEnabled ? linearSpeed : 0; }
    // Returns the analytic orbit position at the requested simulation time
    // without modifying the mobility module's current state.
    virtual Coord getPositionAt(simtime_t time) const;
    ///////TJH
    double getOrbitalInclinationRad() const { return orbitalInclinationRad; }
    double getRaanRad() const { return raanRad; }
    double getCurrentPhaseDeg() const { return currentPhaseDeg; }
    bool getIsAscending() const { return isAscending; }
    ///////
};

} // namespace satnet3d

#endif
