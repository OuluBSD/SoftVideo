#ifndef _VisualStateModel_VideoServerFrameSource_h_
#define _VisualStateModel_VideoServerFrameSource_h_

namespace Upp {

// ---------------------------------------------------------------------------
// VsmVideoServerFrameSource — live, continuous VsmFrameSource that connects
// to a running VideoServer (uppsrc/VideoServer) over its TCP frame-poll
// protocol and streams decoded frames. This is the live counterpart to
// VsmSessionStoreSource (FrameSource.h/.cpp), which only replays an
// already-recorded, finished session directory (Task 0279).
//
// Wire protocol (v1 introduced by Task 0278; see VideoServer::ClientThread() in
// uppsrc/VideoServer/main.cpp, and the two already-proven client
// implementations this class is based on: reference/VideoServerFrameRecorder
// /main.cpp and uppsrc/VideoRecorder/main.cpp's CaptureFrames()): the client
// sends the last-seen 4-byte little-endian frame id; the server immediately
// replies with a 4-byte frame id + 4-byte payload size + (if size>0) that
// many payload bytes (JPEG, or a "YUV0"-prefixed raw-YUV packet depending on
// the server's --wire-format). During Open this client opts into v2 when the
// server accepts its magic/version handshake; v2 additionally replies with
// video PTS and loop count. Older servers are detected then reconnected using
// the untouched v1 request/reply shape. size==0 means "nothing newer than what
// you already have" — the server NEVER blocks waiting for a new frame to
// exist, so all waiting/polling for a new frame is this client's own
// responsibility (see ReadFrame()).
class VsmVideoServerFrameSource : public VsmFrameSource {
public:
	struct ReviewFrame : Moveable<ReviewFrame> {
		VsmImageBuffer image;
		uint32 frame_id = 0;
		int64 video_timestamp_ms = -1;
		int64 video_loop = -1;
	};
	// Error classification for GetLastError()/GetLastErrorKind(). The two
	// kinds must be treated very differently by callers:
	//   VSM_VSFS_ERR_NONE:            no error (last operation succeeded).
	//   VSM_VSFS_ERR_TIMEOUT:         no NEW frame arrived within the
	//     configured wait window (SetWaitTimeoutMs()), or a single transient
	//     socket-op/decode hiccup. RECOVERABLE: IsReady() stays true, and
	//     the intended caller behavior is to simply call ReadFrame() again
	//     (e.g. because the source is momentarily running behind the
	//     server's own capture fps) — this is normal steady-state behavior
	//     for a live source, not a failure to surface to the user.
	//   VSM_VSFS_ERR_CONNECTION_LOST: the TCP connection itself failed or
	//     was closed. UNRECOVERABLE: IsReady() becomes false; the caller
	//     must call Open() again (a fresh reconnect) to resume.
	enum {
		VSM_VSFS_ERR_NONE = 0,
		VSM_VSFS_ERR_TIMEOUT,
		VSM_VSFS_ERR_CONNECTION_LOST,
	};

	void SetLog(AppLog* sink) { log_.SetSink(sink); }

	// Tuning knobs; all have workable defaults and only need to be touched
	// by tests/edge cases. Must be called before Open() to take effect on
	// the open-priming read.
	void SetConnectTimeoutMs(int ms) { connect_timeout_ms_   = max(1, ms); }
	void SetSocketTimeoutMs(int ms)  { socket_op_timeout_ms_ = max(1, ms); }
	void SetWaitTimeoutMs(int ms)    { wait_timeout_ms_       = max(1, ms); }
	void SetPollIntervalMs(int ms)   { poll_interval_ms_      = max(1, ms); }

	// VideoServer does not report its configured --fps anywhere in the wire
	// protocol; GetFPS() honestly returns 0 (unknown) unless a caller who
	// independently knows the server's real --fps (e.g. because it also
	// launched the server process) supplies it here. Never fabricated.
	void SetKnownFps(int fps) { fps_ = max(0, fps); }

	bool   Open(const String& uri) override; // uri: "host:port"
	void   Close() override;
	bool   IsReady()  const override { return is_ready_; }
	int    GetWidth() const override { return width_; }
	int    GetHeight()const override { return height_; }
	int    GetFPS()   const override { return fps_; } // 0 unless SetKnownFps() was called

	// Blocks (bounded by SetWaitTimeoutMs(), default 5000ms) until a frame
	// newer than the last one seen arrives, decodes it into out_frame, and
	// returns true. out_ts_ms is set to the real wall-clock moment this
	// frame was received (msecs()) — not an estimate — since a later task
	// (0282) anchors latency measurement against it.
	// Returns false when no newer frame arrived within the wait window
	// (recoverable — see GetLastErrorKind()) or on an unrecoverable
	// connection error.
	bool   ReadFrame(VsmImageBuffer& out_frame, int64& out_ts_ms) override;
	bool   IsRecoverableNoFrame() const override { return is_ready_ && last_error_kind_ == VSM_VSFS_ERR_TIMEOUT; }
	// The local receive timestamp returned by ReadFrame remains the latency
	// clock. A negotiated v2 VideoServer additionally exposes source-video PTS
	// and loop count here; v1 and non-video servers report -1.
	int64  GetLastVideoTimestampMs() const { return last_video_timestamp_ms_; }
	int64  GetLastVideoLoop() const { return last_video_loop_; }
	uint32 GetLastServerFrameId() const { return last_id_; }
	bool   UsesVideoServerProtocolV2() const { return use_v2_; }

	// Opens a short-lived, independent v3 connection and retrieves at most
	// max_frames retained source frames nearest target_video_timestamp_ms. This
	// deliberately never touches the normal live socket or last_id_: callers
	// may inspect old evidence without turning newest-frame streaming into a
	// backlog. Returns false when an old/v2-only server lacks this opt-in API.
	bool   RequestTargetedReview(int64 target_video_timestamp_ms, int64 target_video_loop,
	                             int range_ms, int max_frames, Vector<ReviewFrame>& out_frames);
	bool   IsTargetedReviewAvailable() const { return targeted_review_available_; }

	String GetLastError()     const override { return last_error_; }
	int    GetLastErrorKind() const          { return last_error_kind_; }
	String GetSourceInfo()    const override;

	// Task 0345: a reader thread that only measures "time inside ReadFrame()"
	// cannot honestly report busy-vs-wait, because one ReadFrame() call mixes
	// two very different things: polling a server that has nothing new yet
	// (pure waiting, no CPU) and reading + JPEG-decoding a payload that just
	// arrived (real work). These two monotonic microsecond accumulators report
	// that split, so the caller can attribute its own thread time correctly.
	// They are pure accounting: nothing here changes what ReadFrame() does.
	//   GetPollWaitUsec()  — round trips that returned "nothing newer" plus the
	//                        poll-interval sleeps between them.
	//   GetFrameWorkUsec() — round trips that actually delivered and decoded a
	//                        frame.
	// Both are cumulative for the lifetime of this object; take deltas.
	int64  GetPollWaitUsec()  const { return poll_wait_us_; }
	int64  GetFrameWorkUsec() const { return frame_work_us_; }

private:
	// Single request/reply round trip against the server.
	// Returns: 1 = got a genuinely new frame (out_frame/out_id filled),
	//          0 = polled successfully but nothing newer than last_id_ yet,
	//         -1 = recoverable hiccup (a single socket op timed out, or the
	//              payload failed to decode) — caller should retry,
	//         -2 = unrecoverable connection error.
	int  PollOnce(VsmImageBuffer& out_frame, uint32& out_id, int64& out_video_timestamp_ms,
	              int64& out_video_loop, String& out_detail);
	bool NegotiateProtocolV2();
	void MarkTimeout(const String& detail);
	void MarkConnectionLost(const String& detail);

	TcpSocket socket_;
	CoreLog   log_;

	bool   is_ready_ = false;
	String host_;
	int    port_   = 0;
	int    width_  = 0;
	int    height_ = 0;
	int    fps_    = 0;
	uint32 last_id_ = 0;
	bool   use_v2_ = false;
	int64  last_video_timestamp_ms_ = -1;
	int64  last_video_loop_ = -1;
	bool   targeted_review_available_ = false;

	String last_error_;
	int    last_error_kind_ = VSM_VSFS_ERR_NONE;

	// Frame read during Open() to establish width_/height_ up front (per the
	// interface contract); handed to the first ReadFrame() call instead of
	// being decoded twice or silently dropped.
	bool           has_pending_   = false;
	VsmImageBuffer pending_frame_;
	int64          pending_ts_ms_ = 0;

	int64 poll_wait_us_  = 0; // Task 0345, see GetPollWaitUsec()
	int64 frame_work_us_ = 0;

	int connect_timeout_ms_   = 5000;
	int socket_op_timeout_ms_ = 3000;
	int wait_timeout_ms_      = 5000;
	int poll_interval_ms_     = 33;
};

} // namespace Upp

#endif
