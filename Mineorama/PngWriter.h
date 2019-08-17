#pragma once

#define PNGWRITER_ENABLE_STATS

#include <string>
#include <vector>
#include <unordered_map>

struct PngWriterColor
{
	uint8_t red;
	uint8_t green;
	uint8_t blue;

	PngWriterColor();
	PngWriterColor(uint8_t r, uint8_t g, uint8_t b);
	PngWriterColor(const std::string &hex);

	inline bool operator==(const PngWriterColor &other) const
	{
		return red == other.red && green == other.green && blue == other.blue;
	}

	inline bool operator!=(const PngWriterColor &other) const
	{
		return !(*this == other);
	}
};

namespace std {
	template<> struct hash<PngWriterColor>
	{
		size_t operator()(const PngWriterColor &x) const
		{
			return std::hash<int32_t>{}((x.red << 16) | (x.green << 8) | x.blue);
		}
	};
}

class PngWriter
{
public:
	using Color = PngWriterColor;

public:
	PngWriter(const std::string &filename, uint32_t width, uint32_t height, const Color &background);

	void setPixel(uint32_t x, uint32_t y, const Color &color);
	const Color &getPixel(uint32_t x, uint32_t y) const;

	void write() const;

private:
	uint8_t getOrAddPaletteIndex(const Color &color);

private:
	std::string filename;
	uint32_t width;
	uint32_t height;

	std::vector<Color> palette;
	std::unordered_map<Color, uint8_t> paletteMap;

	std::vector<uint8_t> pixels;

#ifdef PNGWRITER_ENABLE_STATS
private:
	static struct PngWriterStats {
		size_t imagesGenerated;
		size_t paletteSizeDistribution[256];
	} stats;
#endif
};
