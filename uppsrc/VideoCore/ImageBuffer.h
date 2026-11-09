#ifndef _VisualStateModel_ImageBuffer_h_
#define _VisualStateModel_ImageBuffer_h_

namespace Upp {

// Simple headless pixel buffer — no Draw/GUI dependency.
// Binary file format (.vsm): "VSM1" + uint32 w + uint32 h + uint32 ch + pixels
//   ch == 1 → grayscale
//   ch == 3 → RGB
//   ch == 4 → RGBA

struct VsmImageBuffer : Moveable<VsmImageBuffer> {
	int          width    = 0;
	int          height   = 0;
	int          channels = 1;
	Vector<byte> pixels;

	bool IsEmpty() const { return pixels.IsEmpty(); }
	void Create(int w, int h, int ch = 1);

	byte  Get(int x, int y, int ch = 0) const;
	void  Set(int x, int y, byte v, int ch = 0);

	bool Save(const String& path) const;
	bool Load(const String& path);

	// Diagnostic string
	String Info() const;

	// Synthetic test images (grayscale, ch==1)
	static VsmImageBuffer MakeSolid(int w, int h, byte value, int ch = 1);
	static VsmImageBuffer MakeGradient(int w, int h);
	static VsmImageBuffer MakeCheckerboard(int w, int h, int cell_size = 8);
};

// Image asset save/load helper (session-relative paths, .vsm format)
struct VsmImageAsset {
	static bool Save(const String& abs_path, const VsmImageBuffer& img);
	static bool Load(const String& abs_path, VsmImageBuffer& out);
};

} // namespace Upp

#endif
