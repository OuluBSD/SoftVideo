#include "VideoCore.h"

#if defined(flagFFMPEG) || defined(_WIN32)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}
#endif

namespace Upp {

#if defined(flagFFMPEG) || defined(_WIN32)

struct VsmVideoFileFrameSource::Data {
	AVFormatContext* fmt_ctx = nullptr;
	AVCodecContext* codec_ctx = nullptr;
	AVFrame* frame = nullptr;
	AVFrame* frame_rgb = nullptr;
	SwsContext* sws_ctx = nullptr;
	int video_stream_idx = -1;
	uint8_t* buffer = nullptr;

	~Data() { Cleanup(); }

	void Cleanup() {
		if(sws_ctx) { sws_freeContext(sws_ctx); sws_ctx = nullptr; }
		if(buffer) { av_free(buffer); buffer = nullptr; }
		if(frame_rgb) { av_frame_free(&frame_rgb); frame_rgb = nullptr; }
		if(frame) { av_frame_free(&frame); frame = nullptr; }
		if(codec_ctx) { avcodec_free_context(&codec_ctx); codec_ctx = nullptr; }
		if(fmt_ctx) { avformat_close_input(&fmt_ctx); fmt_ctx = nullptr; }
		video_stream_idx = -1;
	}
};

VsmVideoFileFrameSource::VsmVideoFileFrameSource() {}

VsmVideoFileFrameSource::~VsmVideoFileFrameSource() {
	Close();
}

void VsmVideoFileFrameSource::Close() {
	if(data_) data_.Clear();
	is_ready_ = false;
	eof_ = false;
}

bool VsmVideoFileFrameSource::Open(const String& uri) {
	Close();
	if(uri.IsEmpty()) { last_error_ = "empty video URI"; return false; }
	uri_ = uri;
	data_.Create();

	if(avformat_open_input(&data_->fmt_ctx, uri, nullptr, nullptr) != 0) {
		last_error_ = "cannot open video file: " + uri;
		Close();
		return false;
	}

	if(avformat_find_stream_info(data_->fmt_ctx, nullptr) < 0) {
		last_error_ = "cannot find stream info for: " + uri;
		Close();
		return false;
	}

	data_->video_stream_idx = -1;
	for(unsigned int i = 0; i < data_->fmt_ctx->nb_streams; i++) {
		if(data_->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			data_->video_stream_idx = i;
			break;
		}
	}

	if(data_->video_stream_idx < 0) {
		last_error_ = "no video stream found in: " + uri;
		Close();
		return false;
	}

	AVCodecParameters* codecpar = data_->fmt_ctx->streams[data_->video_stream_idx]->codecpar;
	const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
	if(!codec) {
		last_error_ = "codec not found for video stream";
		Close();
		return false;
	}

	data_->codec_ctx = avcodec_alloc_context3(codec);
	if(!data_->codec_ctx || avcodec_parameters_to_context(data_->codec_ctx, codecpar) < 0 || avcodec_open2(data_->codec_ctx, codec, nullptr) < 0) {
		last_error_ = "cannot open codec for video stream";
		Close();
		return false;
	}

	data_->frame = av_frame_alloc();
	data_->frame_rgb = av_frame_alloc();
	if(!data_->frame || !data_->frame_rgb) {
		last_error_ = "cannot allocate video frames";
		Close();
		return false;
	}

	width_ = data_->codec_ctx->width;
	height_ = data_->codec_ctx->height;
	AVRational r_frame_rate = data_->fmt_ctx->streams[data_->video_stream_idx]->r_frame_rate;
	fps_ = r_frame_rate.den > 0 ? (int)(r_frame_rate.num / r_frame_rate.den) : 30;
	if(data_->fmt_ctx->duration != AV_NOPTS_VALUE) {
		duration_ms_ = data_->fmt_ctx->duration / (AV_TIME_BASE / 1000);
	}

	int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width_, height_, 1);
	data_->buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t));
	av_image_fill_arrays(data_->frame_rgb->data, data_->frame_rgb->linesize, data_->buffer, AV_PIX_FMT_RGBA, width_, height_, 1);

	data_->sws_ctx = sws_getContext(
		width_, height_, data_->codec_ctx->pix_fmt,
		width_, height_, AV_PIX_FMT_RGBA,
		SWS_BILINEAR, nullptr, nullptr, nullptr
	);

	if(!data_->sws_ctx) {
		last_error_ = "cannot initialize SwsContext";
		Close();
		return false;
	}

	// NOTE: the decode must stay on the swscale defaults (BT.601, limited
	// range) because the template assets and the hero-card NN models in the
	// catalog were extracted/trained from exactly that decode. Changing the
	// colorspace here desyncs every visual matcher from its training data.

	is_ready_ = true;
	return true;
}

bool VsmVideoFileFrameSource::NextFrame(Image& out_image, String& error) {
	error.Clear();
	if(!data_ || !data_->fmt_ctx || !data_->codec_ctx) {
		error = "video decoder not open";
		return false;
	}

	AVPacket packet;
	bool frame_decoded = false;

	while(!frame_decoded) {
		int ret = av_read_frame(data_->fmt_ctx, &packet);
		if(ret < 0) {
			if(looping_) {
				av_seek_frame(data_->fmt_ctx, data_->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
				avcodec_flush_buffers(data_->codec_ctx);
				loops_++;
				ret = av_read_frame(data_->fmt_ctx, &packet);
			}
			if(ret < 0) {
				eof_ = true;
				error = "end of video stream";
				return false;
			}
		}

		if(packet.stream_index == data_->video_stream_idx) {
			ret = avcodec_send_packet(data_->codec_ctx, &packet);
			if(ret >= 0) {
				ret = avcodec_receive_frame(data_->codec_ctx, data_->frame);
				if(ret == 0) {
					frame_decoded = true;
					sws_scale(data_->sws_ctx, (const uint8_t* const*)data_->frame->data,
					          data_->frame->linesize, 0, height_,
					          data_->frame_rgb->data, data_->frame_rgb->linesize);

					ImageBuffer ib(width_, height_);
					for(int y = 0; y < height_; y++) {
						RGBA* dest = ib[y];
						const uint8_t* src = data_->frame_rgb->data[0] + y * data_->frame_rgb->linesize[0];
						for(int x = 0; x < width_; x++) {
							dest[x].r = src[x * 4 + 0];
							dest[x].g = src[x * 4 + 1];
							dest[x].b = src[x * 4 + 2];
							dest[x].a = 255;
						}
					}
					out_image = ib;

					AVRational tb = data_->fmt_ctx->streams[data_->video_stream_idx]->time_base;
					int64_t pts = data_->frame->best_effort_timestamp;
					if(pts != AV_NOPTS_VALUE) {
						last_pts_ms_ = (int64_t)(pts * av_q2d(tb) * 1000.0);
					}
					decoded_frames_++;
				}
			}
		}
		av_packet_unref(&packet);
	}

	return true;
}

bool VsmVideoFileFrameSource::ReadFrame(VsmImageBuffer& out_frame, int64& out_ts_ms) {
	Image img;
	String error;
	if(!NextFrame(img, error)) return false;
	out_frame.Create(width_, height_, 4);
	if(data_ && data_->frame_rgb && data_->frame_rgb->data[0]) {
		memcpy(out_frame.pixels.Begin(), data_->frame_rgb->data[0], (size_t)width_ * height_ * 4);
	}
	out_ts_ms = last_pts_ms_;
	return true;
}

bool VsmVideoFileFrameSource::ReadImage(Image& out_image, int64& out_ts_ms) {
	String error;
	if(!NextFrame(out_image, error)) return false;
	out_ts_ms = last_pts_ms_;
	return true;
}

bool VsmVideoFileFrameSource::SeekMs(int64 timestamp_ms) {
	if(!data_ || !data_->fmt_ctx) return false;
	// CAUTION (decode-path provenance): with open-GOP encodings the frames
	// decoded after this seek differ from the sequential from-zero decode of
	// the same PTS (the seek target is a non-IDR "keyframe" whose references
	// cross the flushed decode point; measured drift up to 14 luma levels on
	// a 14x14 screen-recording glyph). Template assets, NN training corpora
	// and dumps must come from the sequential decode; never mix a seek-based
	// span extraction with a from-zero training space.
	AVRational tb = data_->fmt_ctx->streams[data_->video_stream_idx]->time_base;
	int64_t seek_target = (int64_t)(timestamp_ms / (av_q2d(tb) * 1000.0));
	if(av_seek_frame(data_->fmt_ctx, data_->video_stream_idx, seek_target, AVSEEK_FLAG_BACKWARD) < 0) return false;
	avcodec_flush_buffers(data_->codec_ctx);
	last_pts_ms_ = timestamp_ms;
	return true;
}

String VsmVideoFileFrameSource::GetSourceInfo() const {
	return "VsmVideoFileFrameSource(FFmpeg)";
}

double VsmVideoFileFrameSource::LastPtsSeconds() const {
	return (double)last_pts_ms_ / 1000.0;
}

#else

struct VsmVideoFileFrameSource::Data {};

VsmVideoFileFrameSource::VsmVideoFileFrameSource() {}
VsmVideoFileFrameSource::~VsmVideoFileFrameSource() {}
void VsmVideoFileFrameSource::Close() {}
bool VsmVideoFileFrameSource::Open(const String& uri) { last_error_ = "FFMPEG support not enabled in this build"; return false; }
bool VsmVideoFileFrameSource::ReadFrame(VsmImageBuffer& out_frame, int64& out_ts_ms) { last_error_ = "FFMPEG support not enabled in this build"; return false; }
bool VsmVideoFileFrameSource::ReadImage(Image& out_image, int64& out_ts_ms) { last_error_ = "FFMPEG support not enabled in this build"; return false; }
bool VsmVideoFileFrameSource::NextFrame(Image& out_image, String& error) { error = "FFMPEG support not enabled in this build"; return false; }
bool VsmVideoFileFrameSource::SeekMs(int64 timestamp_ms) { last_error_ = "FFMPEG support not enabled in this build"; return false; }
String VsmVideoFileFrameSource::GetSourceInfo() const { return "VsmVideoFileFrameSource(disabled)"; }
double VsmVideoFileFrameSource::LastPtsSeconds() const { return (double)last_pts_ms_ / 1000.0; }

#endif

} // namespace Upp
