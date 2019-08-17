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

	std::ifstream stream(RESOURCE_PACK_PATH + std::string("/") + path + ".png", std::ios_base::in | std::ios_base::binary);
	if (!stream) {
		return "";
		//throw std::runtime_error("Failed to open " + path + " for reading");
	}

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

	png_read_png(context, info, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_STRIP_ALPHA | PNG_TRANSFORM_PACKING | PNG_TRANSFORM_EXPAND, nullptr);

	uint32_t height = png_get_image_height(context, info);
	uint32_t width = png_get_image_width(context, info);

	size_t count = 0;
	double r = 0, g = 0, b = 0;

	png_byte **rows = png_get_rows(context, info);
	for (uint32_t y = 0; y < height; ++y) {
		auto *row = rows[y];
		for (uint32_t x = 0; x < width; ++x) {
			auto ir = row[x * 3];
			auto ig = row[(x * 3) + 1];
			auto ib = row[(x * 3) + 2];

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

	// TODO: Proper leaf color handling.
	if (path.find("/leaves_") != path.npos || path.find("/tallgrass") != path.npos || path.find("/melon_stem") != path.npos || path.find("/pumpkin_stem") != path.npos) {
		r = (r + 78) / 2;
		g = (g + 118) / 2;
		b = (b + 42) / 2;
	}

	png_destroy_read_struct(&context, &info, nullptr);

	stream.close();

	std::ostringstream hex;
	hex << std::hex << std::setfill('0') << std::setw(2) << (int)r;
	hex << std::hex << std::setfill('0') << std::setw(2) << (int)g;
	hex << std::hex << std::setfill('0') << std::setw(2) << (int)b;

	g_ColorCache.emplace(path, hex.str());
	return hex.str();
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
