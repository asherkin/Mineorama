#pragma once

#include <string>

std::string string_to_hex(const std::string &input);

int read_signed_varint(const std::string &data, size_t &cursor);

template<class T>
inline void hash_combine(std::size_t &seed, const T &v)
{
	std::hash<T> hasher;
	seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}
