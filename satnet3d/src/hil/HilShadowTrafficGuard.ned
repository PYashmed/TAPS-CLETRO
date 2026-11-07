package satnet3d.hil;

// Unused logical directions need no NIC, but must never become a silent
// software bypass if an offline route is changed after the wiring is planned.
simple HilShadowTrafficGuard
{
    parameters:
        string shadowSatellites;
        @class(satnet3d::hil::HilShadowTrafficGuard);
        @display("i=block/control");
}
