#ifndef _VisualStateModel_FrameSource_h_
#define _VisualStateModel_FrameSource_h_

namespace Upp {

// ---------------------------------------------------------------------------
// Abstract frame source interface

class VsmFrameSource {
public:
	virtual ~VsmFrameSource() {}

	virtual bool   Open(const String& uri) = 0;
	virtual void   Close() = 0;
	virtual bool   IsReady()  const = 0;
	virtual int    GetWidth() const = 0;
	virtual int    GetHeight()const = 0;
	virtual int    GetFPS()   const = 0; // 0 if unknown

	// Read next frame into out_frame; set out_ts_ms to millisecond timestamp.
	// Returns false at end of stream or on unrecoverable error.
	virtual bool   ReadFrame(VsmImageBuffer& out_frame, int64& out_ts_ms) = 0;
	// Image-native fast path: decode straight into an Upp Image (skips the
	// canonical-buffer round-trip in pipelines). Sources that support it
	// return true from SupportsFrameImage; for them a false return means
	// end of stream or error. The default reports unsupported and callers
	// fall back to ReadFrame + conversion.
	virtual bool   SupportsFrameImage() const { return false; }
	virtual bool   ReadFrameImage(Upp::Image& out_image, int64& out_ts_ms) { return false; }
	// Pre-position a seekable source near `pts_ms` before ReadFrame, so a late
	// start interval does not need to decode the whole stream from the beginning.
	// Not every source is seekable; the default is a no-op returning false.
	virtual bool   SeekToMs(int64 pts_ms) { return false; }
	// A continuous source may report a bounded wait with no newer frame while
	// staying connected. The caller must not mistake that normal live idle for
	// EOF or a transport failure.
	virtual bool   IsRecoverableNoFrame() const { return false; }

	virtual String GetLastError()  const { return String(); }
	virtual String GetSourceInfo() const { return String(); }
};

// Replays a directory of full-composite PNG frames. Filenames must be an
// integer millisecond PTS followed by .png (for example 00010672.png). This
// source deliberately never derives timestamps from a nominal frame rate.
class VsmPngDirectoryFrameSource : public VsmFrameSource {
public:
	bool   Open(const String& uri) override;
	void   Close() override;
	bool   IsReady()  const override { return is_ready_; }
	int    GetWidth() const override { return width_; }
	int    GetHeight()const override { return height_; }
	int    GetFPS()   const override { return 0; }
	bool   ReadFrame(VsmImageBuffer& out_frame, int64& out_ts_ms) override;
	String GetLastError()  const override { return last_error_; }
	String GetSourceInfo() const override { return source_info_; }

private:
	struct Entry : Moveable<Entry> {
		int64  pts_ms = -1;
		String path;
	};
	Vector<Entry> entries_;
	int    cursor_ = 0;
	int    width_ = 0;
	int    height_ = 0;
	bool   is_ready_ = false;
	String last_error_;
	String source_info_;
};

// Headless regression for numeric PNG filename timestamps.
bool VsmPngDirectoryFrameSourceSelfTest(String& failure);

// ---------------------------------------------------------------------------
// Stepped frame source — for semi-live, event-stepped sources whose state
// only advances when explicitly told to (e.g. a game driven one card play
// at a time). ReadFrame() on these sources captures the current state (as
// of the most recent Step()); it does not itself advance anything.

class VsmSteppedFrameSource : public VsmFrameSource {
public:
	// Advance the underlying source by one logical unit (source-defined:
	// one card play, one AI turn, etc.). Returns false on error or when the
	// source has no more steps to take (check GetLastError() to tell them
	// apart, empty means "done", not "failed").
	virtual bool Step() = 0;

	// True while Step() can still be called meaningfully. Callers should
	// check this before Step(), not rely on Step()'s return value alone.
	virtual bool HasMoreSteps() const = 0;
};

// ---------------------------------------------------------------------------
// Source info metadata

struct VsmFrameSourceInfo : Moveable<VsmFrameSourceInfo> {
	String source_type;
	String uri;
	int    width       = 0;
	int    height      = 0;
	int    fps         = 0;
	int    frame_count = -1; // -1 if unknown / live

	void Jsonize(JsonIO& json) {
		json("source_type", source_type)("uri", uri)
		    ("width",       width)      ("height",      height)
		    ("fps",         fps)        ("frame_count", frame_count);
	}
};

// ---------------------------------------------------------------------------
// VsmSessionStoreSource — replays an existing VSM session directory

} // namespace Upp

#endif
