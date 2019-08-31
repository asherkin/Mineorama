// Blockapic.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <png.h>

constexpr auto RESOURCE_PACK_PATH = R"(C:\Users\asherkin\Downloads\Vanilla_Resource_Pack_1.12.0)";

std::unordered_map<std::string, std::string> g_ColorCache;

#pragma optimize("", off)
// [00:57:14] <asherkin> https://gist.github.com/asherkin/3fd74dfe83dc76d48360bbecaefa68a8 loop unroller bug or have I done something stupid (any one of those 3 changes "fixes" it)
// [00:57:57] <asherkin> context: [00:28] asherkin : oh dear, I've ended up with a program that works correctly in a debug build but outputs the wrong values in a release build
std::string color_from_png(const std::string &path, std::ifstream &stream)
{
	png_struct *context = png_create_read_struct_2(PNG_LIBPNG_VER_STRING, nullptr, [](png_struct *ctx, const char *msg) {
		throw std::runtime_error(msg);
	}, [](png_struct *ctx, const char *msg) {
		std::cerr << "libpng warning: " << msg << std::endl;
	}, nullptr, [](png_struct *ctx, size_t len) {
		return malloc(len);
	}, [](png_struct *ctx, void *ptr) {
		return free(ptr);
	});

	png_info *info = png_create_info_struct(context);

	png_set_read_fn(context, &stream, [](png_struct *ctx, png_byte *data, size_t length) {
		auto file = reinterpret_cast<decltype(&stream)>(png_get_io_ptr(ctx));
		if (!file->read(reinterpret_cast<char *>(data), length)) {
			png_error(ctx, "read error");
		}
	});

	png_read_png(context, info, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_PACKING | PNG_TRANSFORM_EXPAND, nullptr);

	uint32_t height = png_get_image_height(context, info);
	uint32_t width = png_get_image_width(context, info);

	png_byte channels = png_get_channels(context, info);
	if (channels < 3 || channels > 4) {
		throw std::runtime_error("bad number of color channels");
	}

	size_t count = 0;
	double r = 0, g = 0, b = 0;

	png_byte **rows = png_get_rows(context, info);
	for (uint32_t y = 0; y < height; ++y) {
		auto *row = rows[y];
		for (uint32_t x = 0; x < width; ++x) {
			auto ir = row[x * channels];
			auto ig = row[(x * channels) + 1];
			auto ib = row[(x * channels) + 2];
			if (channels >= 4) {
				auto ia = row[(x * channels) + 3];
				if (ia == 0) {
					// Ignore fully transparent pixels.
					continue;
				}
			}

			if (ir == ig && ig == ib) {
				// Ignore the colors of stone.
				// TODO: Generate these.
				if (path != "textures/blocks/stone" && (ir == 104 || ir == 116 || ir == 127 || ir == 143)) {
					continue;
				}
			}

			r = ((r * count) + ir) / (count + 1);
			g = ((g * count) + ig) / (count + 1);
			b = ((b * count) + ib) / (count + 1);

			// std::cout << count << " " << r << " " << g << " " << b << std::endl;

			count++;
		}
	}

	// TODO: Proper leaf color handling.
	if (path.find("/leaves_") != path.npos || path.find("/tallgrass") != path.npos || path.find("/melon_stem") != path.npos || path.find("/pumpkin_stem") != path.npos) {
		r = (r + 78) / 2;
		g = (g + 118) / 2;
		b = (b + 42) / 2;
	}

	png_destroy_read_struct(&context, &info, nullptr);

	std::ostringstream hex;
	hex << std::hex << std::setfill('0') << std::setw(2) << (int)r;
	hex << std::hex << std::setfill('0') << std::setw(2) << (int)g;
	hex << std::hex << std::setfill('0') << std::setw(2) << (int)b;

	// std::cout << path << " = " << hex.str() << std::endl;

	g_ColorCache.emplace(path, hex.str());
	return hex.str();
}
#pragma optimize("", on)

std::string color_from_tga(const std::string &path, std::ifstream &stream)
{
	uint8_t idLength;
	stream.read(reinterpret_cast<char *>(&idLength), sizeof(idLength));
	if (idLength != 0) {
		throw new std::runtime_error("unknown idLength");
	}

	uint8_t colorMapType;
	stream.read(reinterpret_cast<char *>(&colorMapType), sizeof(colorMapType));
	if (colorMapType != 0) {
		throw new std::runtime_error("unknown colorMapType");
	}

	uint8_t imageType;
	stream.read(reinterpret_cast<char *>(&imageType), sizeof(imageType));
	if (imageType != 2 && imageType != 10) {
		// 2 = uncompressed true-color image
		// 10 = run-length encoded true-color image
		throw new std::runtime_error("unknown imageType");
	}

	// We don't actually handle these yet...
	if (imageType == 10) {
		return "ffffff";
	}

	int16_t colorMapFirstIndexEntry;
	stream.read(reinterpret_cast<char *>(&colorMapFirstIndexEntry), sizeof(colorMapFirstIndexEntry));
	if (colorMapFirstIndexEntry != 0) {
		throw new std::runtime_error("unknown colorMapFirstIndexEntry");
	}

	int16_t colorMapLength;
	stream.read(reinterpret_cast<char *>(&colorMapLength), sizeof(colorMapLength));
	if (colorMapLength != 0) {
		throw new std::runtime_error("unknown colorMapLength");
	}

	uint8_t colorMapEntrySize;
	stream.read(reinterpret_cast<char *>(&colorMapEntrySize), sizeof(colorMapEntrySize));
	if (colorMapEntrySize != 0) {
		throw new std::runtime_error("unknown colorMapEntrySize");
	}

	int16_t xOrigin;
	stream.read(reinterpret_cast<char *>(&xOrigin), sizeof(xOrigin));
	if (xOrigin != 0) {
		throw new std::runtime_error("unknown xOrigin");
	}

	int16_t yOrigin;
	stream.read(reinterpret_cast<char *>(&yOrigin), sizeof(yOrigin));
	if (yOrigin != 0) {
		throw new std::runtime_error("unknown yOrigin");
	}

	int16_t width;
	stream.read(reinterpret_cast<char *>(&width), sizeof(width));

	int16_t height;
	stream.read(reinterpret_cast<char *>(&height), sizeof(height));

	uint8_t pixelDepth;
	stream.read(reinterpret_cast<char *>(&pixelDepth), sizeof(pixelDepth));
	if (pixelDepth != 32) {
		throw new std::runtime_error("unknown pixelDepth");
	}

	uint8_t imageDescriptor;
	stream.read(reinterpret_cast<char *>(&imageDescriptor), sizeof(imageDescriptor));
	if (imageDescriptor != 8) {
		throw new std::runtime_error("unknown imageDescriptor");
	}

	size_t count = 0;
	double r = 0, g = 0, b = 0;

	for (int16_t y = 0; y < height; ++y) {
		for (int16_t x = 0; x < width; ++x) {
			uint8_t ir, ig, ib, ia;
			stream.read(reinterpret_cast<char *>(&ib), sizeof(ir));
			stream.read(reinterpret_cast<char *>(&ig), sizeof(ir));
			stream.read(reinterpret_cast<char *>(&ir), sizeof(ir));
			stream.read(reinterpret_cast<char *>(&ia), sizeof(ir));

			if (ia == 0) {
				// Ignore fully transparent pixels.
				continue;
			}

			if (ir == ig && ig == ib) {
				// Ignore the colors of stone.
				// TODO: Generate these.
				if (path != "textures/blocks/stone" && (ir == 104 || ir == 116 || ir == 127 || ir == 143)) {
					continue;
				}
			}

			r = ((r * count) + ir) / (count + 1);
			g = ((g * count) + ig) / (count + 1);
			b = ((b * count) + ib) / (count + 1);

			count++;
		}
	}

	std::ostringstream hex;
	hex << std::hex << std::setfill('0') << std::setw(2) << (int)r;
	hex << std::hex << std::setfill('0') << std::setw(2) << (int)g;
	hex << std::hex << std::setfill('0') << std::setw(2) << (int)b;

	// std::cout << path << " = " << hex.str() << std::endl;

	g_ColorCache.emplace(path, hex.str());
	return hex.str();
}

std::string color_from_image(std::string path)
{
	auto cache = g_ColorCache.find(path);
	if (cache != g_ColorCache.end()) {
		return cache->second;
	}

	// TODO: Handle water/grass colors properly.
	if (path == "textures/blocks/grass_top") {
		path = "textures/blocks/grass_carried";
	} else if (path == "textures/blocks/waterlily") {
		path = "textures/blocks/carried_waterlily";
	} else if (path == "textures/blocks/vine") {
		path = "textures/blocks/vine_carried";
	} else if (path == "textures/blocks/water_still_grey") {
		path = "textures/blocks/water_still";
	} else if (path == "textures/blocks/water_flow_grey") {
		path = "textures/blocks/water_flow";
	}

	//if (path != "textures/blocks/lava_still") return "";

	std::ifstream stream(RESOURCE_PACK_PATH + std::string("/") + path + ".png", std::ios_base::in | std::ios_base::binary);
	if (stream) {
		return color_from_png(path, stream);
	}

	stream = std::ifstream(RESOURCE_PACK_PATH + std::string("/") + path + ".tga", std::ios_base::in | std::ios_base::binary);
	if (stream) {
		return color_from_tga(path, stream);
	}

	return "ffffff";
	// throw std::runtime_error("Failed to open " + path + " for reading");
}

int main()
{
	// If these throw an exception, make sure you've removed the comments from the json files

	std::ifstream blocksStream(RESOURCE_PACK_PATH + std::string("/blocks.json"));
	nlohmann::json blocksConfig;
	blocksStream >> blocksConfig;

	std::ifstream texturesStream(RESOURCE_PACK_PATH + std::string("/textures/terrain_texture.json"));
	nlohmann::json texturesConfig;
	texturesStream >> texturesConfig;
	const auto &textureData = texturesConfig["texture_data"];

	nlohmann::json output;

	for (const auto &bit : blocksConfig.items()) {
		const auto &name = bit.key();
		const auto &block = bit.value();

		const auto &textureSet = block.find("textures");
		if (textureSet == block.end()) {
			continue;
		}

		std::string textureName;
		switch (textureSet->type()) {
			case nlohmann::json::value_t::string:
				textureName = textureSet->get<std::string>();
				break;
			case nlohmann::json::value_t::object:
				textureName = (*textureSet)["up"].get<std::string>();
				break;
			default:
				throw std::runtime_error("unknown textures type for " + name + ": " + textureSet->type_name());
		}

		const auto &textures = textureData[textureName]["textures"];

		if (textures.type() == nlohmann::json::value_t::string) {
			auto color = color_from_image(textures);
			if (!color.empty()) {
				output[name]["color"] = color;
			}
			continue;	
		}

		if (textures.type() == nlohmann::json::value_t::object) {
			// TODO: Do something with color_tint?
			auto color = color_from_image(textures["path"]);
			if (!color.empty()) {
				output[name]["color"] = color;
			}
			continue;
		}

		nlohmann::json arr;
		for (const auto &texture : textures) {
			if (texture.type() == nlohmann::json::value_t::object) {
				// TODO: Do something with color_tint?
				auto color = color_from_image(texture["path"]);
				if (color.empty() && !arr.empty()) {
					color = "ffffff"; // TODO: If the array isn't empty, got to maintain indexing.
				}
				if (!color.empty()) {
					arr.push_back(color);
				}
				continue;
			}

			auto color = color_from_image(texture);
			if (color.empty() && !arr.empty()) {
				color = "ffffff"; // TODO: If the array isn't empty, got to maintain indexing.
			}
			if (!color.empty()) {
				arr.push_back(color);
			}
		}

		if (!arr.empty()) {
			output[name]["color"] = arr;
		}
	}

	std::cout << output << std::endl;
}
