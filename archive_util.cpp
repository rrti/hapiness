#include <algorithm>
#include <numeric>

#include "archive_util.hpp"
#include "decompress_util.hpp"
#include "string_util.hpp"

namespace util {
	// decrypt buffer with <key> and <seed> (position of the starting byte)
	static bool decrypt_buffer(uint8_t key, uint8_t seed, char* buffer, size_t buffer_size) {
		if (key == 0)
			return false;

		for (size_t i = 0; i < buffer_size; ++i) {
			const uint8_t pos = seed + static_cast<uint8_t>(i);
			const uint8_t dec = buffer[i] ^ (pos ^ key);

			buffer[i] = dec;
		}

		return true;
	}

	static void read_decrypt_buffer(std::istream& stream, uint8_t key, char* buffer, size_t size) {
		const uint8_t seed = static_cast<uint8_t>(stream.tellg());

		stream.read(buffer, size);
		decrypt_buffer(key, seed, buffer, stream.gcount());
	}


	template <typename T>
	static T read_raw_value(std::istream& stream) {
		T val;
		stream.read(reinterpret_cast<char*>(&val), sizeof(T));
		return val;
	}

	template <typename T>
	static T read_decrypt_raw_value(std::istream& stream, uint8_t key) {
		T val;
		read_decrypt_buffer(stream, key, reinterpret_cast<char*>(&val), sizeof(T));
		return val;
	}

	template <typename T>
	static void read_decrypt_raw_array(std::istream& stream, uint8_t key, T* buffer, size_t size) {
		read_decrypt_buffer(stream, key, reinterpret_cast<char*>(buffer), sizeof(T) * size);
	}


	// note: "decrypt" would be a misnomer here
	static void decode_chunk_buffer(char* buffer, size_t size) {
		#if 1
		std::transform(buffer, buffer + size, buffer, [&](char& b) { return ((b - (&b - buffer)) ^ (&b - buffer)); });
		#else
		for (size_t i = 0; i < size; ++i) {
			const uint8_t pos = static_cast<uint8_t>(i);
			const uint8_t dec = (buffer[i] - pos) ^ pos;
			buffer[i] = dec;
		}
		#endif
	}

	static uint32_t compute_buffer_checksum(const char* buffer, size_t size) {
		// note: binop LHS has to be uint32_t due to definition of accumulate
		return (std::accumulate(buffer, buffer + size, 0u, [](uint32_t sum, char byte) { return (sum + static_cast<uint8_t>(byte)); }));
	}


	hpi_archive::arch_entry
	hpi_archive::make_arch_entry(const hpi_arch_entry& entry, const std::vector<char>& buffer) {
		const size_t name_size = str_size(buffer.data() + entry.name_offset, buffer.data() + buffer.size());
		const size_t entry_ofs = entry.data_offset;

		char error[256];

		if (name_size == std::string::npos) {
			snprintf(error, sizeof(error) - 1, "[%s] unterminated entry name", __func__);
			throw hpi_exception(error);
			return {};
		}

		if (entry.is_path != 0) {
			if ((entry_ofs + sizeof(hpi_path_data)) > buffer.size()) {
				snprintf(error, sizeof(error) - 1, "[%s] path data-offset %lu greater than size %lu", __func__, entry_ofs + sizeof(hpi_path_data), buffer.size());
				throw hpi_exception(error);
				return {};
			}

			const hpi_path_data* hpd = reinterpret_cast<const hpi_path_data*>(buffer.data() + entry_ofs);
			const     path_data   pd = std::move(make_path_data(*hpd, buffer));
			return {{buffer.data() + entry.name_offset, name_size}, pd};
		}

		if ((entry_ofs + sizeof(hpi_file_data)) > buffer.size()) {
			snprintf(error, sizeof(error) - 1, "[%s] file data-offset %lu greater than size %lu", __func__, entry_ofs + sizeof(hpi_file_data), buffer.size());
			throw hpi_exception(error);
			return {};
		}

		const hpi_file_data* hfd = reinterpret_cast<const hpi_file_data*>(buffer.data() + entry_ofs);
		const     file_data   fd = std::move(make_file_data(*hfd));
		return {{buffer.data() + entry.name_offset, name_size}, fd};
	}


	hpi_archive::path_data
	hpi_archive::make_path_data(const hpi_path_data& path, const std::vector<char>& buffer) {
		char error[256];

		if ((path.entry_list_offset + (path.number_of_entries * sizeof(hpi_arch_entry))) > buffer.size()) {
			snprintf(error, sizeof(error) - 1, "[%s] dir-entry list offset greater than size %lu", __func__, buffer.size());
			throw hpi_exception(error);
			return {};
		}

		const char* path_start_addr = buffer.data() + path.entry_list_offset;
		const hpi_arch_entry* hpi_path_entries = reinterpret_cast<const hpi_arch_entry*>(path_start_addr);

		std::vector<arch_entry> v(path.number_of_entries);

		for (size_t i = 0, n = v.size(); i < n; ++i) {
			v[i] = std::move(make_arch_entry(hpi_path_entries[i], buffer));
		}

		return {std::move(v)};
	}

	bool hpi_archive::open(std::istream* istream) {
		// note: caller must open stream
		const hpi_version archive_version = read_raw_value<hpi_version>(*(stream = istream));
		const hpi_header archive_header = read_raw_value<hpi_header>(*stream);
		char error[256];

		std::vector<char> buffer;

		if (archive_version.magic != HPI_MAGIC_NUMBER) {
			snprintf(error, sizeof(error) - 1, "[%s] invalid HPI magic-number %u", __func__, archive_version.magic);
			throw hpi_exception(error);
			return false;
		}

		if (archive_version.version != HPI_VERSION_NUMBER) {
			snprintf(error, sizeof(error) - 1, "[%s] unsupported HPI version-number %u", __func__, archive_version.version);
			throw hpi_exception(error);
			return false;
		}

		// transform key
		decrypt_key |= (static_cast<uint8_t>(archive_header.header_key) << 2);
		decrypt_key |= (static_cast<uint8_t>(archive_header.header_key) >> 6);

		stream->seekg(archive_header.start);
		buffer.resize(archive_header.directory_size, 0);
		read_decrypt_buffer(*stream, decrypt_key, buffer.data() + archive_header.start, archive_header.directory_size - archive_header.start);

		if ((archive_header.start + sizeof(hpi_path_data)) > archive_header.directory_size) {
			snprintf(error, sizeof(error) - 1, "[%s] root-dir offset %lu greater than dir-size %u", __func__, archive_header.start + sizeof(hpi_path_data), archive_header.directory_size);
			throw hpi_exception(error);
			return false;
		}

		root_path = std::move(make_path_data(*reinterpret_cast<hpi_path_data*>(buffer.data() + archive_header.start), buffer));
		return true;
	}


	bool hpi_archive::extract(const hpi_archive::file_data& file, std::vector<char>& buffer) const {
		switch (file.compression_type) {
			case COMPRESSION_TYPE_NULL: {
				stream->seekg(file.offset);
				// stream->read(buffer.data(), file.size);
				read_decrypt_buffer(*stream, decrypt_key, buffer.data(), file.size);
				return true;
			} break;
			case COMPRESSION_TYPE_LZ77:
			case COMPRESSION_TYPE_ZLIB: {
				return (extract_compressed(file, buffer));
			} break;
			default: {
			} break;
		}

		char error[256];
		snprintf(error, sizeof(error) - 1, "[%s] invalid compression type %u", __func__, file.compression_type);
		throw hpi_exception(error);
		return false;
	}

	bool hpi_archive::extract_compressed(const hpi_archive::file_data& file, std::vector<char>& buffer) const {
		char error[256];

		// add one extra chunk if size is not a multiple of 64K
		std::vector<uint32_t> chunk_sizes((file.size / 65536) + ((file.size % 65536) != 0), 0);
		std::vector<char> chunk_buffer;

		stream->seekg(file.offset);
		read_decrypt_raw_array(*stream, decrypt_key, chunk_sizes.data(), chunk_sizes.size());

		for (size_t i = 0, buffer_offset = 0, n = chunk_sizes.size(); i < n; ++i) {
			const hpi_chunk chunk_header = read_decrypt_raw_value<hpi_chunk>(*stream, decrypt_key);

			if (chunk_header.magic != HPI_CHUNK_MAGIC_NUMBER) {
				snprintf(error, sizeof(error) - 1, "[%s] invalid header magic-number %u for chunk %lu", __func__, chunk_header.magic, i);
				throw hpi_exception(error);
				return false;
			}

			if ((buffer_offset + chunk_header.decompressed_size) > file.size) {
				snprintf(error, sizeof(error) - 1, "[%s] extracted file size %lu larger than expected size %lu for chunk %lu", __func__, buffer_offset + chunk_header.decompressed_size, file.size, i);
				throw hpi_exception(error);
				return false;
			}

			chunk_buffer.clear();
			chunk_buffer.resize(chunk_header.compressed_size, 0);
			read_decrypt_buffer(*stream, decrypt_key, chunk_buffer.data(), chunk_header.compressed_size);

			const uint32_t checksum = compute_buffer_checksum(chunk_buffer.data(), chunk_header.compressed_size);

			if (checksum != chunk_header.checksum) {
				snprintf(error, sizeof(error) - 1, "[%s] invalid buffer checksum %u for chunk %lu", __func__, checksum, i);
				throw hpi_exception(error);
				return false;
			}

			if (chunk_header.encoded != 0)
				decode_chunk_buffer(chunk_buffer.data(), chunk_header.compressed_size);

			switch (chunk_header.compression_type) {
				case COMPRESSION_TYPE_NULL: {
					if (chunk_header.compressed_size != chunk_header.decompressed_size) {
						snprintf(error, sizeof(error) - 1, "[%s] size mismatch (%u vs %u) for uncompressed chunk %lu", __func__, chunk_header.decompressed_size, chunk_header.compressed_size, i);
						throw hpi_exception(error);
						return false;
					}

					std::copy(chunk_buffer.data(), chunk_buffer.data() + chunk_header.compressed_size, buffer.data() + buffer_offset);
					buffer_offset += chunk_header.decompressed_size;
				} break;

				case COMPRESSION_TYPE_LZ77: {
					decompress_lz77(chunk_buffer.data(), chunk_header.compressed_size, buffer.data() + buffer_offset, chunk_header.decompressed_size);
					buffer_offset += chunk_header.decompressed_size;
				} break;

				case COMPRESSION_TYPE_ZLIB: {
					decompress_zlib(chunk_buffer.data(), chunk_header.compressed_size, buffer.data() + buffer_offset, chunk_header.decompressed_size);
					buffer_offset += chunk_header.decompressed_size;
				} break;

				default: {
					snprintf(error, sizeof(error) - 1, "[%s] invalid compression type %u for chunk %lu", __func__, chunk_header.compression_type, i);
					throw hpi_exception(error);
					return false;
				} break;
			}
		}

		return true;
	}


	#ifdef USE_STD_OPTIONAL
	struct file_to_opt_visitor: public boost::static_visitor<std::optional<std::reference_wrapper<const hpi_archive::file_data>>> {
		std::optional<std::reference_wrapper<const hpi_archive::file_data>> operator()(const hpi_archive::file_data& fd) const { return           fd; }
		std::optional<std::reference_wrapper<const hpi_archive::file_data>> operator()(const hpi_archive::path_data&   ) const { return std::nullopt; }
    };

	struct path_to_opt_visitor: public boost::static_visitor<std::optional<std::reference_wrapper<const hpi_archive::path_data>>> {
		std::optional<std::reference_wrapper<const hpi_archive::path_data>> operator()(const hpi_archive::file_data&   ) const { return std::nullopt; }
		std::optional<std::reference_wrapper<const hpi_archive::path_data>> operator()(const hpi_archive::path_data& pd) const { return           pd; }
	};
	#endif


	#ifdef USE_STD_OPTIONAL
	static std::optional<std::reference_wrapper<const hpi_archive::file_data>>
	#else
	static const hpi_archive::file_data*
	#endif
	find_file_inner(const hpi_archive::path_data& path, const std::string& name) {
		const auto pred = [name](const hpi_archive::arch_entry& e) { return (str_to_uppercase(e.name) == str_to_uppercase(name)); };
		const auto iter = std::find_if(path.entries.begin(), path.entries.end(), pred);

		#ifdef USE_STD_OPTIONAL
		if (iter == path.entries.end())
			return std::nullopt;

		return (boost::apply_visitor(file_to_opt_visitor(), iter->data));
		#else
		if (iter == path.entries.end())
			return nullptr;

		return &boost::get<hpi_archive::file_data>(iter->data);
		#endif
	}


	#ifdef USE_STD_OPTIONAL
	std::optional<std::reference_wrapper<const hpi_archive::file_data>>
	#else
	const hpi_archive::file_data*
	#endif
	hpi_archive::find_file(const std::string& path_str) const {
		const std::vector<std::string>& split_path_strs = str_split(path_str, {'/'});

		const path_data* path = &get_root_path();

		// descend down the archive
		for (auto comp_iter = split_path_strs.cbegin(), comp_end = --split_path_strs.cend(); comp_iter != comp_end; ++comp_iter) {
			const std::string& path_comp = *comp_iter;

			const auto beg = path->entries.begin();
			const auto end = path->entries.end();

			const auto pred = [&path_comp](const arch_entry& e) { return (str_to_uppercase(e.name) == str_to_uppercase(path_comp)); };
			const auto iter = std::find_if(beg, end, pred);

			#ifdef USE_STD_OPTIONAL
			{
				if (iter == end)
					return std::nullopt;

				const auto found_path = boost::apply_visitor(path_to_opt_visitor(), iter->data);

				if (!found_path)
					return std::nullopt;

				path = &(found_path->get());
			}
			#else
			{
				if (iter == end)
					return nullptr;

				const hpi_archive::path_data* found_path = &boost::get<hpi_archive::path_data>(iter->data);

				if (found_path == nullptr)
					return nullptr;

				path = found_path;
			}
			#endif
		}

		// find file in directory
		return (find_file_inner(*path, split_path_strs.back()));
	}

	#ifdef USE_STD_OPTIONAL
	std::optional<std::reference_wrapper<const hpi_archive::path_data>>
	#else
	const hpi_archive::path_data*
	#endif
	hpi_archive::find_path(const std::string& path_str) const {
		const std::vector<std::string>& split_path_strs = str_split(path_str, {'/'});

		const path_data* path = &get_root_path();

		// descend down the archive
		for (auto comp_iter = split_path_strs.cbegin(), comp_end = --split_path_strs.cend(); comp_iter != comp_end; ++comp_iter) {
			const std::string& path_comp = *comp_iter;

			const auto beg = path->entries.begin();
			const auto end = path->entries.end();

			const auto pred = [&path_comp](const arch_entry& e) { return (str_to_uppercase(e.name) == str_to_uppercase(path_comp)); };
			const auto iter = std::find_if(beg, end, pred);

			#ifdef USE_STD_OPTIONAL
			{
				if (iter == end)
					return std::nullopt;

				const auto found_path = boost::apply_visitor(path_to_opt_visitor(), iter->data);

				if (!found_path)
					return std::nullopt;

				path = &(found_path->get());
			}
			#else
			{
				if (iter == end)
					return nullptr;

				const hpi_archive::path_data* found_path = &boost::get<hpi_archive::path_data>(iter->data);

				if (found_path == nullptr)
					return nullptr;

				path = found_path;
			}
			#endif
		}

		#ifdef USE_STD_OPTIONAL
		return *path;
		#else
		return path;
		#endif
	}
}

