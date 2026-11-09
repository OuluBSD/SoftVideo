#include "VideoCore.h"

namespace Upp {

// VSM1 binary format header size (magic + w + h + ch)
static const int VSM_HEADER_SIZE = 16;

// ---------------------------------------------------------------------------
// VsmImageBuffer

void VsmImageBuffer::Create(int w, int h, int ch)
{
	width = w; height = h; channels = ch;
	pixels.SetCount(w * h * ch, 0);
}

byte VsmImageBuffer::Get(int x, int y, int ch) const
{
	ASSERT(x >= 0 && x < width && y >= 0 && y < height && ch < channels);
	return pixels[(y * width + x) * channels + ch];
}

void VsmImageBuffer::Set(int x, int y, byte v, int ch)
{
	ASSERT(x >= 0 && x < width && y >= 0 && y < height && ch < channels);
	pixels[(y * width + x) * channels + ch] = v;
}

String VsmImageBuffer::Info() const
{
	return IntStr(width) + "x" + IntStr(height) + " ch=" + IntStr(channels);
}

bool VsmImageBuffer::Save(const String& path) const
{
	if(IsEmpty()) return false;

	int pixel_bytes = width * height * channels;
	String out;
	out.Reserve(VSM_HEADER_SIZE + pixel_bytes);

	// Magic
	out.Cat("VSM1", 4);
	// Dimensions as little-endian uint32
	dword w = (dword)width, h = (dword)height, ch = (dword)channels;
	out.Cat((const char*)&w,  4);
	out.Cat((const char*)&h,  4);
	out.Cat((const char*)&ch, 4);
	// Pixel data
	out.Cat((const char*)pixels.Begin(), pixel_bytes);

	return SaveFile(path, out);
}

bool VsmImageBuffer::Load(const String& path)
{
	String data = LoadFile(path);
	if(data.GetCount() < VSM_HEADER_SIZE) return false;
	if(memcmp(data.Begin(), "VSM1", 4) != 0) return false;

	const byte* p = (const byte*)data.Begin() + 4;
	dword w, h, ch;
	memcpy(&w,  p + 0, 4);
	memcpy(&h,  p + 4, 4);
	memcpy(&ch, p + 8, 4);

	int expected = (int)(w * h * ch);
	if(data.GetCount() != VSM_HEADER_SIZE + expected) return false;

	width    = (int)w;
	height   = (int)h;
	channels = (int)ch;
	pixels.SetCount(expected);
	memcpy(pixels.Begin(), data.Begin() + VSM_HEADER_SIZE, expected);
	return true;
}

// ---------------------------------------------------------------------------

VsmImageBuffer VsmImageBuffer::MakeSolid(int w, int h, byte value, int ch)
{
	VsmImageBuffer buf;
	buf.Create(w, h, ch);
	Fill(buf.pixels.Begin(), buf.pixels.End(), value);
	return buf;
}

VsmImageBuffer VsmImageBuffer::MakeGradient(int w, int h)
{
	VsmImageBuffer buf;
	buf.Create(w, h, 1);
	for(int y = 0; y < h; y++)
		for(int x = 0; x < w; x++)
			buf.pixels[y * w + x] = (byte)((x * 255) / max(w - 1, 1));
	return buf;
}

VsmImageBuffer VsmImageBuffer::MakeCheckerboard(int w, int h, int cell_size)
{
	VsmImageBuffer buf;
	buf.Create(w, h, 1);
	for(int y = 0; y < h; y++)
		for(int x = 0; x < w; x++) {
			bool white = ((x / cell_size) + (y / cell_size)) % 2 == 0;
			buf.pixels[y * w + x] = white ? 255 : 0;
		}
	return buf;
}

// ---------------------------------------------------------------------------
// VsmImageAsset

bool VsmImageAsset::Save(const String& abs_path, const VsmImageBuffer& img)
{
	return img.Save(abs_path);
}

bool VsmImageAsset::Load(const String& abs_path, VsmImageBuffer& out)
{
	return out.Load(abs_path);
}

} // namespace Upp
