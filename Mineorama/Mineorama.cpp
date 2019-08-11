// Mineorama.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <leveldb/db.h>
#include <leveldb/env.h>
#include <leveldb/filter_policy.h>
#include <leveldb/cache.h>
#include <leveldb/zlib_compressor.h>
#include <leveldb/decompress_allocator.h>

#include <io/stream_reader.h>

#define PNG_DEBUG 3
#include <png.h>

#include <iostream>
#include <sstream>
#include <unordered_map>
#include <bitset>
#include <thread>
#include <atomic>
#include <fstream>

enum class Tag : int8_t {
	Unknown = 0,
	Data2D = 45,
	Data2DLegacy = 46,
	SubChunkPrefix = 47,
	LegacyTerrain = 48,
	BlockEntity = 49,
	Entity = 50,
	PendingTicks = 51,
	BlockExtraData = 52,
	BiomeState = 53,
	FinalizedState = 54,
	Version = 118
};

struct ChunkDbKey {
	int32_t x;
	int32_t z;
	int32_t dimension;
	Tag tag;
	int8_t subchunk;

	operator leveldb::Slice()
	{
		size_t required = sizeof(x) + sizeof(z) + sizeof(tag);
		if (dimension != 0) required += sizeof(dimension);
		if (subchunk >= 0) required += sizeof(subchunk);

		buffer.resize(required);
		memcpy(static_cast<void *>(buffer.data()), &x, sizeof(x));
		memcpy(static_cast<void *>(buffer.data() + sizeof(x)), &z, sizeof(z));
		if (dimension != 0) {
			memcpy(static_cast<void *>(buffer.data() + sizeof(x) + sizeof(z)), &dimension, sizeof(dimension));
			memcpy(static_cast<void *>(buffer.data() + sizeof(x) + sizeof(z) + sizeof(dimension)), &tag, sizeof(tag));
			if (subchunk >= 0) {
				memcpy(static_cast<void *>(buffer.data() + sizeof(x) + sizeof(z) + sizeof(dimension) + sizeof(tag)), &subchunk, sizeof(subchunk));
			}
		} else {
			memcpy(static_cast<void *>(buffer.data() + sizeof(x) + sizeof(z)), &tag, sizeof(tag));
			if (subchunk >= 0) {
				memcpy(static_cast<void *>(buffer.data() + sizeof(x) + sizeof(z) + sizeof(tag)), &subchunk, sizeof(subchunk));
			}
		}

		return leveldb::Slice(buffer.data(), buffer.size());
	}

private:
	std::vector<char> buffer;
};

struct SuperchunkKey {
	int32_t dimension;
	int32_t x;
	int32_t z;

	bool operator==(const SuperchunkKey &other) const
	{
		return dimension == other.dimension && x == other.x && z == other.z;
	}
};

namespace std {
	template<> struct hash<SuperchunkKey>
	{
		size_t operator()(const SuperchunkKey &x) const
		{
			return std::hash<int32_t>{}(x.dimension) ^ std::hash<int32_t>{}(x.x) ^ std::hash<int32_t>{}(x.z);
		}
	};
}

constexpr int THREAD_COUNT = 1; // 0 = Auto

constexpr int BLOCKS_PER_CHUNK = 16;
constexpr int CHUNKS_PER_SUPERCHUNK = 16;

typedef std::bitset<CHUNKS_PER_SUPERCHUNK * CHUNKS_PER_SUPERCHUNK> ChunkBitset;
typedef std::unordered_map<SuperchunkKey, ChunkBitset> SuperchunkMap;
SuperchunkMap g_Superchunks;

std::string string_to_hex(const std::string &input)
{
	static const char *const lut = "0123456789ABCDEF";
	size_t len = input.length();

	std::string output;
	output.reserve(2 * len);
	for (size_t i = 0; i < len; ++i)
	{
		const unsigned char c = input[i];
		output.push_back(lut[c >> 4]);
		output.push_back(lut[c & 15]);
	}
	return output;
}

std::string superchunk_to_string(ChunkBitset bitmap)
{
	std::string output;
	const auto size = bitmap.size();
	output.reserve(size * 2);
	for (size_t i = 0; i < size; ++i) {
		const auto bit = bitmap[i] != 0;
		output.push_back(bit ? 'X' : ' ');
		if ((i % CHUNKS_PER_SUPERCHUNK) == (CHUNKS_PER_SUPERCHUNK - 1)) {
			output.push_back('\n');
		} else {
			output.push_back(' ');
		}
	}
	return output;
}

int read_signed_varint(const std::string &data, size_t &cursor)
{
	int result = 0;
	for (int i = 0; i <= 5; ++i) {
		int8_t value;
		memcpy(&value, data.data() + cursor, sizeof(int8_t));
		cursor += sizeof(int8_t);

		result |= (value & ~(1 << 7)) << (7 * i);

		if ((value & (1 << 7)) == 0) {
			return result;
		}
	}

	throw std::runtime_error("SVarInt used too many bytes");
}

class BlockStorage
{
public:
	BlockStorage(std::istream &stream) {
		stream.read(reinterpret_cast<char *>(&bitsPerBlock), sizeof(int8_t));

		if ((bitsPerBlock & 1) == 1) {
			throw std::runtime_error("block storages persisted for network transfer are not supported");
		}
		bitsPerBlock >>= 1;

		int blockWords = (BLOCKS_PER_CHUNK * BLOCKS_PER_CHUNK * BLOCKS_PER_CHUNK) / (32 / bitsPerBlock);
		if ((32 % bitsPerBlock) != 0) {
			blockWords += 1;
		}

		blockData.resize(blockWords);
		stream.read(reinterpret_cast<char *>(&blockData[0]), blockWords * sizeof(int32_t));

		int32_t paletteCount;
		stream.read(reinterpret_cast<char *>(&paletteCount), sizeof(int32_t));
		palette.reserve(paletteCount);

		nbt::io::stream_reader reader(stream, endian::little);
		for (int pi = 0; pi < paletteCount; ++pi) {
			auto tag = reader.read_compound();
			palette.push_back(std::move(tag.second));
		}
	}

	BlockStorage(const BlockStorage &) = delete;
	void operator=(const BlockStorage &) = delete;

	const nbt::tag_compound &operator[](size_t idx) const {
		if (idx > (BLOCKS_PER_CHUNK * BLOCKS_PER_CHUNK * BLOCKS_PER_CHUNK)) {
			throw std::runtime_error("block request out of bounds");
		}

		size_t wordIdx = idx / (32 / bitsPerBlock);
		uint32_t blockWord = blockData[wordIdx];

		size_t wordOffs = (idx % (32 / bitsPerBlock)) * bitsPerBlock;
		uint32_t blockIdx = (blockWord >> wordOffs) & (0xffffffffUL >> (32 - bitsPerBlock));

		if (blockIdx >= palette.size()) {
			throw std::runtime_error("block index out of bounds of palette");
		}

		return *palette[blockIdx];
	}

private:
	int8_t bitsPerBlock;
	std::vector<uint32_t> blockData;
	std::vector<std::unique_ptr<nbt::tag_compound>> palette;
};

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
	for (int x = 0; x < CHUNKS_PER_SUPERCHUNK; ++x) {
		for (int z = 0; z < CHUNKS_PER_SUPERCHUNK; ++z) {
			int i = (x * CHUNKS_PER_SUPERCHUNK) + z;
			if (!bitmap[i]) {
				// There is no chunk here.
				continue;
			}

			for (int8_t y = 0; true; ++y) {
				ChunkDbKey chunkKey = {};
				chunkKey.x = (key.x * CHUNKS_PER_SUPERCHUNK) + x;
				chunkKey.z = (key.z * CHUNKS_PER_SUPERCHUNK) + z;
				chunkKey.dimension = key.dimension;
				chunkKey.tag = Tag::SubChunkPrefix;
				chunkKey.subchunk = y;
				
				std::string value;
				leveldb::Status status = db->Get(options, chunkKey, &value);

				// Subchunks work like a stack, so we loop up until we can't find any more
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

					break;
				}

				if (!status.ok()) {
					throw std::runtime_error("failed to read subchunk key");
				}

				auto subchunk = parse_subchunk(value);

				for (int bx = 0; bx < BLOCKS_PER_CHUNK; ++bx) {
					for (int by = 0; by < BLOCKS_PER_CHUNK; ++by) {
						for (int bz = 0; bz < BLOCKS_PER_CHUNK; ++bz) {
							size_t idx = (((bx * 16) + by) * 16) + bz;
							const auto &block = (*subchunk[0])[idx];
							std::cout << idx << " = " << block << std::endl;
						}
					}
				}
			}
		}
	}
}

// TODO: Consider options for palleted PNGs, should be much smaller with what we're writing.
// Pack RGBA and use as a map key to the palette index?
class PngWriter
{
public:
	PngWriter(const std::string &filename, uint32_t width, uint32_t height):
		filename(filename), width(width), height(height) {}

	void set_pixel(uint32_t x, uint32_t y, int8_t r, int8_t g, int8_t b, int8_t a) {

	}

	void write() {
		std::ofstream stream(filename, std::ios_base::out | std::ios_base::binary);

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
			std::ostream *file = reinterpret_cast<std::ostream *>(png_get_io_ptr(ctx));
			if (!file->write(reinterpret_cast<char *>(data), length)) {
				png_error(ctx, "write error");
			}
		}, [](png_struct *ctx) {
			std::ostream *file = reinterpret_cast<std::ostream *>(png_get_io_ptr(ctx));
			if (!file->flush()) {
				png_error(ctx, "flush error");
			}
		});

		png_set_IHDR(context, info, width, height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

		// png_set_PLTE ?

		png_write_info(context, info);

		std::vector<png_byte> data = {
				255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255,
				255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255,
				255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255,
				255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255,
		};

		std::vector<png_byte *> rows = {
			&data[0], &data[16], &data[32], &data[48],
		};

		png_write_image(context, rows.data());

		png_write_end(context, info);

		png_destroy_write_struct(&context, &info);

		stream.close();
	}

private:
	std::string filename;
	uint32_t width;
	uint32_t height;
};

int main()
{
	PngWriter pngWriter("test.png", 4, 4);
	pngWriter.write();

	return 0;

	leveldb::Options options;

	//create a bloom filter to quickly tell if a key is in the database or not
	options.filter_policy = leveldb::NewBloomFilterPolicy(10);

	//create a 100 mb cache
	options.block_cache = leveldb::NewLRUCache(100 * 1024 * 1024);

	//create a 4mb write buffer, to improve compression and touch the disk less
	options.write_buffer_size = 4 * 1024 * 1024;

	//use the new raw-zip compressor to write (and read)
	options.compressors[0] = new leveldb::ZlibCompressorRaw(-1);

	//also setup the old, slower compressor for backwards compatibility. This will only be used to read old compressed blocks.
	options.compressors[1] = new leveldb::ZlibCompressor();

	//create a reusable memory space for decompression so it allocates less
	leveldb::ReadOptions readOptions;
	readOptions.decompress_allocator = new leveldb::DecompressAllocator();

	leveldb::DB *db;
	leveldb::Status status = leveldb::DB::Open(options, "C:\\Users\\asherkin\\Downloads\\mcpe_viz-master\\work\\4t08XdwoCQA=\\db", &db);

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

		// This gets us round-to-negative-infinty behaviour
		if (chunkKey.x < 0) chunkKey.x -= CHUNKS_PER_SUPERCHUNK - 1;
		chunkKey.x = chunkKey.x / CHUNKS_PER_SUPERCHUNK;

		if (chunkKey.z < 0) chunkKey.z -= CHUNKS_PER_SUPERCHUNK - 1;
		chunkKey.z = chunkKey.z / CHUNKS_PER_SUPERCHUNK;

		g_Superchunks[chunkKey][idx] = true;

		//if (chunkKey.dimension == 0 && chunkKey.x == 0 && chunkKey.z == 0) {
		//	std::cout << string_to_hex(name) << " " << size << std::endl;
		//}
		continue;
	
		//if (size == 43 && iter->key().starts_with("player_")) {
		//	std::cout << size << " " << iter->key().data() << std::endl;
		{
			//std::cout << size << std::endl;

			std::string value = iter->value().ToString();
			std::istringstream is(value);
			nbt::io::stream_reader reader(is, endian::little);

			while (is && is.peek() != EOF) {
				try {
					auto t = reader.read_compound();
					std::cout << "\"" << t.first << "\" = " << *t.second << std::endl;
				} catch (std::exception) {

				}
			}

			if (!is) {
				// throw std::runtime_error("truncated?");
			}
		}
	}

	std::cout << " Done" << std::endl;

	size_t bitcount = 0;
	for (auto &it : g_Superchunks) {
		//std::cout << it.first.dimension << " " << (it.first.x * CHUNKS_PER_SUPERCHUNK * BLOCKS_PER_CHUNK) << " " << (it.first.z * CHUNKS_PER_SUPERCHUNK * BLOCKS_PER_CHUNK) << std::endl << superchunk_to_string(it.second) << std::endl;
		bitcount += it.second.count();
	}

	std::cout << "Chunk count: " << count << std::endl;
	std::cout << "Superchunk count: " << g_Superchunks.size() << std::endl;

	if (count != bitcount) {
		throw std::runtime_error("sanity check failed, chunk count != bitcount");
	}

	unsigned int thread_count = (THREAD_COUNT != 0) ? THREAD_COUNT : std::thread::hardware_concurrency();
	std::cout << "Starting " << thread_count << " worker threads..." << std::endl;

	std::vector<std::thread> workers;

#if 1
	// Eager queue. (THIS IS FASTER, AND BETTER WORK BALANCE)
	std::mutex chunk_mutex;
	auto chunk = g_Superchunks.cbegin();
	auto end = g_Superchunks.cend();
	for (unsigned int i = 0; i < thread_count; ++i) {
		workers.emplace_back([i, &chunk_mutex, &chunk, &end, db, &readOptions]() {
			while (true) {
				std::pair<SuperchunkKey, ChunkBitset> it;
				{
					std::lock_guard<std::mutex> lock(chunk_mutex);
					if (chunk == end) {
						break;
					}
					it = *chunk++;
				}

				process_superchunk(db, readOptions, it.first, it.second);
			}
		});
	}
#else
	// Cache locality
	auto start = g_Superchunks.cbegin();
	auto per_thread = g_Superchunks.size() / thread_count;

	for (unsigned int i = 0; i < thread_count; ++i) {
		auto end = start;
		if (i == (thread_count - 1)) {
			end = g_Superchunks.cend();
		} else {
			for (size_t ii = 0; ii < per_thread; ++ii) {
				end++;
			}
		}
		workers.emplace_back([i, start, end]() {
			for (auto it = start; it != end; ++it) {
				process_superchunk(db, readOptions, it->first, it->second);
			}
		});
		start = end;
	}
#endif

	for (auto &it : workers) {
		if (it.joinable()) {
			it.join();
		}
	}
	workers.clear();

    std::cout << "Hello World!" << std::endl;
}
