// Mineorama.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "BlockStorage.h"
#include "ChunkDbKey.h"
#include "Helpers.h"
#include "Minecraft.h"
#include "PngWriter.h"
#include "SuperchunkKey.h"

#include <leveldb/cache.h>
#include <leveldb/db.h>
#include <leveldb/decompress_allocator.h>
#include <leveldb/filter_policy.h>
#include <leveldb/zlib_compressor.h>

#include <tag_primitive.h>
#include <tag_string.h>
#include <tag_compound.h>

#include <nlohmann/json.hpp>

#include <bitset>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

// Paths
constexpr auto WORLD_PATH = R"(C:\Users\asherkin\Downloads\mcpe_viz-master\work\My World\db)";
constexpr auto OUTPUT_PATH = R"(C:\Users\asherkin\Downloads\mcpe_viz-master\work\output)";

// Configurable knobs.
constexpr int THREAD_COUNT = 0; // 0 = Auto
constexpr int CHUNKS_PER_SUPERCHUNK = 32; // 1 - 256 tested, should probably be a ^2
// 256 needs ~2000MB per thread, 4096x4096 output
// 128 needs ~500MB per thread, 2048x2048 output
// 64 needs ~125MB per thread, 1024x1024 output
// 32 needs ~30MB per thread, 512x512 output

using ChunkBitset = std::bitset<CHUNKS_PER_SUPERCHUNK * CHUNKS_PER_SUPERCHUNK>;
using SuperchunkMap = std::unordered_map<SuperchunkKey, ChunkBitset>;
SuperchunkMap g_Superchunks;

namespace std {
	template<> struct hash<pair<string, int16_t>>
	{
		size_t operator()(const pair<string, int16_t> &x) const
		{
			auto h = hash<string>{}(x.first);
			hash_combine(h, x.second);
			return h;
		}
	};
}

std::unordered_map<std::pair<std::string, int16_t>, PngWriter::Color> g_BlockColors;
thread_local std::unordered_map<std::string, std::map<int16_t, size_t>> g_LoggedMissing;

PngWriter::Color get_color_for_block(const std::string &name, int16_t val) {
	auto it = g_BlockColors.find({ name, -1 });
	if (it != g_BlockColors.end()) {
		return it->second;
	}

	auto key = std::make_pair(name, val);
	
	it = g_BlockColors.find(key);
	if (it != g_BlockColors.end()) {
		return it->second;
	}

	g_LoggedMissing[name][val]++;

	// auto hash = std::hash<decltype(key)>{}(key);
	// return PngWriter::Color(hash & 0xFF, (hash >> 8) & 0xFF, (hash >> 16) & 0xFF);
	return PngWriter::Color(255, 255, 255);
}

std::vector<std::unique_ptr<BlockStorage>> parse_subchunk(const std::string &data)
{
	std::istringstream stream(data, std::istringstream::in | std::istringstream::binary);

	int8_t version;
	stream.read(reinterpret_cast<char *>(&version), sizeof(int8_t));

	if (version != 8) {
		throw std::runtime_error("only subchunk format 8 is supported");
	}

	std::vector<std::unique_ptr<BlockStorage>> storages;

	int8_t storageCount;
	stream.read(reinterpret_cast<char *>(&storageCount), sizeof(int8_t));
	storages.reserve(storageCount);

	for (int8_t i = 0; i < storageCount; ++i) {
		storages.emplace_back(std::make_unique<BlockStorage>(stream));
	}

	stream.peek();
	if (!stream.eof()) {
		throw std::runtime_error("did not parse entire subchunk data");
	}

	return storages;
}

void process_superchunk(leveldb::DB *db, const leveldb::ReadOptions &options, const SuperchunkKey &key, const ChunkBitset &bitmap)
{
	const auto &airColor = get_color_for_block("minecraft:air", 0);
	std::vector<std::unique_ptr<PngWriter>> images;

	for (int x = 0; x < CHUNKS_PER_SUPERCHUNK; ++x) {
		for (int z = 0; z < CHUNKS_PER_SUPERCHUNK; ++z) {
			int i = (x * CHUNKS_PER_SUPERCHUNK) + z;
			if (!bitmap[i]) {
				// There is no chunk here.
				continue;
			}

			// 16 * BLOCKS_PER_CHUNK = 256, the max height of the world currently.
			// Got to have the hard limit here as subchunks can exist above non-existent subchunks.
			// TODO: Possibly only in the nether? Which could give us a performance increase.
			for (uint8_t y = 0; y < 16; ++y) {
				ChunkDbKey chunkKey = {};
				chunkKey.x = (key.x * CHUNKS_PER_SUPERCHUNK) + x;
				chunkKey.z = (key.z * CHUNKS_PER_SUPERCHUNK) + z;
				chunkKey.dimension = key.dimension;
				chunkKey.tag = Tag::SubChunkPrefix;
				chunkKey.subchunk = y;
				
				std::string value;
				leveldb::Status status = db->Get(options, chunkKey, &value);

				// Subchunks work like a stack, so we loop up until we can't find any more
				// TODO: This turns out to be a lie, any pure-air subchunk will not exist in the nether.
				if (status.IsNotFound()) {
					// If we failed immediately, do a sanity check looking for the version tag
					if (y == 0) {
						chunkKey.tag = Tag::Version;
						chunkKey.subchunk = -1;

						status = db->Get(options, chunkKey, &value);

						if (!status.ok()) {
							throw std::runtime_error("did not find subchunk key and then did not find version key, check key generation");
						}
					}

					continue; // If the stack worked, this would be a break.
				}

				if (!status.ok()) {
					throw std::runtime_error("failed to read subchunk key");
				}

				auto subchunk = parse_subchunk(value);

				for (int bx = 0; bx < BLOCKS_PER_CHUNK; ++bx) {
					for (int bz = 0; bz < BLOCKS_PER_CHUNK; ++bz) {
						for (int by = 0; by < BLOCKS_PER_CHUNK; ++by) {
							size_t idx = (((bx * BLOCKS_PER_CHUNK) + bz) * BLOCKS_PER_CHUNK) + by;
							const auto &block = (*subchunk[0])[idx];
							//std::cout << idx << " = " << block << std::endl;

							int ix = (x * BLOCKS_PER_CHUNK) + bx;
							int iy = (y * BLOCKS_PER_CHUNK) + by;
							int iz = (z * BLOCKS_PER_CHUNK) + bz;

							while (iy >= images.size()) {
								std::ostringstream filename;
								filename << OUTPUT_PATH << "/tile_" << key.dimension << "_" << iy << "_" << key.x << "_" << key.z << ".png";
								images.push_back(std::make_unique<PngWriter>(filename.str(), BLOCKS_PER_CHUNK * CHUNKS_PER_SUPERCHUNK, BLOCKS_PER_CHUNK * CHUNKS_PER_SUPERCHUNK, airColor));
							}

							// The dynamic_cast .as<>() uses is very, very slow, so be unsafe instead.
							const auto &blockName = reinterpret_cast<const nbt::tag_string *>(block.at("name").get_ptr().get())->get();
							auto blockVal = reinterpret_cast<const nbt::tag_short *>(block.at("val").get_ptr().get())->get();
							auto blockColor = get_color_for_block(blockName, blockVal);
							images[iy]->setPixel(ix, iz, blockColor);
						}
					}
				}
			}
		}
	}

#if 1
	// Loop again to fill blocks up to the top, has to be after all layer images are generated
	// TODO: I think this needs to be two different things:
	//   a) The current looping of blocks ignoring air.
	//	 b) An overview image that hides top-layers.
	for (int x = 0; x < CHUNKS_PER_SUPERCHUNK; ++x) {
		for (int z = 0; z < CHUNKS_PER_SUPERCHUNK; ++z) {
			int i = (x * CHUNKS_PER_SUPERCHUNK) + z;
			if (!bitmap[i]) {
				// There is no chunk here.
				continue;
			}

			for (int bx = 0; bx < BLOCKS_PER_CHUNK; ++bx) {
				for (int bz = 0; bz < BLOCKS_PER_CHUNK; ++bz) {
					int ix = (x * BLOCKS_PER_CHUNK) + bx;
					int iz = (z * BLOCKS_PER_CHUNK) + bz;

					auto color = airColor;
					auto it = images.rbegin();

					while (it != images.rend()) {
						color = (*it)->getPixel(ix, iz);
						if (color != airColor) {
							break;
						}

						it++;
					}

					while (it != images.rbegin()) {
						it--;

						(*it)->setPixel(ix, iz, color);
					}
				}
			}
		}
	}
#endif

	// TODO: Store the highest image layer index somewhere for the web viewer to use

	for (const auto &it : images) {
		it->write();
	}
}

int main()
{
	g_BlockColors[{ "minecraft:air", -1 }] = PngWriter::Color(0, 0, 0);

	std::ifstream configFile("blocks.json");
	if (configFile) {
		std::cout << "Parsing blocks config ..." << std::endl;

		nlohmann::json config;
		configFile >> config;

		for (const auto &blockIt : config.items()) {
			auto name = blockIt.key();
			if (name.find(':') == name.npos) {
				name = "minecraft:" + name;
			}

			auto colors = blockIt.value()["color"];

			if (colors.type() == nlohmann::json::value_t::string) {
				g_BlockColors[{ name, -1 }] = PngWriter::Color(colors);
				continue;
			}

			for (int16_t i = 0; i < colors.size(); ++i) {
				g_BlockColors[{ name, i }] = PngWriter::Color(colors[i]);
			}
		}
	} else {
		std::cout << "Warning: did not find blocks.json, colors will be random" << std::endl;
	}

	leveldb::Options options;

	// Create a bloom filter to quickly tell if a key is in the database or not
	options.filter_policy = leveldb::NewBloomFilterPolicy(10);

	// Create a 16 mb cache, tuning this doesn't really impact performance at all with our workload
	options.block_cache = leveldb::NewLRUCache(16 * 1024 * 1024);

	// Create a 4mb write buffer, to improve compression and touch the disk less
	options.write_buffer_size = 4 * 1024 * 1024;

	// Use the new raw-zip compressor to write (and read)
	options.compressors[0] = new leveldb::ZlibCompressorRaw(-1);

	// Also setup the old, slower compressor for backwards compatibility. This will only be used to read old compressed blocks.
	options.compressors[1] = new leveldb::ZlibCompressor();

	// Create a reusable memory space for decompression so it allocates less
	leveldb::ReadOptions readOptions;
	readOptions.decompress_allocator = new leveldb::DecompressAllocator();

	leveldb::DB *db;
	leveldb::Status status = leveldb::DB::Open(options, WORLD_PATH, &db);

	std::cout << "Iterating all keys to build superchunks ";

	int count = 0;
	leveldb::Iterator *iter = db->NewIterator(readOptions);
	for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
		const auto size = iter->key().size();
		const auto name = iter->key().ToString();

		// Various bits of village data, key is GUID then type
		if (iter->key().starts_with("VILLAGE_")) {
			continue;
		}

		// Maps
		if (iter->key().starts_with("map_")) {
			continue;
		}

		// Remote player information
		if (iter->key().starts_with("player_server_")) {
			continue;
		}

		// Player mappings
		if (iter->key().starts_with("player_")) {
			continue;
		}

		// Single player information
		if (name == "~local_player") {
			continue;
		}

		if (name == "AutonomousEntities") {
			continue;
		}

		if (name == "BiomeData") {
			continue;
		}

		if (name == "Nether") {
			continue;
		}

		if (name == "Overworld") {
			continue;
		}

		if (name == "mobevents") {
			continue;
		}

		if (name == "portals") {
			continue;
		}

		if (name == "schedulerWT") {
			continue;
		}

		if (name == "scoreboard") {
			continue;
		}

		//  9 = Overworld without subchunk id
		// 10 = Overworld with subchunk id
		// 13 = Non-overworld without subchunk id
		// 14 = Non-overworld wiht subchunk id

		// Any keys that got this far we expect to be subchunks.
		if (size != 9 && size != 10 && size != 13 && size != 14) {
			throw std::runtime_error("suspicious key, not the right size to be a subchunk: " + string_to_hex(name));
			continue;
		}

		// If the high byte of the x-coord is in use, it probably isn't a subchunk but a printable key the same length.
		if (iter->key()[3] != 0 && iter->key()[3] != -1) {
			throw std::runtime_error("suspicious key, very large x coordinate, printable?: " + string_to_hex(name));
			continue;
		}

		Tag tag = Tag::Unknown;

		if (size == 13 || size == 14) {
			memcpy(&tag, name.data() + sizeof(int32_t) + sizeof(int32_t) + sizeof(int32_t), sizeof(tag));
		} else {
			memcpy(&tag, name.data() + sizeof(int32_t) + sizeof(int32_t), sizeof(tag));
		}

		// At this point we only care about knowing if a chunk exists, so ignore everything other than Version (which should always exist)
		if (tag != Tag::Version) {
			continue;
		}

		SuperchunkKey chunkKey = {};
		memcpy(&chunkKey.x, name.data(), sizeof(int32_t));
		memcpy(&chunkKey.z, name.data() + sizeof(int32_t), sizeof(int32_t));
		if (size == 13 || size == 14) {
			memcpy(&chunkKey.dimension, name.data() + sizeof(int32_t) + sizeof(int32_t), sizeof(int32_t));
		}

		count++;
		if ((count % 1000) == 0) {
			std::cout << ".";
		}

		auto xc = chunkKey.x % CHUNKS_PER_SUPERCHUNK;
		if (xc < 0) xc = CHUNKS_PER_SUPERCHUNK + xc;

		auto zc = chunkKey.z % CHUNKS_PER_SUPERCHUNK;
		if (zc < 0) zc = CHUNKS_PER_SUPERCHUNK + zc;

		auto idx = (xc * CHUNKS_PER_SUPERCHUNK) + zc;

		// This gets us round-to-negative-infinity behaviour
		if (chunkKey.x < 0) chunkKey.x -= CHUNKS_PER_SUPERCHUNK - 1;
		chunkKey.x = chunkKey.x / CHUNKS_PER_SUPERCHUNK;

		if (chunkKey.z < 0) chunkKey.z -= CHUNKS_PER_SUPERCHUNK - 1;
		chunkKey.z = chunkKey.z / CHUNKS_PER_SUPERCHUNK;

		g_Superchunks[chunkKey][idx] = true;
	}

	std::cout << " Done" << std::endl;

	size_t bitcount = 0;
	for (auto &it : g_Superchunks) {
		//std::cout << it.first.dimension << " " << (it.first.x * CHUNKS_PER_SUPERCHUNK * BLOCKS_PER_CHUNK) << " " << (it.first.z * CHUNKS_PER_SUPERCHUNK * BLOCKS_PER_CHUNK) << std::endl << superchunk_to_string(it.second) << std::endl;
		bitcount += it.second.count();
	}

	std::cout << "Chunk count: " << count << std::endl;

	auto total = g_Superchunks.size();
	std::cout << "Superchunk count: " << total << std::endl;

	if (count != bitcount) {
		throw std::runtime_error("sanity check failed, chunk count != bitcount");
	}

	unsigned int thread_count = THREAD_COUNT;
	if (thread_count == 0) {
		thread_count = std::thread::hardware_concurrency();
	}

	std::cout << "Starting " << thread_count << " worker threads..." << std::endl;

	size_t done = 0;
	std::vector<std::thread> workers;

	std::mutex chunk_mutex;
	auto chunk = g_Superchunks.cbegin();
	auto end = g_Superchunks.cend();

	std::mutex loggedMissingSummaryMutex;
	decltype(g_LoggedMissing) loggedMissingSummary;

	auto worker = [total, &done, &chunk_mutex, &chunk, &end, db, &readOptions, &loggedMissingSummaryMutex, &loggedMissingSummary](bool shouldPrint) {
		while (true) {
			std::pair<SuperchunkKey, ChunkBitset> it;
			{
				std::lock_guard<std::mutex> lock(chunk_mutex);
				done++;
				if (chunk == end) {
					break;
				}
				it = *chunk++;
			}

			if (shouldPrint) {
				std::cout << "Progress: " << ((done * 100) / total) << std::endl;
			}

			process_superchunk(db, readOptions, it.first, it.second);
		}

		// Copy from per-thread store to shared store.
		{
			std::lock_guard<std::mutex> lock(loggedMissingSummaryMutex);
			for (const auto &it : g_LoggedMissing) {
				for (const auto &iit : it.second) {
					loggedMissingSummary[it.first][iit.first] += iit.second;
				}
			}
		}
	};

	for (unsigned int i = 0; i < (thread_count - 1); ++i) {
		workers.emplace_back(worker, false);
	}

	worker(true);

	for (auto &it : workers) {
		it.join();
	}

	std::multimap<size_t, std::string> loggedMissingDisplay;

	for (const auto &it : loggedMissingSummary) {
		std::string name = it.first + "[";
		size_t total = 0;

		for (const auto &iit : it.second) {
			if (name.back() != '[') {
				name.push_back(',');
			}
			name += std::to_string(iit.first);
			total += iit.second;
		}

		loggedMissingDisplay.emplace(total, name + "]");
	}

	for (const auto &it : loggedMissingDisplay) {
		std::cout << it.first << " " << it.second << std::endl;
	}
}
