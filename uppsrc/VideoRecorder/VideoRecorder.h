#ifndef _VideoRecorder_VideoRecorder_h_
#define _VideoRecorder_VideoRecorder_h_

#include <Core/Core.h>
#include <Draw/Draw.h>
#include <plugin/jpg/jpg.h>

NAMESPACE_UPP

struct VideoRecorderOptions {
	String host = "127.0.0.1";
	int    port = 8082;
	int    seconds = 60;
	int    fps = 10;
	int    poll_ms = 10;
	int    timeout_ms = 5000;
	String out_path;
	String work_dir;
	String codec = "mpeg4";
	String pix_fmt = "yuv420p";
	String ffmpeg_dll_dir;
	int    bitrate = 4000000;
	bool   dump_frames = false;
	bool   help = false;
};

struct RecordedFrame : Moveable<RecordedFrame> {
	int    index = 0;
	uint32 id = 0;
	Size   size;
	String format;
	int64  elapsed_ms = 0;
	String dump_path;
};

END_UPP_NAMESPACE

#endif
