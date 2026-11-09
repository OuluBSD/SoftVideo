#ifndef _VideoCore_VideoCore_h_
#define _VideoCore_VideoCore_h_

#include <Core/Core.h>
#include <Draw/Draw.h>

#ifndef LogInfo
#define LogInfo(log, tag, msg) RLOG(tag << ": " << msg)
#endif

#ifndef LogWarn
#define LogWarn(log, tag, msg) RLOG("WARN " << tag << ": " << msg)
#endif

#include "ImageBuffer.h"
#include "FrameSource.h"
#include "VideoServerFrameSource.h"
#include "VideoFileFrameSource.h"

#endif
