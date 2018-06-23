#include <algorithm>
#include <cctype>

#include "string_util.hpp"

namespace util {
	template<typename StrIter>
	static std::vector<std::string> str_split_internal(StrIter str_begin, StrIter str_end, StrIter chrs_begin, StrIter chrs_end) {
		std::vector<std::string> v;

		v.reserve(str_end - str_begin);

		while (true) {
			const auto it = std::find_first_of(str_begin, str_end, chrs_begin, chrs_end);
			v.emplace_back(str_begin, it);

			if (it == str_end)
				break;

			str_begin = it + 1;
		}

		return v;
	};


	std::vector<std::string> str_split(const std::string& str, const std::string& chrs) {
		return str_split_internal(str.begin(), str.end(), chrs.begin(), chrs.end());
	}


	std::string str_to_uppercase(const std::string& str) {
		std::string copy(str);
		std::transform(copy.begin(), copy.end(), copy.begin(), [](char c) { return std::toupper(c); });
		return copy;
	}

	std::string str_latin1_to_utf8(const std::string& str) {
		std::string output;

		for (char c: str) {
			const uint8_t ch = static_cast<uint8_t>(c);

			if (ch < 0x80) {
				// regular ASCII
				output.push_back(ch);
			} else {
				// extended ASCII
				output.push_back(static_cast<char>(0xc0 | (ch       ) >> 6));
				output.push_back(static_cast<char>(0x80 | (ch & 0x3f)     ));
			}
		}

		return output;
    }


	size_t str_size(const char* begin, const char* end) {
		size_t count = 0;

		while (begin != end) {
			if (*begin == '\0')
				return count;

			count += 1;
			begin += 1;
		}

		// no zero-terminator encountered between begin and end
		return std::string::npos;
	}
}

