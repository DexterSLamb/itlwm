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
    // Sequoia 15.7.5: Apple exports CCDataStream::withPipeAndName (subclass
    // factory). Same pattern as CCLogStream. Using CCStream::withPipeAndName
    // (abstract base) + OSDynamicCast<CCDataStream> yielded a non-NULL but
    // improperly-initialized CCDataStream pointer. Later wrapped via
    // CCFaultReporter::withStreamWorkloop, Apple's PeerManager called
    // ccFaultReporter->registerCallbacks (vtable[0x120]) → kernel helper
    // page-faulted on null+0x38 (CR2=0x38) deref'ing the broken stream's
    // ivars. Use the proper subclass factory.
    static CCDataStream *withPipeAndName(CCPipe *,char const*,CCStreamOptions const*);
};

#endif /* CCDataStream_h */
