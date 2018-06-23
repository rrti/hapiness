#ifndef HAPINESS_DECOMPRESS_UTIL_HDR
#define HAPINESS_DECOMPRESS_UTIL_HDR

namespace util {
	void decompress_lz77(const char* in, size_t len, char* out, size_t max_bytes);
	void decompress_zlib(const char* in, size_t len, char* out, size_t max_bytes);
}

#endif

