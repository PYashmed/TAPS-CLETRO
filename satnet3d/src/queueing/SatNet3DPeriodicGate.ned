package satnet3d.queueing;

import inet.queueing.base.PacketGateBase;
import inet.queueing.contract.IPacketGate;

simple SatNet3DPeriodicGate extends PacketGateBase like IPacketGate
{
    parameters:
        string clockModule = default(""); // Relative path of a module that implements IClock; optional
        bool initiallyOpen @mutable = default(true);
        double offset @mutable @unit(s) = default(0s);
        object durations @mutable @unit(s) = default([]);
        bool scheduleForAbsoluteTime = default(true);
        int openSchedulingPriority = default(0);
        int closeSchedulingPriority = default(0);
        bool enableImplicitGuardBand = default(true);
        bool usePeriodicGateManager = default(true);
        string periodicGateManagerModule = default("^.^.^.^.periodicGateManager");
        @class(satnet3d::queueing::SatNet3DPeriodicGate);
        @signal[guardBandStateChanged](type=bool);
        @statistic[guardBandState](title="guard band state"; type=enum; enum=ON, OFF; source=guardBandStateChanged; record=vector; interpolationmode=sample-hold);
}
