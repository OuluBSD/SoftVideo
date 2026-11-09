#include "VideoPlayer.h"

NAMESPACE_UPP

// Diagnostic-only inverse of VideoServerFrameSource.cpp's ImageToVsmImageBuffer()
static Image VsmImageBufferToImage(const VsmImageBuffer& b)
{
	ImageBuffer ib(b.width, b.height);
	for(int y = 0; y < b.height; y++) {
		RGBA* row = ib[y];
		for(int x = 0; x < b.width; x++) {
			row[x].r = b.Get(x, y, 0);
			row[x].g = b.Get(x, y, 1);
			row[x].b = b.Get(x, y, 2);
			row[x].a = b.channels > 3 ? b.Get(x, y, 3) : 255;
		}
	}
	ib.SetKind(IMAGE_OPAQUE);
	return Image(ib);
}

VideoPlayerWindow::HudPanel::HudPanel()
{
	Add(host_port);
	Add(reconnect);
	Add(close_btn);

	host_port.SetBackground(Color(45, 45, 45));
	host_port.SetColor(Color(230, 230, 230));
	host_port.SetFont(StdFont(11));
	host_port.NullText("host:port", Color(120, 120, 120));

	reconnect.SetLabel("Reconnect");
	close_btn.SetLabel("X");
}

void VideoPlayerWindow::HudPanel::Layout()
{
	Size sz = GetSize();
	int input_w = 160;
	int btn_w = 80;
	int close_w = 24;
	int h = 24;
	int y = (sz.cy - h) / 2;
	host_port.LeftPos(10, input_w).TopPos(y, h);
	reconnect.LeftPos(20 + input_w, btn_w).TopPos(y, h);
	close_btn.RightPos(10, close_w).TopPos(y, h);
}

void VideoPlayerWindow::HudPanel::Paint(Draw& w)
{
	Size sz = GetSize();
	
	// Draw semi-transparent background
	ImageBuffer ib(1, 1);
	RGBA rgba;
	rgba.r = 20;
	rgba.g = 20;
	rgba.b = 20;
	rgba.a = 200;
	ib[0][0] = rgba;
	w.DrawImage(sz, Image(ib));

	// Draw border manually
	w.DrawRect(0, 0, sz.cx, 1, Color(60, 60, 60)); // Top
	w.DrawRect(0, sz.cy - 1, sz.cx, 1, Color(60, 60, 60)); // Bottom
	w.DrawRect(0, 0, 1, sz.cy, Color(60, 60, 60)); // Left
	w.DrawRect(sz.cx - 1, 0, 1, sz.cy, Color(60, 60, 60)); // Right

	// Draw stats text
	int text_x = 10 + 160 + 10 + 80 + 20; // 280
	w.DrawText(text_x, (sz.cy - StdFont().GetCy()) / 2, stats_str, StdFont(11).Bold(), Color(220, 220, 220));
}

VideoPlayerWindow::VideoPlayerWindow()
{
	Sizeable().FrameLess();
	
	// Default size centered on screen
	SetRect(Ctrl::GetWorkArea().CenterRect(960, 540));
	
	Add(hud_panel_);
	hud_panel_.HSizePos(10, 10).BottomPos(10, 40);
	
	hud_panel_.Hide();
	hud_visible_ = false;
	
	hud_panel_.reconnect.WhenAction = [=] {
		Connect(~hud_panel_.host_port);
	};
	hud_panel_.host_port.WhenEnter = [=] {
		Connect(~hud_panel_.host_port);
	};
	hud_panel_.close_btn.WhenAction = [=] {
		Close();
	};

	String host = "127.0.0.1";
	int port = 8082;
	String file_target;
	
	const Vector<String>& args = CommandLine();
	for (int i = 0; i < args.GetCount(); i++) {
		if (args[i] == "--host" && i + 1 < args.GetCount()) {
			host = args[++i];
		} else if (args[i] == "--port" && i + 1 < args.GetCount()) {
			port = StrInt(args[++i]);
		} else if (FileExists(args[i]) || args[i].EndsWith(".mp4") || args[i].EndsWith(".mkv") || args[i].EndsWith(".avi")) {
			file_target = args[i];
		}
	}
	
	String initial_uri;
	if (!file_target.IsEmpty()) {
		initial_uri = file_target;
	} else {
		initial_uri = host + ":" + Format("%d", port);
	}
	hud_panel_.host_port <<= initial_uri;
	
	PostCallback([=] { Connect(initial_uri); });
}

VideoPlayerWindow::~VideoPlayerWindow()
{
	StopThread();
}

void VideoPlayerWindow::Paint(Draw& w)
{
	Size sz = GetSize();
	if (current_frame_.IsEmpty()) {
		w.DrawRect(sz, Color(10, 10, 10));
		w.DrawText((sz.cx - 100) / 2, (sz.cy - StdFont().GetCy()) / 2, "No Stream", StdFont(14).Bold(), Color(120, 120, 120));
		return;
	}

	// Aspect ratio scaling
	Size img_sz = current_frame_.GetSize();
	double aspect_w = (double)sz.cx / img_sz.cx;
	double aspect_h = (double)sz.cy / img_sz.cy;
	double aspect = min(aspect_w, aspect_h);

	int new_w = (int)(img_sz.cx * aspect);
	int new_h = (int)(img_sz.cy * aspect);
	int x = (sz.cx - new_w) / 2;
	int y = (sz.cy - new_h) / 2;

	// Fill background around video
	if (x > 0) {
		w.DrawRect(0, 0, x, sz.cy, Color(10, 10, 10));
		w.DrawRect(x + new_w, 0, sz.cx - (x + new_w), sz.cy, Color(10, 10, 10));
	}
	if (y > 0) {
		w.DrawRect(0, 0, sz.cx, y, Color(10, 10, 10));
		w.DrawRect(0, y + new_h, sz.cx, sz.cy - (y + new_h), Color(10, 10, 10));
	}

	Rect dst_rect(x, y, x + new_w, y + new_h);
	w.DrawImage(dst_rect, current_frame_);
}

void VideoPlayerWindow::LeftDown(Point p, dword flags)
{
	if (!hud_visible_ || !hud_panel_.GetRect().Contains(p)) {
		drag_start_ = p;
		is_dragging_ = true;
		SetCapture();
	}
}

void VideoPlayerWindow::LeftUp(Point p, dword flags)
{
	if (is_dragging_) {
		ReleaseCapture();
		is_dragging_ = false;
	}
}

void VideoPlayerWindow::MouseMove(Point p, dword flags)
{
	ShowHud();

	if (is_dragging_) {
		Point screen_p = GetMousePos();
		Rect rect = GetRect();
		rect.Offset(screen_p - (rect.TopLeft() + drag_start_));
		SetRect(rect);
	}
}

bool VideoPlayerWindow::Key(dword key, int count)
{
	if (key == K_ESCAPE || key == 'q' || key == 'Q') {
		Close();
		return true;
	}
	return TopWindow::Key(key, count);
}

void VideoPlayerWindow::RightDown(Point p, dword flags)
{
	MenuBar menu;
	menu.Add("Exit", [=] { Close(); });
	menu.Execute();
}

void VideoPlayerWindow::Close()
{
	StopThread();
	TopWindow::Close();
}

void VideoPlayerWindow::ThreadLoop()
{
	while (!thread_should_stop_) {
		if (!is_connected_) {
			Ctrl::Call([=] {
				UpdateStats("Connecting...");
			});

			frame_source_.SetConnectTimeoutMs(2000);
			frame_source_.SetSocketTimeoutMs(2000);
			frame_source_.SetWaitTimeoutMs(3000);

			if (frame_source_.Open(uri_)) {
				is_connected_ = true;
				Ctrl::Call([=] {
					UpdateStats("Connected");
				});
			} else {
				String err = frame_source_.GetLastError();
				Ctrl::Call([=] {
					UpdateStats("Connection failed: " + err);
				});
				for (int i = 0; i < 20 && !thread_should_stop_; i++) {
					Thread::Sleep(100);
				}
				continue;
			}
		}

		VsmImageBuffer buf;
		int64 ts = 0;
		if (frame_source_.ReadFrame(buf, ts)) {
			Image img = VsmImageBufferToImage(buf);
			int64 now = msecs();
			int64 latency_ms = now - ts;

			{
				Mutex::Lock __(frame_lock_);
				next_image_ = img;
				next_latency_ = latency_ms;
				has_new_frame_ = true;
			}

			Ctrl::PostCallback([=] {
				Image img_to_draw;
				int64 latency = 0;
				{
					Mutex::Lock __(frame_lock_);
					if (!has_new_frame_) return;
					img_to_draw = next_image_;
					latency = next_latency_;
					has_new_frame_ = false;
				}
				OnNewFrame(img_to_draw, latency);
			});
		} else {
			int kind = frame_source_.GetLastErrorKind();
			if (kind == VsmVideoServerFrameSource::VSM_VSFS_ERR_TIMEOUT) {
				// Recoverable timeout, just loop again
			} else {
				is_connected_ = false;
				frame_source_.Close();
				Ctrl::Call([=] {
					UpdateStats("Connection lost");
				});
			}
		}
	}
}

void VideoPlayerWindow::StartThread()
{
	thread_should_stop_ = false;
	is_connected_ = false;
	bg_thread_.Run(THISBACK(ThreadLoop));
}

void VideoPlayerWindow::StopThread()
{
	thread_should_stop_ = true;
	frame_source_.Close();
	if (bg_thread_.IsOpen()) {
		bg_thread_.Wait();
	}
	if (server_process_.IsRunning()) {
		server_process_.Kill();
	}
}

void VideoPlayerWindow::Connect(const String& target)
{
	StopThread();
	
	current_frame_ = Null;
	frame_times_.Clear();
	current_fps_ = 0.0;
	current_latency_ = 0;
	Refresh();

	String target_uri = target;

	if (FileExists(target)) {
		String server_exe = GetExeDirFile("VideoServer.exe");
		
		Vector<String> args;
		args.Add("--source");
		args.Add("video");
		args.Add("--video");
		args.Add(target);
		args.Add("--port");
		args.Add("8082");
		
		if (!server_process_.Start(server_exe, args)) {
			UpdateStats("Failed to start VideoServer");
			return;
		}
		
		// Wait a moment for server to bind port
		Thread::Sleep(800);
		
		target_uri = "127.0.0.1:8082";
	}

	uri_ = target_uri;
	StartThread();
}

void VideoPlayerWindow::UpdateStats(const String& stats)
{
	connection_status_ = stats;
	
	int w = current_frame_.IsEmpty() ? 0 : current_frame_.GetWidth();
	int h = current_frame_.IsEmpty() ? 0 : current_frame_.GetHeight();
	
	hud_panel_.stats_str = Format("FPS: %.1f | Latency: %d ms | Resolution: %d x %d | State: %s",
	                              current_fps_, current_latency_, w, h, connection_status_);
	
	hud_panel_.Refresh();
}

void VideoPlayerWindow::OnNewFrame(const Image& img, int64 latency)
{
	current_frame_ = img;
	current_latency_ = latency;

	int64 now = msecs();
	frame_times_.Add(now);
	while (frame_times_.GetCount() > 30) {
		frame_times_.Remove(0);
	}
	if (frame_times_.GetCount() > 1) {
		double duration_sec = (double)(frame_times_[frame_times_.GetCount() - 1] - frame_times_[0]) / 1000.0;
		if (duration_sec > 0.0) {
			current_fps_ = (frame_times_.GetCount() - 1) / duration_sec;
		}
	}

	String stats;
	stats = Format("FPS: %.1f | Latency: %d ms | Resolution: %d x %d | State: %s",
	               current_fps_, current_latency_, img.GetWidth(), img.GetHeight(), connection_status_);
	hud_panel_.stats_str = stats;

	Refresh();
}

void VideoPlayerWindow::ShowHud()
{
	if (!hud_visible_) {
		hud_visible_ = true;
		hud_panel_.Show();
		Refresh();
	}
	KillSetTimeCallback(3000, THISBACK(HideHud), 1);
}

void VideoPlayerWindow::HideHud()
{
	if (hud_panel_.host_port.HasFocus()) {
		KillSetTimeCallback(3000, THISBACK(HideHud), 1);
		return;
	}
	hud_visible_ = false;
	hud_panel_.Hide();
	Refresh();
}

END_UPP_NAMESPACE

GUI_APP_MAIN
{
	Upp::VideoPlayerWindow().Run();
}
