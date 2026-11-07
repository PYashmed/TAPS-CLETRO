//
// Copyright (C) 2020 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


package satnet3d.source;

import inet.queueing.base.ActivePacketSourceBase;
import inet.queueing.contract.IActivePacketSource;

//
// This module is an active packet source which pushes packets into the connected
// module. It pushes whole packets through the out gate at every production
// interval. It pushes the first packet after the initial production offset.
//
// If backpressure is present at the end of a production interval, then it waits
// until the backpressure disappears, and then pushes out a packet. The production
// interval timer always starts after the module pushes a packet out.
//
// Invariants:
//  - two subsequent packets are always pushed out at least productionInterval time apart
//  - backpressure prevents packets from being pushed out
// 
// 此外还添加了改变发包周期的功能，通过productionIntervalList参数指定一组发包周期，
// 并按照productionIntervalSwapPeriod参数指定的周期进行切换
//
simple ActivePacketSourceMultipleInterval extends ActivePacketSourceBase like IActivePacketSource
{
    parameters:
        string clockModule = default(""); // relative path of a module that implements IClock; optional
        double initialProductionOffset @mutable @unit(s) = default(-1s); // initial duration before which packets are not produced. When negative, then produce packet in initialize()
        volatile double productionInterval @unit(s) = default(1000s); // elapsed time between subsequent packets pushed to the connected packet consumer
        double stopTime @unit(s) = default(-1s); // no packet is produced at or after this simulation/clock time; negative means unlimited
        object productionIntervalList = default([]);
        double productionIntervalSwapPeriod @unit(s) = default(15s);
        int pcp = default(-1); // optional IEEE 802.1Q priority request
        int vlanId = default(-1); // optional IEEE 802.1Q VLAN request
        bool scheduleForAbsoluteTime = default(true); // when a clock is used relative means that setting the clock will not affect the simulation time of the event
        @class(satnet3d::ActivePacketSourceMultipleInterval);
    gates:
        output out @labels(push);
}
