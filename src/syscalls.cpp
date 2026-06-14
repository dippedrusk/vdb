#include <libvdb/syscalls.hpp>
#include <libvdb/error.hpp>
#include <unordered_map>

namespace {
	const std::unordered_map<std::string_view, int> g_syscall_name_map = {
		#define DEFINE_SYSCALL(name,id) { #name, id },
		#include "include/syscalls.inc"
		#undef DEFINE_SYSCALL
	};
}

std::string_view vdb::syscall_id_to_name(int id) {
	switch(id) {
		#define DEFINE_SYSCALL(name,id) case id: return #name;
		#include "include/syscalls.inc"
		#undef DEFINE_SYSCALL
		default: vdb::error::send("No such syscall");
	}
}

int vdb::syscall_name_to_id(std::string_view name) {
	if (g_syscall_name_map.count(name) != 1) vdb::error::send("No such syscall");
	return g_syscall_name_map.at(name);
}
