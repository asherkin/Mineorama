#pragma once

#include "Minecraft.h"

#include <leveldb/db.h>

#include <vector>

struct ChunkDbKey {
	int32_t x;
	int32_t z;
	int32_t dimension;
	Tag tag;
	uint8_t subchunk;

	operator leveldb::Slice();

private:
	std::vector<char> buffer;
};
