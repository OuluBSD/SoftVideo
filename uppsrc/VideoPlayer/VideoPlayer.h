#ifndef _VideoPlayer_VideoPlayer_h_
#define _VideoPlayer_VideoPlayer_h_

#include <atomic>
#include <CtrlLib/CtrlLib.h>
#include <VideoCore/VideoCore.h>

NAMESPACE_UPP

class VideoPlayerWindow : public TopWindow {
public:
	typedef VideoPlayerWindow CLASSNAME;

	VideoPlayerWindow();
	~VideoPlayerWindow();

	void Paint(Draw& w) override;
	void LeftDown(Point p, dword flags) override;
	void LeftUp(Point p, dword flags) override;
	void MouseMove(Point p, dword flags) override;
	bool Key(dword key, int count) override;
	void RightDown(Point p, dword flags) override;
	void Close() override;

private:
	struct HudPanel : public ParentCtrl {
		typedef HudPanel CLASSNAME;

		EditString host_port;
		Button     reconnect;
		Button     close_btn;
		String     stats_str;

		HudPanel();
		void Layout() override;
		void Paint(Draw& w) override;
	};

	void ThreadLoop();
	void StartThread();
	void StopThread();
	void Connect(const String& new_uri);
	void UpdateStats(const String& stats);
	void OnNewFrame(const Image& img, int64 latency);

	VsmVideoServerFrameSource frame_source_;
	LocalProcess     server_process_;
	Thread           bg_thread_;
	std::atomic<bool> thread_should_stop_{false};
	std::atomic<bool> is_connected_{false};
	String           uri_;

	Mutex            frame_lock_;
	Image            next_image_;
	int64            next_latency_ = 0;
	bool             has_new_frame_ = false;

	Image            current_frame_;
	Vector<int64>    frame_times_;
	double           current_fps_ = 0.0;
	int64            current_latency_ = 0;
	String           connection_status_;

	HudPanel         hud_panel_;
	bool             hud_visible_ = false;
	Point            drag_start_;
	bool             is_dragging_ = false;

	void ShowHud();
	void HideHud();
};

END_UPP_NAMESPACE

#endif
