#ifndef __SATNET3D_GEODETICMOBILITYBASE_H
#define __SATNET3D_GEODETICMOBILITYBASE_H

#include <cmath>

#include "inet/common/ModuleAccess.h"
#include "inet/common/geometry/common/GeographicCoordinateSystem.h"
#include "inet/mobility/base/MovingMobilityBase.h"

#include "MobilityManager.h"

using namespace inet;

namespace satnet3d {

class GeodeticMobilityBase : public inet::MovingMobilityBase
{
  protected:
    const inet::IGeographicCoordinateSystem *coordinateSystem = nullptr;
    bool useMobilityManager = false;
    bool managedByMobilityManager = false;
    MobilityManager *mobilityManager = nullptr;

  protected:
    virtual void initialize(int stage) override
    {
        MovingMobilityBase::initialize(stage);
        if (stage == inet::INITSTAGE_LOCAL) {
            coordinateSystem = findModuleFromPar<inet::IGeographicCoordinateSystem>(par("coordinateSystemModule"), this);
            if (coordinateSystem == nullptr)
                throw cRuntimeError("%s requires coordinateSystemModule to be set", getClassName());

            if (hasPar("useMobilityManager")) {
                useMobilityManager = par("useMobilityManager").boolValue();
                if (useMobilityManager) {
                    const char *managerPath = par("mobilityManagerModule").stringValue();
                    cModule *managerModule = getModuleByPath(managerPath);
                    mobilityManager = dynamic_cast<MobilityManager *>(managerModule);
                    if (mobilityManager == nullptr)
                        EV_WARN << "MobilityManager module '" << managerPath
                                << "' not found, falling back to local mobility timer" << endl;
                }
            }

            WATCH(useMobilityManager);
            WATCH(managedByMobilityManager);
        }
    }

    virtual void initializePosition() override
    {
        if (useMobilityManager && mobilityManager != nullptr && mobilityManager->isEnabled() && !stationary && updateInterval > SIMTIME_ZERO) {
            MobilityBase::initializePosition();
            lastUpdate = simTime();
            managedByMobilityManager = mobilityManager->registerMobility(this, updateInterval);
            if (!managedByMobilityManager)
                scheduleUpdate();
        }
        else
            MovingMobilityBase::initializePosition();
    }

    static double normalizeLongitudeDeg(double longitudeDeg)
    {
        double normalized = std::fmod(longitudeDeg + 180.0, 360.0);
        if (normalized < 0)
            normalized += 360.0;
        return normalized - 180.0;
    }

    Coord computeSceneCoordinate(double latitudeDeg, double longitudeDeg, double altitudeMeters) const
    {
        return coordinateSystem->computeSceneCoordinate(GeoCoord(deg(latitudeDeg), deg(normalizeLongitudeDeg(longitudeDeg)), m(altitudeMeters)));
    }

    Coord computeSceneVelocityFromGeo(double latitudeDeg, double longitudeDeg, double altitudeMeters,
            double nextLatitudeDeg, double nextLongitudeDeg, double nextAltitudeMeters, double deltaTimeSeconds) const
    {
        if (deltaTimeSeconds <= 0)
            throw cRuntimeError("deltaTimeSeconds must be > 0, got %g", deltaTimeSeconds);
        Coord position = computeSceneCoordinate(latitudeDeg, longitudeDeg, altitudeMeters);
        Coord nextPosition = computeSceneCoordinate(nextLatitudeDeg, nextLongitudeDeg, nextAltitudeMeters);
        return (nextPosition - position) * (1.0 / deltaTimeSeconds);
    }

  public:
    virtual bool managedMove()
    {
        simtime_t now = simTime();
        if (nextChange == now || lastUpdate != now) {
            move();
            orient();
            lastUpdate = now;
            return true;
        }
        return false;
    }

    virtual void emitManagedMobilityStateChangedSignal()
    {
        emitMobilityStateChangedSignal();
    }

    virtual void managedMoveAndUpdate()
    {
        if (managedMove())
            emitManagedMobilityStateChangedSignal();
    }
};

} // namespace satnet3d

#endif
