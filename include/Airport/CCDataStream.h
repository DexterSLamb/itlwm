//
//  CCDataStream.h
//  itlwm
//
//  Reverse-engineered from CoreCapture.kext (macOS 26 Tahoe).
//  CCStream::withPipeAndName with stream_type=1 creates CCDataStream.
//  Object size: 0x98 bytes. Parent: CCStream (0x90 bytes).
//

#ifndef CCDataStream_h
#define CCDataStream_h

#include "CCStream.h"

class CCDataStream : public CCStream {
    OSDeclareDefaultStructors(CCDataStream)

public:
    // Instance methods exported by CoreCapture:
    //   openSession(char const*)
    //   openSession(char const*, CCTimestamp const*)
    //   closeSession(CCDataSession*)
    //   saveData(char const*, OSData*, void(*)(OSObject*,int,void*), OSObject*, CCDataSession*)
    //   hasProfileLoaded()
    // We don't call these directly; the class is needed for OSDynamicCast
    // after CCStream::withPipeAndName(pipe, name, {stream_type=1}).
};

#endif /* CCDataStream_h */
