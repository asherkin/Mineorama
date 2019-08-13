#pragma once

#include <string>

std::string string_to_hex(const std::string &input);

int read_signed_varint(const std::string &data, size_t &cursor);
