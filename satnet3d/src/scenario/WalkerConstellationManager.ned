package satnet3d.scenario;

import inet.common.scenario.ScenarioManager;

simple WalkerConstellationManager extends ScenarioManager
{
	parameters:
		@class(satnet3d::WalkerConstellationManager);
		bool autoGenerateConstellation = default(true);
		string targetNetworkModule = default("^");
		string satelliteModuleType = default("");
		string satelliteModuleName = default("satellite");
		int numEthInterfaces = default(5);
		string numEthInterfacesOverrides = default("");
		int numPlanes = default(8);
		int satsPerPlane = default(6);
		int phaseFactor = default(0);
		double initialPhaseOffset @unit(deg) = default(0deg);
		double orbitalAltitude @unit(m) = default(1414km);
		double orbitalInclination @unit(deg) = default(52deg);
		string constellationType = default("delta"); // "delta" (360-degree RAAN) or "star" (180-degree RAAN)
		double polarRegionLatitude @unit(deg) = default(75deg); // Walker-Star only; cross-plane ISLs are disabled at |latitude| >= this value
		double polarLinkUpdateInterval @unit(s) = default(1s); // Walker-Star polar-boundary detection resolution
		string coordinateSystemModule = default("^.coordinateSystem");
		string connectionType = default("oneStarFourLinks");
		string interSatelliteChannelType = default("satnet3d.wireless.DynamicChannel");
		double datarate @unit(bps) = default(1Gbps);
		double propagationSpeed @unit(mps) = default(299792458mps);
		string intraPlaneLinkColor = default("white");
		string interPlaneLinkColor = default("gray");
		string topologyCoordinatorModule = default("");
		@display("i=block/control");
}
