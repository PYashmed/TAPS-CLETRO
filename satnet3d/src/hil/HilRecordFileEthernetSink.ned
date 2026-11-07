package satnet3d.hil;

import satnet3d.app.RecordFileEthernetSink;

// Read sequence number and generation time from the received wire payload.
simple HilRecordFileEthernetSink extends RecordFileEthernetSink
{
    parameters:
        @class(satnet3d::hil::HilRecordFileEthernetSink);
}
