#pragma once

#include <tagfwd.h>

#include <istream>
#include <vector>

class BlockStorage
{
public:
	BlockStorage(std::istream &stream);

	BlockStorage(const BlockStorage &) = delete;
	void operator=(const BlockStorage &) = delete;

	const nbt::tag_compound &operator[](size_t idx) const;

private:
	int8_t bitsPerBlock;
	std::vector<uint32_t> blockData;
	std::vector<std::unique_ptr<nbt::tag_compound>> palette;
};
