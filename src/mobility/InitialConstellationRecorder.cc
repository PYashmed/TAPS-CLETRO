#include "InitialConstellationRecorder.h"

#include <fstream>
#include <iomanip>

#include "inet/common/ModuleAccess.h"
#include "inet/common/geometry/common/GeographicCoordinateSystem.h"
#include "inet/mobility/contract/IMobility.h"

namespace satnet3d {

using namespace inet;
using namespace omnetpp;

Define_Module(InitialConstellationRecorder);

void InitialConstellationRecorder::initialize(int stage)
{
    if (stage != INITSTAGE_LAST || !par("enabled").boolValue())
        return;

    cModule *network = getModuleByPath(par("networkModule").stringValue());
    if (network == nullptr)
        throw cRuntimeError("networkModule not found");
    auto coordinateSystem = getModuleFromPar<IGeographicCoordinateSystem>(par("coordinateSystemModule"), this);
    cModule *walkerManager = getModuleByPath(par("walkerManagerModule").stringValue());
    if (walkerManager == nullptr)
        throw cRuntimeError("walkerManagerModule not found");

    int numPlanes = walkerManager->par("numPlanes").intValue();
    int satsPerPlane = walkerManager->par("satsPerPlane").intValue();
    int expectedCount = numPlanes * satsPerPlane;
    const char *nodeName = par("nodeName").stringValue();
    if (!network->hasSubmoduleVector(nodeName) || network->getSubmoduleVectorSize(nodeName) != expectedCount)
        throw cRuntimeError("Expected %d %s modules", expectedCount, nodeName);

    std::ofstream output(par("outputFile").stringValue(), std::ios::out | std::ios::trunc);
    if (!output.is_open())
        throw cRuntimeError("Cannot open initial constellation output file '%s'", par("outputFile").stringValue());
    output << "satelliteIndex,planeIndex,slotIndex,latitudeDeg,longitudeDeg,altitudeM\n";
    output << std::fixed << std::setprecision(12);

    const char *mobilityName = par("mobilitySubmoduleName").stringValue();
    for (int index = 0; index < expectedCount; ++index) {
        cModule *node = network->getSubmodule(nodeName, index);
        cModule *mobilityModule = node != nullptr ? node->getSubmodule(mobilityName) : nullptr;
        auto mobility = dynamic_cast<IMobility *>(mobilityModule);
        if (mobility == nullptr)
            throw cRuntimeError("Mobility module not found at %s[%d]", nodeName, index);
        GeoCoord position = coordinateSystem->computeGeographicCoordinate(mobility->getCurrentPosition());
        output << index << ',' << index / satsPerPlane << ',' << index % satsPerPlane << ','
               << position.latitude.get<deg>() << ',' << position.longitude.get<deg>() << ','
               << position.altitude.get<m>() << '\n';
    }
}

void InitialConstellationRecorder::handleMessage(cMessage *message)
{
    throw cRuntimeError("InitialConstellationRecorder does not accept messages");
}

} // namespace satnet3d
