#ifndef VDB_ELF_HPP
#define VDB_ELF_HPP

#include <filesystem>
#include <elf.h>

namespace vdb {
	class elf {
		public:
			elf(const std::filesystem::path& path);
			~elf();

			elf(const elf&) = delete;
			elf& operator=(const elf&) = delete;

			std::filesystem::path path() const { return path_; }
			const Elf64_Ehdr& get_header() const { return header_; }

		private:
			void parse_section_headers();

			int fd_;
			std::filesystem::path path_;
			std::size_t file_size_;
			std::byte* data_;
			Elf64_Ehdr header_;
			std::vector<Elf64_Shdr> section_headers_;
	};
}

#endif
