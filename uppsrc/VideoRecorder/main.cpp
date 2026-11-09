#include "VideoRecorder.h"

#ifdef flagWIN32
#define CY win32_CY_
#define FAR win32_FAR_
#include <windows.h>
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#ifdef flagWIN32
#undef CY
#undef FAR
#endif

NAMESPACE_UPP

static String GetDefaultFfmpegDllDirectory()
{
#ifdef flagWIN32
	return AppendFileName(AppendFileName(AppendFileName(AppendFileName(GetHomeDirectory(), "vcpkg"),
	                                                   "installed"),
	                                    "x64-windows"),
	                      "bin");
#else
	return Null;
#endif
}

static bool PrepareFfmpegRuntime(const VideoRecorderOptions& opt, String& error)
{
#ifdef flagWIN32
	String dir = opt.ffmpeg_dll_dir.IsEmpty() ? GetDefaultFfmpegDllDirectory() : opt.ffmpeg_dll_dir;
	if(!DirectoryExists(dir)) {
		error = "FFmpeg DLL directory does not exist: " + dir;
		return false;
	}
	if(!SetDllDirectoryA(~dir)) {
		error = "SetDllDirectoryA failed for: " + dir;
		return false;
	}
#endif
	return true;
}

static void PrintHelp()
{
	Cout() << "VideoRecorder\n\n"
	       << "Usage: VideoRecorder [options]\n\n"
	       << "Options:\n"
	       << "  --host <host>       VideoServer host (default 127.0.0.1)\n"
	       << "  --port <port>       VideoServer port (default 8082)\n"
	       << "  --seconds <n>       Recording duration in seconds (default 60)\n"
	       << "  --minutes <n>       Recording duration in minutes\n"
	       << "  --fps <n>           Target capture FPS hint (default 10)\n"
	       << "  --out <file>        Output video path (default bin/video_record_<timestamp>.mp4)\n"
	       << "  --work-dir <dir>    Diagnostic frame dump directory\n"
	       << "  --codec <name>      libavcodec encoder (default mpeg4; libx264 if available)\n"
	       << "  --bitrate <n>       Video bitrate in bits/s (default 4000000)\n"
	       << "  --dump-frames       Also dump decoded frames as JPEG diagnostics\n"
	       << "  --ffmpeg-dll-dir <dir>\n"
	       << "                      Windows FFmpeg DLL dir (default " << GetDefaultFfmpegDllDirectory() << ")\n"
	       << "  --help, -h          Show help\n";
}

static VideoRecorderOptions ParseOptions(const Vector<String>& args)
{
	VideoRecorderOptions opt;
	for(int i = 0; i < args.GetCount(); i++) {
		if(args[i] == "--host" && i + 1 < args.GetCount())
			opt.host = args[++i];
		else if(args[i] == "--port" && i + 1 < args.GetCount())
			opt.port = StrInt(args[++i]);
		else if(args[i] == "--seconds" && i + 1 < args.GetCount())
			opt.seconds = max(1, StrInt(args[++i]));
		else if(args[i] == "--minutes" && i + 1 < args.GetCount())
			opt.seconds = max(1, StrInt(args[++i])) * 60;
		else if(args[i] == "--fps" && i + 1 < args.GetCount())
			opt.fps = max(1, StrInt(args[++i]));
		else if(args[i] == "--poll-ms" && i + 1 < args.GetCount())
			opt.poll_ms = max(1, StrInt(args[++i]));
		else if(args[i] == "--timeout-ms" && i + 1 < args.GetCount())
			opt.timeout_ms = max(100, StrInt(args[++i]));
		else if(args[i] == "--out" && i + 1 < args.GetCount())
			opt.out_path = args[++i];
		else if(args[i] == "--work-dir" && i + 1 < args.GetCount())
			opt.work_dir = args[++i];
		else if(args[i] == "--codec" && i + 1 < args.GetCount())
			opt.codec = args[++i];
		else if(args[i] == "--bitrate" && i + 1 < args.GetCount())
			opt.bitrate = max(100000, StrInt(args[++i]));
		else if(args[i] == "--ffmpeg-dll-dir" && i + 1 < args.GetCount())
			opt.ffmpeg_dll_dir = args[++i];
		else if(args[i] == "--dump-frames")
			opt.dump_frames = true;
		else if(args[i] == "--help" || args[i] == "-h")
			opt.help = true;
	}
	Time now = GetSysTime();
	String stamp = Format("%04d%02d%02d_%02d%02d%02d",
	                      now.year, now.month, now.day, now.hour, now.minute, now.second);
	if(opt.out_path.IsEmpty())
		opt.out_path = AppendFileName(GetCurrentDirectory(),
		                               AppendFileName("bin", "video_record_" + stamp + ".mp4"));
	if(opt.work_dir.IsEmpty())
		opt.work_dir = AppendFileName(GetCurrentDirectory(),
		                               AppendFileName("tmp", "video_record_frames_" + stamp));
	return opt;
}

static int ClipByte(int v)
{
	return minmax(v, 0, 255);
}

static bool DecodeYuv0(const String& payload, Image& out, String& error)
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

static bool DecodePayloadFrame(const String& payload, Image& out, Size& out_size,
                               String& out_format, String& error)
{
	if(payload.GetCount() >= 4 && payload.Mid(0, 4) == "YUV0") {
		if(!DecodeYuv0(payload, out, error))
			return false;
		out_size = out.GetSize();
		out_format = "YUV0";
		return true;
	}
	out = JPGRaster().LoadString(payload);
	if(out.IsEmpty()) {
		error = Format("payload is not decodable JPEG/YUV0, bytes=%d", payload.GetCount());
		return false;
	}
	out_size = out.GetSize();
	out_format = "JPEG";
	return true;
}

static String AvError(int code)
{
	char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(code, buffer, sizeof(buffer));
	return buffer;
}

struct DirectMp4Writer {
	AVFormatContext *format = nullptr;
	AVCodecContext  *codec = nullptr;
	AVStream        *stream = nullptr;
	AVFrame         *frame = nullptr;
	SwsContext      *sws = nullptr;
	Size             size;
	String           codec_name;
	int64            frames = 0;
	bool             opened = false;

	~DirectMp4Writer()
	{
		Close();
	}

	bool Open(const VideoRecorderOptions& opt, Size frame_size, String& error)
	{
		size = frame_size;
		codec_name = opt.codec;
		int rc = avformat_alloc_output_context2(&format, nullptr, "mp4", ~opt.out_path);
		if(rc < 0 || !format) {
			error = "avformat_alloc_output_context2 failed: " + AvError(rc);
			return false;
		}

		const AVCodec *encoder = avcodec_find_encoder_by_name(~opt.codec);
		if(!encoder) {
			error = "encoder not found: " + opt.codec;
			return false;
		}
		stream = avformat_new_stream(format, nullptr);
		if(!stream) {
			error = "avformat_new_stream failed";
			return false;
		}
		codec = avcodec_alloc_context3(encoder);
		if(!codec) {
			error = "avcodec_alloc_context3 failed";
			return false;
		}
		codec->codec_id = encoder->id;
		codec->codec_type = AVMEDIA_TYPE_VIDEO;
		codec->width = size.cx;
		codec->height = size.cy;
		codec->time_base = AVRational{1, 1000};
		codec->framerate = AVRational{opt.fps, 1};
		codec->pix_fmt = AV_PIX_FMT_YUV420P;
		codec->bit_rate = opt.bitrate;
		codec->gop_size = max(1, opt.fps * 2);
		codec->max_b_frames = 0;
		stream->time_base = codec->time_base;
		if(format->oformat->flags & AVFMT_GLOBALHEADER)
			codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		if(codec->codec_id == AV_CODEC_ID_H264)
			av_opt_set(codec->priv_data, "preset", "veryfast", 0);

		rc = avcodec_open2(codec, encoder, nullptr);
		if(rc < 0) {
			error = "avcodec_open2 failed for " + opt.codec + ": " + AvError(rc);
			return false;
		}
		rc = avcodec_parameters_from_context(stream->codecpar, codec);
		if(rc < 0) {
			error = "avcodec_parameters_from_context failed: " + AvError(rc);
			return false;
		}
		rc = avio_open(&format->pb, ~opt.out_path, AVIO_FLAG_WRITE);
		if(rc < 0) {
			error = "avio_open failed for " + opt.out_path + ": " + AvError(rc);
			return false;
		}
		rc = avformat_write_header(format, nullptr);
		if(rc < 0) {
			error = "avformat_write_header failed: " + AvError(rc);
			return false;
		}

		frame = av_frame_alloc();
		if(!frame) {
			error = "av_frame_alloc failed";
			return false;
		}
		frame->format = codec->pix_fmt;
		frame->width = codec->width;
		frame->height = codec->height;
		rc = av_frame_get_buffer(frame, 32);
		if(rc < 0) {
			error = "av_frame_get_buffer failed: " + AvError(rc);
			return false;
		}
		sws = sws_getContext(size.cx, size.cy, AV_PIX_FMT_BGRA,
		                     size.cx, size.cy, codec->pix_fmt,
		                     SWS_BILINEAR, nullptr, nullptr, nullptr);
		if(!sws) {
			error = "sws_getContext failed";
			return false;
		}
		opened = true;
		return true;
	}

	bool Write(const Image& image, int64 pts_ms, String& error)
	{
		if(!opened) {
			error = "writer is not open";
			return false;
		}
		if(image.GetSize() != size) {
			error = Format("frame size changed from %d`x%d to %d`x%d",
			               size.cx, size.cy, image.GetWidth(), image.GetHeight());
			return false;
		}
		int rc = av_frame_make_writable(frame);
		if(rc < 0) {
			error = "av_frame_make_writable failed: " + AvError(rc);
			return false;
		}
		const byte *src_data[4] = {(const byte*)~image, nullptr, nullptr, nullptr};
		int src_linesize[4] = {image.GetWidth() * (int)sizeof(RGBA), 0, 0, 0};
		sws_scale(sws, src_data, src_linesize, 0, size.cy, frame->data, frame->linesize);
		frame->pts = pts_ms;
		return SendFrame(frame, error);
	}

	bool SendFrame(AVFrame *input, String& error)
	{
		int rc = avcodec_send_frame(codec, input);
		if(rc < 0) {
			error = "avcodec_send_frame failed: " + AvError(rc);
			return false;
		}
		for(;;) {
			AVPacket *packet = av_packet_alloc();
			if(!packet) {
				error = "av_packet_alloc failed";
				return false;
			}
			rc = avcodec_receive_packet(codec, packet);
			if(rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
				av_packet_free(&packet);
				break;
			}
			if(rc < 0) {
				error = "avcodec_receive_packet failed: " + AvError(rc);
				av_packet_free(&packet);
				return false;
			}
			av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
			packet->stream_index = stream->index;
			rc = av_interleaved_write_frame(format, packet);
			av_packet_free(&packet);
			if(rc < 0) {
				error = "av_interleaved_write_frame failed: " + AvError(rc);
				return false;
			}
			frames++;
		}
		return true;
	}

	bool Finish(String& error)
	{
		if(!opened)
			return true;
		if(!SendFrame(nullptr, error))
			return false;
		int rc = av_write_trailer(format);
		if(rc < 0) {
			error = "av_write_trailer failed: " + AvError(rc);
			return false;
		}
		opened = false;
		return true;
	}

	void Close()
	{
		if(sws) {
			sws_freeContext(sws);
			sws = nullptr;
		}
		if(frame) {
			av_frame_free(&frame);
			frame = nullptr;
		}
		if(codec) {
			avcodec_free_context(&codec);
			codec = nullptr;
		}
		if(format) {
			if(format->pb)
				avio_closep(&format->pb);
			avformat_free_context(format);
			format = nullptr;
		}
		opened = false;
	}
};

static String JsonString(const String& s)
{
	String out;
	for(int i = 0; i < s.GetCount(); i++) {
		byte c = s[i];
		if(c == '\\')
			out << "\\\\";
		else if(c == '"')
			out << "\\\"";
		else if(c == '\n')
			out << "\\n";
		else if(c == '\r')
			out << "\\r";
		else if(c == '\t')
			out << "\\t";
		else
			out.Cat(c);
	}
	return out;
}

static String ManifestJson(const VideoRecorderOptions& opt, const Vector<RecordedFrame>& frames,
                           const DirectMp4Writer& writer)
{
	String out;
	out << "{\n";
	out << "  \"tool\": \"VideoRecorder\",\n";
	out << "  \"backend\": \"libavcodec/libavformat\",\n";
	out << "  \"host\": \"" << JsonString(opt.host) << "\",\n";
	out << "  \"port\": " << opt.port << ",\n";
	out << "  \"seconds_requested\": " << opt.seconds << ",\n";
	out << "  \"fps\": " << opt.fps << ",\n";
	out << "  \"frames_captured\": " << frames.GetCount() << ",\n";
	out << "  \"packets_written\": " << writer.frames << ",\n";
	out << "  \"output_video\": \"" << JsonString(opt.out_path) << "\",\n";
	out << "  \"work_dir\": \"" << JsonString(opt.work_dir) << "\",\n";
	out << "  \"dump_frames\": " << (opt.dump_frames ? "true" : "false") << ",\n";
	out << "  \"codec\": \"" << JsonString(opt.codec) << "\",\n";
	out << "  \"ffmpeg_dll_dir\": \"" << JsonString(opt.ffmpeg_dll_dir.IsEmpty() ? GetDefaultFfmpegDllDirectory() : opt.ffmpeg_dll_dir) << "\",\n";
	out << "  \"pixel_format\": \"" << JsonString(opt.pix_fmt) << "\",\n";
	out << "  \"bitrate\": " << opt.bitrate << ",\n";
	out << "  \"frames\": [\n";
	for(int i = 0; i < frames.GetCount(); i++) {
		const RecordedFrame& f = frames[i];
		out << "    {\"index\": " << f.index
		    << ", \"id\": " << f.id
		    << ", \"width\": " << f.size.cx
		    << ", \"height\": " << f.size.cy
		    << ", \"format\": \"" << JsonString(f.format)
		    << "\", \"elapsed_ms\": " << f.elapsed_ms
		    << ", \"dump_path\": \"" << JsonString(f.dump_path) << "\"}";
		if(i + 1 < frames.GetCount())
			out << ",";
		out << "\n";
	}
	out << "  ]\n";
	out << "}\n";
	return out;
}

static bool CaptureFrames(const VideoRecorderOptions& opt, Vector<RecordedFrame>& frames,
                          DirectMp4Writer& writer)
{
	TcpSocket socket;
	if(!socket.Timeout(opt.timeout_ms).Connect(opt.host, opt.port)) {
		Cerr() << "ERROR: failed to connect VideoServer at " << opt.host << ":" << opt.port << "\n";
		return false;
	}
	uint32 last_id = 0;
	int64 started = msecs();
	int64 next_capture = started;
	int64 deadline = started + (int64)opt.seconds * 1000;
	int64 first_frame_ms = -1;
	int empty_replies = 0;
	Image cached_image;
	Size cached_size;
	String cached_format;
	uint32 cached_id = 0;
	bool has_cached = false;
	Cout() << "recording_mp4_direct seconds=" << opt.seconds
	       << " fps=" << opt.fps << " out=" << opt.out_path
	       << " codec=" << opt.codec << "\n";
	while(msecs() < deadline) {
		int64 now = msecs();
		if(now < next_capture) {
			Sleep((int)min<int64>(opt.poll_ms, next_capture - now));
			continue;
		}
		next_capture = now + max<int64>(1, 1000 / max(1, opt.fps));
		if(!socket.Timeout(opt.timeout_ms).Put(&last_id, 4)) {
			Cerr() << "ERROR: failed to send last frame id\n";
			return false;
		}
		uint32 frame_id = 0;
		uint32 size = 0;
		if(!socket.Timeout(opt.timeout_ms).GetAll(&frame_id, 4) ||
		   !socket.Timeout(opt.timeout_ms).GetAll(&size, 4)) {
			Cerr() << "ERROR: failed to read frame header\n";
			return false;
		}
		if(size == 0 && !has_cached) {
			empty_replies++;
			Sleep(opt.poll_ms);
			continue;
		}
		if(size > 0) {
			String payload = socket.Timeout(opt.timeout_ms).GetAll((int)size);
			if(payload.GetCount() != (int)size) {
				Cerr() << "ERROR: failed to read payload expected=" << size
				       << " got=" << payload.GetCount() << "\n";
				return false;
			}
			String error;
			if(!DecodePayloadFrame(payload, cached_image, cached_size, cached_format, error)) {
				Cerr() << "ERROR: " << error << "\n";
				return false;
			}
			cached_id = frame_id;
			has_cached = true;
		}
		else
			empty_replies++;
		int64 elapsed_ms = max<int64>(0, msecs() - started);
		if(first_frame_ms < 0)
			first_frame_ms = elapsed_ms;
		int64 pts_ms = elapsed_ms - first_frame_ms;
		if(frames.IsEmpty()) {
			String error;
			if(!writer.Open(opt, cached_size, error)) {
				Cerr() << "ERROR: " << error << "\n";
				return false;
			}
			Cout() << "encoder_opened backend=libavcodec codec=" << opt.codec
			       << " size=" << cached_size.cx << "`x" << cached_size.cy
			       << " source_pix_fmt=bgra"
			       << " output_pix_fmt=" << opt.pix_fmt << "\n";
		}
		String error;
		if(!writer.Write(cached_image, pts_ms, error)) {
			Cerr() << "ERROR: " << error << "\n";
			return false;
		}
		RecordedFrame& frame = frames.Add();
		frame.index = frames.GetCount() - 1;
		frame.id = cached_id;
		frame.size = cached_size;
		frame.format = cached_format;
		frame.elapsed_ms = pts_ms;
		if(opt.dump_frames) {
			RealizeDirectory(opt.work_dir);
			frame.dump_path = AppendFileName(opt.work_dir, Format("frame_%06d.jpg", frame.index));
			if(!JPGEncoder().Quality(95).SaveFile(frame.dump_path, cached_image)) {
				Cerr() << "ERROR: failed to save diagnostic frame: " << frame.dump_path << "\n";
				return false;
			}
		}
		last_id = cached_id;
		if(frame.index == 0 || (frame.index + 1) % max(1, opt.fps * 5) == 0) {
			int elapsed = (int)((msecs() - started) / 1000);
			Cout() << "progress encoded_frames=" << frames.GetCount()
			       << " elapsed=" << elapsed << "s"
			       << " empty_replies=" << empty_replies << "\n";
		}
	}
	if(frames.IsEmpty()) {
		Cerr() << "ERROR: no frames captured\n";
		return false;
	}
	String error;
	if(!writer.Finish(error)) {
		Cerr() << "ERROR: " << error << "\n";
		return false;
	}
	return true;
}

END_UPP_NAMESPACE

using namespace Upp;

CONSOLE_APP_MAIN
{
	VideoRecorderOptions opt = ParseOptions(CommandLine());
	if(opt.help) {
		PrintHelp();
		return;
	}

	RealizeDirectory(GetFileDirectory(opt.out_path));
	String runtime_error;
	if(!PrepareFfmpegRuntime(opt, runtime_error)) {
		Cerr() << "ERROR: " << runtime_error << "\n";
		SetExitCode(1);
		return;
	}
	Vector<RecordedFrame> frames;
	DirectMp4Writer writer;
	if(!CaptureFrames(opt, frames, writer)) {
		SetExitCode(1);
		return;
	}
	String manifest_path = opt.out_path + ".json";
	SaveFile(manifest_path, ManifestJson(opt, frames, writer));
	Cout() << "manifest=" << manifest_path << "\n";
	if(!FileExists(opt.out_path)) {
		Cerr() << "ERROR: output video missing: " << opt.out_path << "\n";
		SetExitCode(1);
		return;
	}
	Cout() << "video_recording_done backend=libavcodec output=" << opt.out_path
	       << " frames=" << frames.GetCount()
	       << " packets=" << writer.frames << "\n";
}
