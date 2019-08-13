#include "Helpers.h"

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
