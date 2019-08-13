#include "BlockStorage.h"

#include "Minecraft.h"

#include <io/stream_reader.h>

BlockStorage::BlockStorage(std::istream &stream)
{
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

const nbt::tag_compound & BlockStorage::operator[](size_t idx) const
{
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
