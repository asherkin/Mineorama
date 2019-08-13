#include "PngWriter.h"

#include <png.h>

#include <fstream>
#include <iostream>
#include <type_traits>

// Check that our public API types match the libpng types.
static_assert(std::is_same<png_byte, uint8_t>::value, "png_byte is not a uint8_t");
static_assert(sizeof(PngWriter::Color) == sizeof(png_color), "PngWriter::Color is not the same size as png_color");
static_assert(offsetof(PngWriter::Color, red) == offsetof(png_color, red), "PngWriter::Color::red is not at the same offset as png_color::red");
static_assert(offsetof(PngWriter::Color, green) == offsetof(png_color, green), "PngWriter::Color::green is not at the same offset as png_color::green");
static_assert(offsetof(PngWriter::Color, blue) == offsetof(png_color, blue), "PngWriter::Color::blue is not at the same offset as png_color::blue");

#ifdef PNGWRITER_ENABLE_STATS
PngWriter::PngWriterStats PngWriter::stats;
#endif

PngWriter::PngWriter(const std::string &filename, uint32_t width, uint32_t height) :
	filename(filename), width(width), height(height)
{
	auto blackIdx = getOrAddPaletteIndex(Color{ 0, 0, 0 });

	pixels = std::vector<uint8_t>(width * height, blackIdx);
}

void PngWriter::setPixel(uint32_t x, uint32_t y, const Color &color)
{
	if (x >= width || y >= height) {
		throw std::runtime_error("pixel index out of bounds");
	}

	pixels[(y * width) + x] = getOrAddPaletteIndex(color);
}

const PngWriter::Color &PngWriter::getPixel(uint32_t x, uint32_t y) const
{
	if (x >= width || y >= height) {
		throw std::runtime_error("pixel index out of bounds");
	}

	return palette[pixels[(y * width) + x]];
}

void PngWriter::write() const
{
	std::ofstream stream(filename, std::ios_base::out | std::ios_base::binary);
	if (!stream) {
		throw std::runtime_error("Failed to open " + filename + " for writing");
	}

	png_struct *context = png_create_write_struct_2(PNG_LIBPNG_VER_STRING, nullptr, [](png_struct *ctx, const char *msg) {
		throw std::runtime_error(msg);
	}, [](png_struct *ctx, const char *msg) {
		std::cerr << "libpng warning: " << msg << std::endl;
	}, nullptr, [](png_struct *ctx, size_t len) {
		return malloc(len);
	}, [](png_struct *ctx, void *ptr) {
		return free(ptr);
	});

	png_info *info = png_create_info_struct(context);

	png_set_write_fn(context, &stream, [](png_struct *ctx, png_byte *data, size_t length) {
		auto file = reinterpret_cast<decltype(&stream)>(png_get_io_ptr(ctx));
		if (!file->write(reinterpret_cast<char *>(data), length)) {
			png_error(ctx, "write error");
		}
	}, [](png_struct *ctx) {
		auto file = reinterpret_cast<decltype(&stream)>(png_get_io_ptr(ctx));
		if (!file->flush()) {
			png_error(ctx, "flush error");
		}
	});

	// Favor speed over size.
	png_set_filter(context, PNG_FILTER_TYPE_BASE, PNG_FILTER_NONE);
	png_set_compression_strategy(context, 0); // Z_DEFAULT_STRATEGY
	png_set_compression_level(context, 1); // Z_BEST_SPEED

	// TODO: We might be able to encode using less than 8bpp if the palette is small enough.
	// But probably not worth it as we'll need to re-pack the rows.
	png_set_IHDR(context, info, width, height, 8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_set_PLTE(context, info, reinterpret_cast<const png_color *>(palette.data()), static_cast<int>(palette.size()));

	png_write_info(context, info);

	for (uint32_t y = 0; y < height; ++y) {
		png_write_row(context, &pixels[y * width]);
	}

	png_write_end(context, info);

	png_destroy_write_struct(&context, &info);

	stream.close();

#ifdef PNGWRITER_ENABLE_STATS
	PngWriter::stats.imagesGenerated++;
	PngWriter::stats.paletteSizeDistribution[palette.size()]++;
#endif
}

uint8_t PngWriter::getOrAddPaletteIndex(const Color &color)
{
	const auto &it = paletteMap.find(color);
	if (it != paletteMap.end()) {
		return it->second;
	}

	auto idx = palette.size();
	if (idx >= std::numeric_limits<uint8_t>::max()) {
		throw std::runtime_error("too many colors in palette");
	}

	palette.push_back(color);
	paletteMap[color] = static_cast<uint8_t>(idx);

	return static_cast<uint8_t>(idx);
}
