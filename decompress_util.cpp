#include <cstdint>
#include <zlib.h>

#include "decompress_util.hpp"
#include "archive_util.hpp"

namespace util {
	void decompress_lz77(const char* in, size_t len, char* out, size_t max_bytes) {
		char window[4096];
		char error[256];

		size_t in_pos = 0;
		size_t out_pos = 0;
		uint32_t window_pos = 1;

		while (true) {
			if (in_pos >= len) {
				snprintf(error, sizeof(error) - 1, "[%s] expected tag, got end of input", __func__);
				throw hpi_exception(error);
			}

			uint8_t tag = static_cast<uint8_t>(in[in_pos++]);

			for (uint32_t i = 0; i < 8; ++i) {
				if ((tag & 1) == 0) {
					// next byte is a literal byte
					if (in_pos >= len) {
						snprintf(error, sizeof(error) - 1, "[%s] expected byte, got end of input", __func__);
						throw hpi_exception(error);
					}

					if (out_pos >= max_bytes) {
						snprintf(error, sizeof(error) - 1, "[%s][literal] exceeded maximum output size", __func__);
						throw hpi_exception(error);
					}

					out[out_pos++] = in[in_pos];
					window[window_pos] = in[in_pos];
					window_pos = (window_pos + 1) & 0xFFF;
					in_pos++;
				} else {
					// next bytes point into the sliding window
					if (in_pos >= (len - 1)) {
						snprintf(error, sizeof(error) - 1, "[%s] expected window offset/length, got end of input", __func__);
						throw hpi_exception(error);
					}

					uint32_t packed_data = *(reinterpret_cast<const uint16_t*>(&in[in_pos]));
					uint32_t offset = packed_data >> 4;
					uint32_t count = (packed_data & 0x0F) + 2;

					in_pos += 2;

					if (offset == 0)
						return;

					if ((out_pos + count) > max_bytes) {
						snprintf(error, sizeof(error) - 1, "[%s][window] exceeded maximum output size", __func__);
						throw hpi_exception(error);
					}

					for (uint32_t x = 0; x < count; ++x) {
						out[out_pos++] = window[offset];
						window[window_pos] = window[offset];

						offset = (offset + 1) & 0xFFF;
						window_pos = (window_pos + 1) & 0xFFF;
					}
				}

				tag >>= 1;
			}
		}
	}

	void decompress_zlib(const char* in, size_t len, char* out, size_t max_bytes) {
		z_stream stream;
		char error[256];

		stream.zalloc = Z_NULL;
		stream.zfree = Z_NULL;
		stream.opaque = Z_NULL;
		stream.avail_in = 0;
		stream.next_in = Z_NULL;

		if (inflateInit(&stream) != Z_OK) {
			snprintf(error, sizeof(error) - 1, "[%s] initialization failed", __func__);
			throw hpi_exception(error);
		}

		stream.avail_in = static_cast<uInt>(len);
		stream.next_in = reinterpret_cast<uint8_t*>(const_cast<char*>(in));
		stream.avail_out = static_cast<uInt>(max_bytes);
		stream.next_out = reinterpret_cast<uint8_t*>(out);

		if (inflate(&stream, Z_NO_FLUSH) != Z_STREAM_END) {
			inflateEnd(&stream);
			snprintf(error, sizeof(error) - 1, "[%s] inflation failed", __func__);
			throw hpi_exception(error);
		}

		inflateEnd(&stream);
	}
}

