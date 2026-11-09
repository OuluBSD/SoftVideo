#ifndef _VisualStateModel_VideoFileFrameSource_h_
#define _VisualStateModel_VideoFileFrameSource_h_

namespace Upp {

class VsmVideoFileFrameSource : public VsmFrameSource {
public:
	VsmVideoFileFrameSource();
	~VsmVideoFileFrameSource();

	void SetLooping(bool looping)                  { looping_ = looping; }
	void SetFfmpegDllDirectory(const String& path) { ffmpeg_dll_directory_ = path; }

	bool   Open(const String& uri) override;
	void   Close() override;
	bool   IsReady()  const override { return is_ready_; }
	int    GetWidth() const override { return width_; }
	int    GetHeight()const override { return height_; }
	int    GetFPS()   const override { return fps_; }
	bool   ReadFrame(VsmImageBuffer& out_frame, int64& out_ts_ms) override;
	bool   SupportsFrameImage() const override { return true; }
	bool   ReadFrameImage(Image& out_image, int64& out_ts_ms) override { return ReadImage(out_image, out_ts_ms); }
	bool   SeekToMs(int64 pts_ms) override               { return SeekMs(pts_ms); }

	bool   ReadImage(Image& out_image, int64& out_ts_ms);
	bool   NextFrame(Image& out_image, String& error);
	bool   SeekMs(int64 timestamp_ms);
	bool   IsEof() const { return eof_; }

	String GetLastError()  const override { return last_error_; }
	String GetSourceInfo() const override;

	int64  GetDecodedFrameCount() const { return decoded_frames_; }
	int64  GetLoopCount() const          { return loops_; }
	int64  GetLastPtsMs() const          { return last_pts_ms_; }
	int64  GetDurationMs() const         { return duration_ms_; }
	double LastPtsSeconds() const;

private:
	struct Data;
	One<Data> data_;
	String    uri_;
	String    ffmpeg_dll_directory_;
	String    last_error_;
	int       width_ = 0;
	int       height_ = 0;
	int       fps_ = 0;
	int64     duration_ms_ = -1;
	int64     decoded_frames_ = 0;
	int64     loops_ = 0;
	int64     last_pts_ms_ = -1;
	bool      looping_ = false;
	bool      is_ready_ = false;
	bool      eof_ = false;
};

} // namespace Upp

#endif
