#include "VideoCore.h"
#include <plugin/jpg/jpg.h>

namespace Upp {

static const uint32 VIDEO_SERVER_V2_REQUEST_MAGIC = 0x32534d56; // "VMS2"
static const uint32 VIDEO_SERVER_V2_REPLY_MAGIC   = 0x32534d56;
static const uint32 VIDEO_SERVER_V2_VERSION       = 2;
static const uint32 VIDEO_SERVER_V3_REQUEST_MAGIC = 0x33534d56; // "VMS3"
static const uint32 VIDEO_SERVER_V3_REPLY_MAGIC   = 0x33534d56;
static const uint32 VIDEO_SERVER_V3_VERSION       = 3;
static const uint32 VIDEO_SERVER_V3_REVIEW_MAGIC  = 0x33524d56; // "VMR3"

// ---------------------------------------------------------------------------
// Local helpers: uri parsing, payload decode (JPEG/YUV0), Image conversion.
//
// Reconciling the two existing prior-art clients (per Task 0279's spec):
// reference/VideoServerFrameRecorder/main.cpp and
// uppsrc/VideoRecorder/main.cpp's CaptureFrames()/DecodePayloadFrame() are,
// on inspection, the SAME protocol with no real behavioral divergence: both
// send the last-seen id via TcpSocket::Put(), read a 4-byte id + 4-byte size
// via TcpSocket::GetAll() under a configurable per-call Timeout(), treat
// size==0 as "poll again after a short sleep" (never a hard error), and
// decode a JPEG-or-YUV0 payload with byte-identical logic. VideoRecorder's
// version additionally caches the last decoded frame across empty replies
// (has_cached) purely because it paces frame *output* on its own encoder
// fps, not because the wire protocol differs. This class mirrors the
// send-id/read-header/read-payload/decode sequence from both, but:
//   - uses PutAll() instead of Put() for the outgoing 4-byte request, since
//     Put() returns a possibly-partial byte count that both prior tools
//     treat as pass/fail via a truthiness check (harmless for a 4-byte send
//     on a healthy localhost socket, but PutAll() is the objectively
//     correct API for "all-or-nothing", so this class uses it instead of
//     copying the latent partial-send gap forward).
//   - adds real bounded-timeout semantics on top of the raw poll loop (see
//     VsmVideoServerFrameSource::ReadFrame()), since neither prior tool is a
//     library-embeddable continuous source: VideoServerFrameRecorder bounds
//     only by a fixed attempt count, and VideoRecorder bounds by its own
//     fixed-fps encoder pacing loop — neither exposes "wait up to N ms for
//     the next frame, then tell me clearly whether that was a timeout or a
//     real connection loss", which is exactly VsmFrameSource::ReadFrame()'s
//     contract.

static bool ParseHostPort(const String& uri, String& host, int& port)
{
	int colon = -1;
	for(int i = 0; i < uri.GetCount(); i++)
		if(uri[i] == ':')
			colon = i;
	if(colon <= 0 || colon >= uri.GetCount() - 1)
		return false;
	String h = uri.Left(colon);
	String p = uri.Mid(colon + 1);
	for(int i = 0; i < p.GetCount(); i++)
		if(p[i] < '0' || p[i] > '9')
			return false;
	int port_num = StrInt(p);
	if(port_num <= 0 || port_num > 65535)
		return false;
	host = h;
	port = port_num;
	return true;
}

static int ClipByte(int v) { return minmax(v, 0, 255); }

// Byte-for-byte port of reference/VideoServerFrameRecorder/main.cpp's
// DecodeYuv0() (identical to uppsrc/VideoRecorder/main.cpp's copy) — same
// "YUV0" + w + h + byte-count little-endian header, packed 4:2:2 payload.
static bool DecodeYuv0Payload(const String& payload, Image& out, String& error)
{
	if(payload.GetCount() < 16 || payload.Mid(0, 4) != "YUV0") {
		error = "not a YUV0 payload";
		return false;
	}
	const byte* p = (const byte*)~payload;
	uint32 w = Peek32le(p + 4);
	uint32 h = Peek32le(p + 8);
	uint32 bytes = Peek32le(p + 12);
	if(w == 0 || h == 0 || bytes != w * h * 2 || payload.GetCount() < 16 + (int)bytes) {
		error = Format("invalid YUV0 header width=%d height=%d bytes=%d payload=%d",
		               (int)w, (int)h, (int)bytes, payload.GetCount());
		return false;
	}
	ImageBuffer ib((int)w, (int)h);
	const byte* src = p + 16;
	for(int y = 0; y < (int)h; y++) {
		RGBA* dst = ib[y];
		const byte* row = src + y * (int)w * 2;
		for(int x = 0; x < (int)w; x += 2) {
			int y0 = row[x * 2 + 0] - 16;
			int u  = row[x * 2 + 1] - 128;
			int y1 = row[x * 2 + 2] - 16;
			int v  = row[x * 2 + 3] - 128;
			int c0 = max(y0, 0);
			int c1 = max(y1, 0);
			dst[x].r = (byte)ClipByte((298 * c0 + 409 * v + 128) >> 8);
			dst[x].g = (byte)ClipByte((298 * c0 - 100 * u - 208 * v + 128) >> 8);
			dst[x].b = (byte)ClipByte((298 * c0 + 516 * u + 128) >> 8);
			dst[x].a = 255;
			if(x + 1 < (int)w) {
				dst[x + 1].r = (byte)ClipByte((298 * c1 + 409 * v + 128) >> 8);
				dst[x + 1].g = (byte)ClipByte((298 * c1 - 100 * u - 208 * v + 128) >> 8);
				dst[x + 1].b = (byte)ClipByte((298 * c1 + 516 * u + 128) >> 8);
				dst[x + 1].a = 255;
			}
		}
	}
	ib.SetKind(IMAGE_OPAQUE);
	out = ib;
	return true;
}

// Mirrors DecodePayloadFrame() in uppsrc/VideoRecorder/main.cpp: JPEG by
// default, YUV0-prefixed raw payload when the server was started with
// --wire-format yuv.
static bool DecodeVideoServerPayload(const String& payload, Image& out, String& error)
{
	if(payload.GetCount() >= 4 && payload.Mid(0, 4) == "YUV0")
		return DecodeYuv0Payload(payload, out, error);
	out = JPGRaster().LoadString(payload);
	if(out.IsEmpty()) {
		error = Format("payload is not decodable JPEG/YUV0, bytes=%d", payload.GetCount());
		return false;
	}
	return true;
}

// Converts a decoded U++ Image (RGBA struct — platform-dependent in-memory
// byte order, see uppsrc/Core/Color.h) into VsmImageBuffer's canonical
// R,G,B,A-per-pixel byte layout (ImageBuffer.h: "ch == 4 -> RGBA"). This is
// a deliberate per-channel field copy, NOT a raw memcpy of the RGBA struct
// (the struct's in-memory field order is b,g,r,a on little-endian, NOT
// r,g,b,a) — mirrors PngFrame.cpp's VsmLoadPngFrame(), the only other place
// in this package that performs this exact Image -> canonical-RGBA-bytes
// conversion (there into a VsmFrameImage rather than a VsmImageBuffer, but
// confirmed to be the identical target byte layout: LiveSession.cpp copies
// a VsmFrameImage into a VsmImageBuffer via a straight memcpy with no
// reordering).
static void ImageToVsmImageBuffer(const Image& img, VsmImageBuffer& out)
{
	int w = img.GetWidth(), h = img.GetHeight();
	out.Create(w, h, 4);
	const RGBA* src = ~img;
	byte* dst = out.pixels.Begin();
	for(int i = 0; i < w * h; i++) {
		dst[i * 4 + 0] = src[i].r;
		dst[i * 4 + 1] = src[i].g;
		dst[i * 4 + 2] = src[i].b;
		dst[i * 4 + 3] = src[i].a;
	}
}

// ---------------------------------------------------------------------------
// VsmVideoServerFrameSource

int VsmVideoServerFrameSource::PollOnce(VsmImageBuffer& out_frame, uint32& out_id,
	int64& out_video_timestamp_ms, int64& out_video_loop, String& out_detail)
{
	out_video_timestamp_ms = -1;
	out_video_loop = -1;
	if(!socket_.Timeout(socket_op_timeout_ms_).PutAll(&last_id_, 4)) {
		bool timed_out = socket_.IsTimeout();
		out_detail = "send request: " + socket_.GetErrorDesc();
		socket_.ClearError();
		return timed_out ? -1 : -2;
	}
	uint32 frame_id = 0, size = 0;
	if(!socket_.Timeout(socket_op_timeout_ms_).GetAll(&frame_id, 4) ||
	   !socket_.Timeout(socket_op_timeout_ms_).GetAll(&size, 4)) {
		bool timed_out = socket_.IsTimeout();
		out_detail = "read header: " + socket_.GetErrorDesc();
		socket_.ClearError();
		return timed_out ? -1 : -2;
	}
	if(use_v2_) {
		if(!socket_.Timeout(socket_op_timeout_ms_).GetAll(&out_video_timestamp_ms, 8) ||
		   !socket_.Timeout(socket_op_timeout_ms_).GetAll(&out_video_loop, 8)) {
			bool timed_out = socket_.IsTimeout();
			out_detail = "read v2 metadata: " + socket_.GetErrorDesc();
			socket_.ClearError();
			return timed_out ? -1 : -2;
		}
	}
	if(size == 0)
		return 0; // server has nothing newer than last_id_ yet — not an error

	String payload = socket_.Timeout(socket_op_timeout_ms_).GetAll((int)size);
	if(payload.GetCount() != (int)size) {
		bool timed_out = socket_.IsTimeout();
		out_detail = Format("read payload: expected %d bytes, got %d", (int)size, payload.GetCount());
		socket_.ClearError();
		return timed_out ? -1 : -2;
	}

	Image img;
	String decode_error;
	if(!DecodeVideoServerPayload(payload, img, decode_error)) {
		out_detail = "decode id=" + IntStr((int)frame_id) + ": " + decode_error;
		return -1; // recoverable — retry; last_id_ intentionally NOT advanced
	}

	ImageToVsmImageBuffer(img, out_frame);
	out_id = frame_id;
	return 1;
}

bool VsmVideoServerFrameSource::NegotiateProtocolV2()
{
	uint32 magic = VIDEO_SERVER_V2_REQUEST_MAGIC;
	uint32 version = VIDEO_SERVER_V2_VERSION;
	uint32 reply_magic = 0, reply_version = 0;
	if(!socket_.Timeout(socket_op_timeout_ms_).PutAll(&magic, 4) ||
	   !socket_.Timeout(socket_op_timeout_ms_).PutAll(&version, 4) ||
	   !socket_.Timeout(socket_op_timeout_ms_).GetAll(&reply_magic, 4) ||
	   !socket_.Timeout(socket_op_timeout_ms_).GetAll(&reply_version, 4))
		return false;
	return reply_magic == VIDEO_SERVER_V2_REPLY_MAGIC && reply_version == VIDEO_SERVER_V2_VERSION;
}

bool VsmVideoServerFrameSource::RequestTargetedReview(int64 target_video_timestamp_ms,
	int64 target_video_loop, int range_ms, int max_frames, Vector<ReviewFrame>& out_frames)
{
	out_frames.Clear();
	targeted_review_available_ = false;
	if(!is_ready_ || target_video_timestamp_ms < 0 || max_frames <= 0)
		return false;
	TcpSocket review;
	if(!review.Timeout(connect_timeout_ms_).Connect(host_, port_))
		return false;
	uint32 magic = VIDEO_SERVER_V3_REQUEST_MAGIC, version = VIDEO_SERVER_V3_VERSION;
	uint32 reply_magic = 0, reply_version = 0;
	bool negotiated = review.Timeout(socket_op_timeout_ms_).PutAll(&magic, 4)
	               && review.Timeout(socket_op_timeout_ms_).PutAll(&version, 4)
	               && review.Timeout(socket_op_timeout_ms_).GetAll(&reply_magic, 4)
	               && review.Timeout(socket_op_timeout_ms_).GetAll(&reply_version, 4)
	               && reply_magic == VIDEO_SERVER_V3_REPLY_MAGIC
	               && reply_version == VIDEO_SERVER_V3_VERSION;
	if(!negotiated)
		return false;
	targeted_review_available_ = true;
	uint32 request = VIDEO_SERVER_V3_REVIEW_MAGIC;
	uint32 requested_count = (uint32)minmax(max_frames, 1, 16);
	int64 requested_range = max(0, range_ms);
	if(!review.Timeout(socket_op_timeout_ms_).PutAll(&request, 4)
	   || !review.Timeout(socket_op_timeout_ms_).PutAll(&target_video_timestamp_ms, 8)
	   || !review.Timeout(socket_op_timeout_ms_).PutAll(&target_video_loop, 8)
	   || !review.Timeout(socket_op_timeout_ms_).PutAll(&requested_range, 8)
	   || !review.Timeout(socket_op_timeout_ms_).PutAll(&requested_count, 4))
		return false;
	uint32 reply = 0, count = 0;
	if(!review.Timeout(socket_op_timeout_ms_).GetAll(&reply, 4)
	   || !review.Timeout(socket_op_timeout_ms_).GetAll(&count, 4)
	   || reply != VIDEO_SERVER_V3_REVIEW_MAGIC || count > 16)
		return false;
	for(uint32 i = 0; i < count; i++) {
		uint32 id = 0, size = 0;
		int64 pts = -1, loop = -1;
		if(!review.Timeout(socket_op_timeout_ms_).GetAll(&id, 4)
		   || !review.Timeout(socket_op_timeout_ms_).GetAll(&size, 4)
		   || !review.Timeout(socket_op_timeout_ms_).GetAll(&pts, 8)
		   || !review.Timeout(socket_op_timeout_ms_).GetAll(&loop, 8)
		   || size == 0 || size > 128 * 1024 * 1024)
			return false;
		String payload = review.Timeout(socket_op_timeout_ms_).GetAll((int)size);
		if(payload.GetCount() != (int)size)
			return false;
		Image image; String error;
		if(!DecodeVideoServerPayload(payload, image, error))
			return false;
		ReviewFrame& frame = out_frames.Add();
		ImageToVsmImageBuffer(image, frame.image);
		frame.frame_id = id; frame.video_timestamp_ms = pts; frame.video_loop = loop;
	}
	return true;
}

void VsmVideoServerFrameSource::MarkTimeout(const String& detail)
{
	last_error_ = "timeout: " + detail;
	last_error_kind_ = VSM_VSFS_ERR_TIMEOUT;
	LogInfo(log_, "VsmVideoServerFrameSource", last_error_);
}

void VsmVideoServerFrameSource::MarkConnectionLost(const String& detail)
{
	last_error_ = "connection: " + detail;
	last_error_kind_ = VSM_VSFS_ERR_CONNECTION_LOST;
	is_ready_ = false;
	if(socket_.IsOpen())
		socket_.Close();
	LogWarn(log_, "VsmVideoServerFrameSource", last_error_);
}

bool VsmVideoServerFrameSource::Open(const String& uri)
{
	Close();

	String host; int port = 0;
	if(!ParseHostPort(uri, host, port)) {
		last_error_ = "Invalid uri, expected host:port: " + uri;
		last_error_kind_ = VSM_VSFS_ERR_CONNECTION_LOST;
		return false;
	}

	if(!socket_.Timeout(connect_timeout_ms_).Connect(host, port)) {
		last_error_ = "connection: connect failed to " + host + ":" + IntStr(port) +
		              ": " + socket_.GetErrorDesc();
		last_error_kind_ = VSM_VSFS_ERR_CONNECTION_LOST;
		socket_.ClearError();
		return false;
	}
	socket_.ClearError();

	host_        = host;
	port_        = port;
	last_id_     = 0;
	last_video_timestamp_ms_ = -1;
	last_video_loop_ = -1;
	targeted_review_available_ = false;
	has_pending_ = false;
	is_ready_    = true;
	last_error_.Clear();
	last_error_kind_ = VSM_VSFS_ERR_NONE;

	// A legacy server interprets the magic as an impossibly high last id and
	// replies with its normal empty v1 header. Reconnect before v1 polling so
	// no unread probe bytes can corrupt the legacy request stream.
	use_v2_ = NegotiateProtocolV2();
	if(!use_v2_) {
		socket_.Close();
		socket_.ClearError();
		if(!socket_.Timeout(connect_timeout_ms_).Connect(host, port)) {
			last_error_ = "connection: v1 fallback connect failed to " + host + ":" + IntStr(port);
			last_error_kind_ = VSM_VSFS_ERR_CONNECTION_LOST;
			return false;
		}
		socket_.ClearError();
	}

	// Prime: read one real frame so GetWidth()/GetHeight() are correct
	// immediately after Open() returns. Not discarded — handed to the first
	// ReadFrame() call.
	VsmImageBuffer first;
	int64 first_ts = 0;
	if(!ReadFrame(first, first_ts)) {
		String reason = last_error_;
		int    kind   = last_error_kind_;
		Close();
		last_error_ = "Open: no initial frame from " + host + ":" + IntStr(port) + " -- " + reason;
		last_error_kind_ = kind;
		return false;
	}

	width_         = first.width;
	height_        = first.height;
	pending_frame_ = pick(first);
	pending_ts_ms_ = first_ts;
	has_pending_   = true;

	LogInfo(log_, "VsmVideoServerFrameSource",
	        "Opened " + host_ + ":" + IntStr(port_) + " " + IntStr(width_) + "x" + IntStr(height_));
	return true;
}

void VsmVideoServerFrameSource::Close()
{
	if(socket_.IsOpen())
		socket_.Close();
	socket_.ClearError();
	is_ready_    = false;
	has_pending_ = false;
	width_ = height_ = 0;
	last_id_    = 0;
	use_v2_ = false;
	last_video_timestamp_ms_ = -1;
	last_video_loop_ = -1;
	targeted_review_available_ = false;
	last_error_ = String();
	last_error_kind_ = VSM_VSFS_ERR_NONE;
}

bool VsmVideoServerFrameSource::ReadFrame(VsmImageBuffer& out_frame, int64& out_ts_ms)
{
	if(!is_ready_) {
		last_error_ = "Source not open";
		last_error_kind_ = VSM_VSFS_ERR_CONNECTION_LOST;
		return false;
	}

	if(has_pending_) {
		out_frame = pick(pending_frame_);
		out_ts_ms = pending_ts_ms_;
		has_pending_ = false;
		last_error_.Clear();
		last_error_kind_ = VSM_VSFS_ERR_NONE;
		return true;
	}

	int64 deadline = msecs() + wait_timeout_ms_;
	for(;;) {
		uint32 frame_id = 0;
		int64 video_timestamp_ms = -1;
		int64 video_loop = -1;
		String detail;
		// Task 0345: split this call's time into "a frame actually arrived and
		// was decoded" (work) and "the server had nothing newer" (wait), so a
		// caller can report an honest busy/wait ratio for its reader thread.
		int64 poll_started_us = usecs();
		int r = PollOnce(out_frame, frame_id, video_timestamp_ms, video_loop, detail);
		if(r == 1)
			frame_work_us_ += usecs() - poll_started_us;
		else
			poll_wait_us_ += usecs() - poll_started_us;
		if(r == 1) {
			last_id_  = frame_id;
			last_video_timestamp_ms_ = video_timestamp_ms;
			last_video_loop_ = video_loop;
			out_ts_ms = msecs(); // real wall-clock moment this frame was received
			last_error_.Clear();
			last_error_kind_ = VSM_VSFS_ERR_NONE;
			return true;
		}
		if(r == -2) {
			MarkConnectionLost(detail);
			return false;
		}
		// r == 0 (no new frame yet) or r == -1 (recoverable hiccup) — keep
		// polling until the overall wait deadline.
		if(msecs() >= deadline) {
			MarkTimeout(r == 0 ? ("no new frame within " + IntStr(wait_timeout_ms_) + "ms") : detail);
			return false;
		}
		int64 sleep_started_us = usecs();
		Sleep(poll_interval_ms_);
		poll_wait_us_ += usecs() - sleep_started_us;
	}
}

String VsmVideoServerFrameSource::GetSourceInfo() const
{
	if(!is_ready_)
		return "VsmVideoServerFrameSource (not open)";
	return "videoserver:" + host_ + ":" + IntStr(port_) +
	       " " + IntStr(width_) + "x" + IntStr(height_) +
	       " last_id=" + IntStr((int)last_id_);
}

} // namespace Upp
