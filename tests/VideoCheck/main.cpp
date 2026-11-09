#include <Core/Core.h>
#include <Draw/Draw.h>
#include <VideoCore/VideoCore.h>
#include <RasterPlayer/RasterPlayer.h>

using namespace Upp;

CONSOLE_APP_MAIN
{
	StdLogSetup(LOG_COUT | LOG_FILE);
	Cout() << "========================================\n";
	Cout() << "Running VideoCheck Unit & Lifecycle Tests\n";
	Cout() << "========================================\n\n";

	// 1. Test VsmImageBuffer
	Cout() << "[1] Testing VsmImageBuffer allocation and manipulation...\n";
	{
		VsmImageBuffer buf;
		buf.Create(64, 48, 4);
		ASSERT(buf.width == 64);
		ASSERT(buf.height == 48);
		ASSERT(buf.channels == 4);
		ASSERT(buf.pixels.GetCount() == 64 * 48 * 4);
		ASSERT(!buf.IsEmpty());

		buf.Set(10, 20, 128, 0);
		buf.Set(10, 20, 200, 1);
		buf.Set(10, 20, 255, 2);
		buf.Set(10, 20, 255, 3);
		ASSERT(buf.Get(10, 20, 0) == 128);
		ASSERT(buf.Get(10, 20, 1) == 200);
		ASSERT(buf.Get(10, 20, 2) == 255);
		ASSERT(buf.Get(10, 20, 3) == 255);

		// Test serialization
		String temp_file = AppendFileName(GetTempPath(), "test_image.vsm");
		bool saved = buf.Save(temp_file);
		ASSERT(saved);

		VsmImageBuffer loaded;
		bool loaded_ok = loaded.Load(temp_file);
		ASSERT(loaded_ok);
		ASSERT(loaded.width == 64);
		ASSERT(loaded.height == 48);
		ASSERT(loaded.channels == 4);
		ASSERT(loaded.Get(10, 20, 0) == 128);
		ASSERT(loaded.Get(10, 20, 1) == 200);
		DeleteFile(temp_file);

		// Test synthetic helpers
		VsmImageBuffer solid = VsmImageBuffer::MakeSolid(32, 32, 42, 1);
		ASSERT(solid.Get(16, 16) == 42);

		VsmImageBuffer grad = VsmImageBuffer::MakeGradient(100, 10);
		ASSERT(grad.Get(0, 5) == 0);
		ASSERT(grad.Get(99, 5) == 255);

		VsmImageBuffer check = VsmImageBuffer::MakeCheckerboard(64, 64, 8);
		ASSERT(check.Get(0, 0) == 255);
		ASSERT(check.Get(8, 0) == 0);

		Cout() << "PASS: VsmImageBuffer verified\n";
	}

	// 2. Test VsmVideoServerFrameSource
	Cout() << "[2] Testing VsmVideoServerFrameSource interface...\n";
	{
		VsmVideoServerFrameSource source;
		ASSERT(!source.IsReady());
		ASSERT(source.GetWidth() == 0);
		ASSERT(source.GetHeight() == 0);
		ASSERT(source.GetFPS() == 0);

		source.SetConnectTimeoutMs(100);
		source.SetSocketTimeoutMs(100);
		source.SetWaitTimeoutMs(100);

		// Connecting to non-existent endpoint fails gracefully without hang or crash
		bool opened = source.Open("127.0.0.1:65530");
		ASSERT(!opened);
		ASSERT(!source.GetLastError().IsEmpty());
		source.Close();

		Cout() << "PASS: VsmVideoServerFrameSource verified\n";
	}

	// 3. Test VsmVideoFileFrameSource
	Cout() << "[3] Testing VsmVideoFileFrameSource fallback...\n";
	{
		VsmVideoFileFrameSource file_source;
		bool open_invalid = file_source.Open("nonexistent_video_file.mp4");
		ASSERT(!open_invalid);
		ASSERT(!file_source.GetSourceInfo().IsEmpty());
		file_source.Close();

		Cout() << "PASS: VsmVideoFileFrameSource verified\n";
	}

	// 4. Test RasterPlayer
	Cout() << "[4] Testing RasterPlayer controls...\n";
	{
		RasterPlayer player;
		ASSERT(player.GetPageCount() == 0);
		ASSERT(player.GetFrameCount() == 0);
		ASSERT(!player.IsRunning());

		player.SetSpeed(2.0);
		player.SetBackground(Black());
		player.SetMT(false);

		Cout() << "PASS: RasterPlayer verified\n";
	}

	Cout() << "\n========================================\n";
	Cout() << "ALL VideoCheck TESTS PASSED SUCCESSFULLY!\n";
	Cout() << "========================================\n";
	SetExitCode(0);
}
