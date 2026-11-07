// SPDX-License-Identifier: LGPL-3.0-or-later

import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;

namespace satnet3d;

// Application payload header carried by every reliable Ethernet DATA frame.
// The two timestamps deliberately have different meanings: the first one is
// stable across retransmissions, while the second one describes this attempt.
class ReliableEthernetHeader extends inet::FieldsChunk
{
    chunkLength = inet::B(36);
    uint64_t flowId;
    uint64_t sequenceNumber;
    uint32_t attempt;
    simtime_t originalCreationTime;
    simtime_t lastTransmissionTime;
}

// Safe, out-of-band replacement for the old receiver-to-sender memory write.
message ReliableDeliveryInd
{
    uint64_t flowId;
    uint64_t sequenceNumber;
}
