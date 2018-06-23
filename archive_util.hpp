#ifndef HAPINESS_ARCHIVE_UTIL_HDR
#define HAPINESS_ARCHIVE_UTIL_HDR

#include <cstdint>
#include <istream>
#ifdef USE_STD_OPTIONAL
#include <optional>
#endif
#include <vector>

#include <boost/variant.hpp>


namespace util {
	// magic number at start of HPI header ("HAPI")
	static constexpr unsigned int HPI_MAGIC_NUMBER = 0x49504148;

	// version number for standard HPI files
	static constexpr unsigned int HPI_VERSION_NUMBER = 0x00010000;

	// version number for saved games ("BANK")
	static constexpr unsigned int HPI_BANK_MAGIC_NUMBER = 0x4B4E4142;

	// magic number at start of HPI chunks ("SQSH")
	static constexpr unsigned int HPI_CHUNK_MAGIC_NUMBER = 0x48535153;


	struct hpi_exception: public std::runtime_error {
	public:
		hpi_exception(const char* message): runtime_error(message) {
		}
	};


	#pragma pack(1)
	struct hpi_version {
		// always "HAPI"
		uint32_t magic = 0;

		// "BANK" if file is a saved game
		uint32_t version = 0;
	};

	struct hpi_header {
		// size of directory in bytes, including directory header
		uint32_t directory_size = 0;

		// decryption key
		uint32_t header_key = 0;

		// offset to start of directory
		uint32_t start = 0;
	};

	struct hpi_path_data {
		uint32_t number_of_entries = 0;
		uint32_t entry_list_offset = 0;
	};

	struct hpi_file_data {
		// pointer to start of file data
		uint32_t data_offset = 0;

		// size of decompressed file in bytes
		uint32_t file_size = 0;

		// 0 for no compression, 1 for LZ77 compression, 2 for ZLib compression
		uint8_t compression_type = 0;
	};

	struct hpi_arch_entry {
		// pointer to null-terminated string containing entry name
		uint32_t name_offset = 0;

		// pointer to entry data; varies depending on whether entry is file or path
		uint32_t data_offset = 0;

		// 1 if entry is a directory, 0 if it is a file
		uint8_t is_path = 0;
	};

	struct hpi_chunk {
		uint32_t magic = 0;
		uint8_t version = 0;
		uint8_t compression_type = 0;
		uint8_t encoded = 0;
		// note: compressed_size should match decompressed_size if type=NOOP
		uint32_t compressed_size = 0;
		uint32_t decompressed_size = 0;
		uint32_t checksum = 0;
	};

	static_assert(sizeof(hpi_version   ) == (sizeof(uint32_t) * 2                                             ), "");
	static_assert(sizeof(hpi_header    ) == (sizeof(uint32_t) * 3                                             ), "");
	static_assert(sizeof(hpi_path_data ) == (sizeof(uint32_t) * 2                                             ), "");
	static_assert(sizeof(hpi_file_data ) == (sizeof(uint32_t) * 2 + sizeof(uint8_t)                           ), "");
	static_assert(sizeof(hpi_arch_entry) == (sizeof(uint32_t) * 2 + sizeof(uint8_t)                           ), "");
	static_assert(sizeof(hpi_chunk     ) == (sizeof(uint32_t)     + sizeof(uint8_t) * 3 + sizeof(uint32_t) * 3), "");
	#pragma pack()


	enum {
		COMPRESSION_TYPE_NULL = 0,
		COMPRESSION_TYPE_LZ77 = 1,
		COMPRESSION_TYPE_ZLIB = 2,
	};


	class hpi_archive {
	public:
		struct arch_entry;
		struct file_data {
			file_data() noexcept = default;

			size_t offset = 0;
			size_t size = 0;

			uint8_t compression_type = COMPRESSION_TYPE_NULL;
		};
		struct path_data {
			std::vector<arch_entry> entries;
		};
		struct arch_entry {
			std::string name;
			boost::variant<file_data, path_data> data;
		};

	public:
		hpi_archive() = default;
		hpi_archive(std::istream* istream) { open(istream); }

		const path_data& get_root_path() const { return root_path; }
		const std::vector<arch_entry>& get_root_entries() const { return root_path.entries; }

		#ifdef USE_STD_OPTIONAL
		std::optional<std::reference_wrapper<const file_data>> find_file(const std::string& path) const;
		std::optional<std::reference_wrapper<const path_data>> find_path(const std::string& path) const;
		#else
		const file_data* find_file(const std::string& path) const;
		const path_data* find_path(const std::string& path) const;
		#endif

		bool open(std::istream* istream);
		bool extract(const file_data& file, std::vector<char>& buffer) const;
		bool extract_compressed(const file_data& file, std::vector<char>& buffer) const;

	private:
		hpi_archive::file_data make_file_data(const hpi_file_data& file) { return {file.data_offset, file.file_size, static_cast<uint8_t>(file.compression_type)}; }
		hpi_archive::arch_entry make_arch_entry(const hpi_arch_entry& entry, const std::vector<char>& buffer);
		hpi_archive::path_data make_path_data(const hpi_path_data& path, const std::vector<char>& buffer);

	private:
		std::istream* stream = nullptr;

		path_data root_path;

		uint8_t decrypt_key = 0;
	};
}

#endif

