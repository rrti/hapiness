#ifndef HAPINESS_STRING_UTIL_HDR
#define HAPINESS_STRING_UTIL_HDR

#include <string>
#include <vector>

namespace util {
	// split <str> on each character in <chrs>
	std::vector<std::string> str_split(const std::string& str, const std::string& chrs);

	std::string str_to_uppercase(const std::string& str);

	std::string str_latin1_to_utf8(const std::string& str);

	size_t str_size(const char* begin, const char* end);
}

#endif

