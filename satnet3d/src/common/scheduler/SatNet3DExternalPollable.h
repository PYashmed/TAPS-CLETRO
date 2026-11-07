#ifndef __SATNET3D_EXTERNALPOLLLABLE_H
#define __SATNET3D_EXTERNALPOLLLABLE_H

namespace satnet3d {

/** Optional interface implemented by external sockets that can drain a batch. */
class SatNet3DExternalPollable
{
  public:
    virtual ~SatNet3DExternalPollable() = default;
    virtual bool pollExternal(int maxPackets) = 0;
};

} // namespace satnet3d

#endif
