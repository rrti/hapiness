#include <cstdio>
#include <fstream>
#include <string>
#include <boost/filesystem.hpp>

#include "archive_util.hpp"

namespace fs = boost::filesystem;


static const char* compression_type_str(uint8_t type) {
	switch (type) {
		case util::COMPRESSION_TYPE_NULL: { return "null-"; } break;
		case util::COMPRESSION_TYPE_LZ77: { return "lz77-"; } break;
		case util::COMPRESSION_TYPE_ZLIB: { return "zlib-"; } break;
		default                         : {                 } break;
	}

	return "????";
}


static void print_path(const std::string& parent, const std::string& name, const util::hpi_archive::path_data& d);
static void print_file(const std::string& parent, const std::string& name, const util::hpi_archive::file_data& f);

static void print_entry(const std::string& path, const util::hpi_archive::arch_entry& entry) {
	if (const util::hpi_archive::file_data* f = boost::get<util::hpi_archive::file_data>(&entry.data); f != nullptr) {
		print_file(path, entry.name, *f);
		return;
	}

	if (const util::hpi_archive::path_data* d = boost::get<util::hpi_archive::path_data>(&entry.data); d != nullptr) {
		print_path(path, entry.name, *d);
		return;
	}
}


static void print_path(const std::string& parent, const std::string& name, const util::hpi_archive::path_data& d) {
	if (parent.empty()) {
		for (const util::hpi_archive::arch_entry& entry: d.entries) {
			print_entry(name, entry);
		}
	} else {
		for (const util::hpi_archive::arch_entry& entry: d.entries) {
			print_entry(parent + "/" + name, entry);
		}
	}
}

static void print_file(const std::string& parent, const std::string& name, const util::hpi_archive::file_data& f) {
	if (parent.empty()) {
		fprintf(stdout, "\t%s (%lu bytes, %scompressed)\n", name.c_str(), f.size, compression_type_str(f.compression_type));
	} else {
		fprintf(stdout, "\t%s/%s (%lu bytes, %scompressed)\n", parent.c_str(), name.c_str(), f.size, compression_type_str(f.compression_type));
	}
}


static int handle_list_files_command(const std::string& archive_file_path) {
	fprintf(stdout, "[%s] opening archive '%s'\n", __func__, archive_file_path.c_str());

	std::ifstream file_stream;
	util::hpi_archive file_archive;

	if (file_stream.open(archive_file_path, std::ios::binary), !file_stream.is_open()) {
		fprintf(stderr, "[%s] failed to open archive '%s'\n", __func__, archive_file_path.c_str());
		return EXIT_FAILURE;
	}

	file_archive.open(&file_stream);

	fprintf(stdout, "[%s] listing archive contents\n", __func__);
	print_path("", ".", file_archive.get_root_path());
	return EXIT_SUCCESS;
}

static int handle_extract_file_command(const std::string& archive_file_path, const std::string& src_file_path, const std::string& tgt_file_path) {
	fprintf(stdout, "[%s] opening archive '%s'\n", __func__, archive_file_path.c_str());

	std::ifstream in_file_stream;
	std::ofstream out_file_stream;
	util::hpi_archive file_archive;
	std::vector<char> file_buffer;

	if (in_file_stream.open(archive_file_path, std::ios::binary), !in_file_stream.is_open()) {
		fprintf(stderr, "[%s] failed to open archive '%s'\n", __func__, archive_file_path.c_str());
		return EXIT_FAILURE;
	}

	fprintf(stdout, "[%s] finding file '%s'\n", __func__, src_file_path.c_str());

	file_archive.open(&in_file_stream);

	#ifdef USE_STD_OPTIONAL
	const std::optional<std::reference_wrapper<const util::hpi_archive::file_data>> entry = file_archive.find_file(src_file_path);
	#else
	const util::hpi_archive::file_data* entry = file_archive.find_file(src_file_path);
	#endif

	if (!entry) {
		fprintf(stderr, "[%s] could not find file '%s' in archive\n", __func__, src_file_path.c_str());
		return EXIT_FAILURE;
	}

	fprintf(stdout, "[%s] extracting file '%s' to '%s'\n", __func__, src_file_path.c_str(), tgt_file_path.c_str());

	#ifdef USE_STD_OPTIONAL
	file_buffer.resize(entry->get().size, 0);
	#else
	file_buffer.resize(entry->size, 0);
	#endif
	file_archive.extract(*entry, file_buffer);

	out_file_stream.open(tgt_file_path, std::ios::binary);
	out_file_stream.write(file_buffer.data(), file_buffer.size());

	return EXIT_SUCCESS;
}

static void extract_archive_rec(util::hpi_archive& file_archive, const util::hpi_archive::arch_entry& entry, const fs::path& tgt_file_path) {
	if (const util::hpi_archive::path_data* d = boost::get<util::hpi_archive::path_data>(&entry.data); d != nullptr) {
		fs::create_directory(tgt_file_path / entry.name);

		for (const util::hpi_archive::arch_entry& e: d->entries) {
			extract_archive_rec(file_archive, e, tgt_file_path / entry.name);
		}

		return;
	}

	if (const util::hpi_archive::file_data* f = boost::get<util::hpi_archive::file_data>(&entry.data); f != nullptr) {
		std::string file_name((tgt_file_path / entry.name).string());
		std::ofstream file_stream(file_name, std::ios::binary);
		std::vector<char> file_buffer(f->size, 0);

		fprintf(stdout, "[%s] extracting file '%s' (%lu bytes)\n", __func__, file_name.c_str(), file_buffer.size());

		file_archive.extract(*f, file_buffer);
		file_stream.write(file_buffer.data(), file_buffer.size());
		return;
	}
}

static int handle_extract_arch_command(const std::string& archive_file_path, const std::string& tgt_file_path) {
	fprintf(stdout, "[%s] opening archive '%s'\n", __func__, archive_file_path.c_str());

	std::ifstream file_stream;
	util::hpi_archive file_archive;

	if (file_stream.open(archive_file_path, std::ios::binary), !file_stream.is_open()) {
		fprintf(stderr, "[%s] failed to open archive '%s'\n", __func__, archive_file_path.c_str());
		return EXIT_FAILURE;
	}

	fprintf(stdout, "[%s] extracting files\n", __func__);
	file_archive.open(&file_stream);

	// assume target directory does not exist yet
	fs::create_directory(tgt_file_path);

	for (const util::hpi_archive::arch_entry& e: file_archive.get_root_entries()) {
		extract_archive_rec(file_archive, e, tgt_file_path);
	}

	return EXIT_SUCCESS;
}


int main(int argc, char** argv) {
	if (argc < 2 || strstr(argv[1], "--") != argv[1]) {
		fprintf(stderr, "[%s] usage: %s <--list-files|--extract-file|--extract-arch>\n", __func__, argv[0]);
		return EXIT_FAILURE;
	}

	try {
		if (strcmp(argv[1] + 2, "lf") == 0 || strcmp(argv[1] + 2, "list-files") == 0) {
			if (argc < 3) {
				fprintf(stderr, "[%s] usage: %s <HPI archive>\n", __func__, argv[1]);
				return EXIT_FAILURE;
			}

			return (handle_list_files_command(argv[2]));
		}

		if (strcmp(argv[1] + 2, "ef") == 0 || strcmp(argv[1] + 2, "extract-file") == 0) {
			if (argc < 5) {
				fprintf(stderr, "[%s] usage: %s <HPI archive> <source file> <target file>\n", __func__, argv[1]);
				return EXIT_FAILURE;
			}

			return (handle_extract_file_command(argv[2], argv[3], argv[4]));
		}

		if (strcmp(argv[1] + 2, "ea") == 0 || strcmp(argv[1] + 2, "extract-arch") == 0) {
			if (argc < 4) {
				fprintf(stderr, "[%s] usage: %s <HPI archive> <target directory>\n", __func__, argv[1]);
				return EXIT_FAILURE;
			}

			return (handle_extract_arch_command(argv[2], argv[3]));
		}

		fprintf(stderr, "[%s] unhandled command \"%s\"\n", __func__, argv[1]);
	} catch (const util::hpi_exception& e) {
		fprintf(stderr, "[%s] exception \"%s\"\n", __func__, e.what());
	}

	return EXIT_FAILURE;
}

