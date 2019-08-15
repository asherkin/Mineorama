#pragma once

#include "Helpers.h"

struct SuperchunkKey {
	int32_t dimension;
	int32_t x;
	int32_t z;

	inline bool operator==(const SuperchunkKey &other) const
	{
		return dimension == other.dimension && x == other.x && z == other.z;
	}
};

namespace std {
	template<> struct hash<SuperchunkKey>
	{
		size_t operator()(const SuperchunkKey &x) const
		{
			auto h = hash<int32_t>{}(x.dimension);
			hash_combine(h, x.x);
			hash_combine(h, x.z);
			return h;
		}
	};
}
