#pragma once

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
			return std::hash<int32_t>{}(x.dimension) ^ std::hash<int32_t>{}(x.x) ^ std::hash<int32_t>{}(x.z);
		}
	};
}
