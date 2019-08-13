#include "ChunkDbKey.h"

ChunkDbKey::operator leveldb::Slice()
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
	}
	else {
		memcpy(static_cast<void *>(buffer.data() + sizeof(x) + sizeof(z)), &tag, sizeof(tag));
		if (subchunk >= 0) {
			memcpy(static_cast<void *>(buffer.data() + sizeof(x) + sizeof(z) + sizeof(tag)), &subchunk, sizeof(subchunk));
		}
	}

	return leveldb::Slice(buffer.data(), buffer.size());
}
