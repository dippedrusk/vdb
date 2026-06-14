#include <catch2/catch_test_macros.hpp>
#include <libvdb/syscalls.hpp>
#include <libvdb/process.hpp>
#include <libvdb/error.hpp>
#include <libvdb/pipe.hpp>
#include <libvdb/bit.hpp>
#include <regex>
#include <elf.h>
#include <sys/types.h>
#include <signal.h>
#include <fstream>
#include <fcntl.h>

using namespace vdb;

namespace {
	bool process_exists(pid_t pid) {
		auto ret = kill(pid, 0);
		return ret != -1 and errno != ESRCH;
	}

	char get_process_status(pid_t pid) {
		std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
		std::string data;
		std::getline(stat, data);
		auto index_of_last_parenthesis = data.rfind(')');
		auto index_of_status_indicator = index_of_last_parenthesis + 2;
		return data[index_of_status_indicator];
	}

	std::int64_t get_section_load_bias(std::filesystem::path path, Elf64_Addr file_address) {
		auto command = std::string("readelf -WS ") + path.string();
		auto pipe = popen(command.c_str(), "r");

		std::regex text_regex(R"(PROGBITS\s+(\w+)\s+(\w+)\s+(\w+))");
		char* line = nullptr;
		std::size_t len = 0;
		while (getline(&line, &len, pipe) != -1) {
			std::cmatch groups;
			if (std::regex_search(line, groups, text_regex)) {
				auto address = std::stol(groups[1], nullptr, 16);
				auto offset = std::stol(groups[2], nullptr, 16);
				auto size = std::stol(groups[3], nullptr, 16);
				if (address <= file_address and file_address < (address + size)) {
					free(line);
					pclose(pipe);
					return address - offset;
				}
			}
			free(line);
			line = nullptr;
		}
		pclose(pipe);
		vdb::error::send("Could not find section load bias");
	}

	std::int64_t get_entry_point_offset(std::filesystem::path path) {
		std::ifstream elf_file(path);

		Elf64_Ehdr header;
		elf_file.read(reinterpret_cast<char*>(&header), sizeof(header));

		auto entry_file_address = header.e_entry;
		auto load_bias = get_section_load_bias(path, entry_file_address);
		return entry_file_address - load_bias;
	}

	virt_addr get_load_address(pid_t pid, std::int64_t offset) {
		std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
		std::regex map_regex (R"((\w+)-\w+ ..(.). (\w+))");

		std::string data;
		while (std::getline(maps, data)) {
			std::smatch groups;
			std::regex_search(data, groups, map_regex);

			if (groups[2] == 'x') {
				auto low_range = std::stol(groups[1], nullptr, 16);
				auto file_offset = std::stol(groups[3], nullptr, 16);
				return virt_addr(offset - file_offset + low_range);
			}
		}
		vdb::error::send("Could not find load address");
	}
}

TEST_CASE("process::launch success", "[process]") {
	auto proc = process::launch("yes");
	REQUIRE(process_exists(proc->pid()));
}

TEST_CASE("process::launch no such program", "[process]") {
	REQUIRE_THROWS(process::launch("you_do_not_have_to_be_good"));
}

TEST_CASE("process::attach success", "[process]") {
	auto target = process::launch("targets/run_endlessly", false);
	auto proc = process::attach(target->pid());
	REQUIRE(get_process_status(target->pid()) == 't');
}

TEST_CASE("process::attach invalid PID", "[process]") {
	REQUIRE_THROWS_AS(process::attach(0), error);
}

TEST_CASE("process::resume success", "[process]") {
	{
		auto proc = process::launch("targets/run_endlessly");
		proc->resume();
		auto status = get_process_status(proc->pid());
		auto success = status == 'R' or status == 'S';
		REQUIRE(success);
	}
	{
		auto target = process::launch("targets/run_endlessly", false);
		auto proc = process::attach(target->pid());
		proc->resume();
		auto status = get_process_status(proc->pid());
		auto success = status == 'R' or status == 'S';
		REQUIRE(success);
	}
}

TEST_CASE("process::resume already terminated", "[process]") {
	auto proc = process::launch("targets/end_immediately");
	proc->resume();
	proc->wait_on_signal();
	REQUIRE_THROWS_AS(proc->resume(), error);
}

TEST_CASE("Write register works", "[register]") {
	bool close_on_exec = false;
	vdb::pipe channel(close_on_exec);

	auto proc = process::launch("targets/reg_write", true, channel.get_write());
	channel.close_write();

	proc->resume();
	proc->wait_on_signal();

	auto& regs = proc->get_registers();

	/* rsi */
	regs.write_by_id(register_id::rsi, 0xcafebabe);

	proc->resume();
	proc->wait_on_signal();

	auto output = channel.read();
	REQUIRE(to_string_view(output) == "0xcafebabe");

	/* mm0 */
	regs.write_by_id(register_id::mm0, 0xba5eba11);

	proc->resume();
	proc->wait_on_signal();

	output = channel.read();
	REQUIRE(to_string_view(output) == "0xba5eba11");

	/* xmm0 */
	regs.write_by_id(register_id::xmm0, 131.719);

	proc->resume();
	proc->wait_on_signal();

	output = channel.read();
	REQUIRE(to_string_view(output) == "131.719");

	/* st0 */
	regs.write_by_id(register_id::st0, 131.719l);
	regs.write_by_id(register_id::fsw, std::uint16_t{0b0011100000000000});
	regs.write_by_id(register_id::ftw, std::uint16_t{0b0011111111111111});

	proc->resume();
	proc->wait_on_signal();

	output = channel.read();
	REQUIRE(to_string_view(output) == "131.719");
}

TEST_CASE("Read register works", "[register]") {
	auto proc = process::launch("targets/reg_read");
	auto& regs = proc->get_registers();

	proc->resume();
	proc->wait_on_signal();

	REQUIRE(regs.read_by_id_as<std::uint64_t>(register_id::r13) == 0xcafebabe);
	proc->resume();
	proc->wait_on_signal();

	REQUIRE(regs.read_by_id_as<std::uint8_t>(register_id::r13b) == 19);
	proc->resume();
	proc->wait_on_signal();

	REQUIRE(regs.read_by_id_as<byte64>(register_id::mm0) == to_byte64(0xba5eba11ull));
	proc->resume();
	proc->wait_on_signal();

	REQUIRE(regs.read_by_id_as<byte128>(register_id::xmm0) == to_byte128(1317.125));
	proc->resume();
	proc->wait_on_signal();

	REQUIRE(regs.read_by_id_as<long double>(register_id::st0) == 1317.125L);
}

TEST_CASE("Create breakpoint site", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	auto& site = proc->create_breakpoint_site(virt_addr{ 13 });
	REQUIRE(site.address().addr() == 13);
}

TEST_CASE("Breakpoint site ids increase", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");

	auto& s1 = proc->create_breakpoint_site(virt_addr{ 13 });
	REQUIRE(s1.address().addr() == 13);

	auto& s2 = proc->create_breakpoint_site(virt_addr{ 17 });
	REQUIRE(s2.id() == s1.id() + 1);

	auto& s3 = proc->create_breakpoint_site(virt_addr{ 19 });
	REQUIRE(s3.id() == s1.id() + 2);
}

TEST_CASE("Find breakpoint site", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	const auto& cproc = proc;

	proc->create_breakpoint_site(virt_addr{ 13 });
	proc->create_breakpoint_site(virt_addr{ 17 });
	proc->create_breakpoint_site(virt_addr{ 19 });

	auto& s1 = proc->breakpoint_sites().get_by_address(virt_addr{ 13 });
	REQUIRE(proc->breakpoint_sites().contains_address(virt_addr{ 13 }));
	REQUIRE(s1.address().addr() == 13);

	auto& cs1 = cproc->breakpoint_sites().get_by_address(virt_addr{ 17 });
	REQUIRE(cproc->breakpoint_sites().contains_address(virt_addr{ 17 }));
	REQUIRE(cs1.address().addr() == 17);

	auto& s2 = proc->breakpoint_sites().get_by_id(s1.id() + 1);
	REQUIRE(proc->breakpoint_sites().contains_id(s1.id() + 1));
	REQUIRE(s2.address().addr() == 17);

	auto& cs2 = cproc->breakpoint_sites().get_by_id(cs1.id() + 1);
	REQUIRE(cproc->breakpoint_sites().contains_id(cs1.id() + 1));
	REQUIRE(cs2.address().addr() == 19);
}

TEST_CASE("Cannot find breakpoint site", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	const auto& cproc = proc;

	REQUIRE_THROWS_AS(proc->breakpoint_sites().get_by_address(virt_addr{ 69 }), error);
	REQUIRE_THROWS_AS(proc->breakpoint_sites().get_by_id(69), error);
	REQUIRE_THROWS_AS(cproc->breakpoint_sites().get_by_address(virt_addr{ 69 }), error);
	REQUIRE_THROWS_AS(cproc->breakpoint_sites().get_by_id(69), error);
}

TEST_CASE("Breakpoint site list size and emptiness", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	const auto& cproc = proc;

	REQUIRE(proc->breakpoint_sites().empty());
	REQUIRE(proc->breakpoint_sites().size() == 0);
	REQUIRE(cproc->breakpoint_sites().empty());
	REQUIRE(cproc->breakpoint_sites().size() == 0);

	proc->create_breakpoint_site(virt_addr{ 13 });

	REQUIRE(!proc->breakpoint_sites().empty());
	REQUIRE(proc->breakpoint_sites().size() == 1);
	REQUIRE(!cproc->breakpoint_sites().empty());
	REQUIRE(cproc->breakpoint_sites().size() == 1);

	proc->create_breakpoint_site(virt_addr{ 17 });

	REQUIRE(!proc->breakpoint_sites().empty());
	REQUIRE(proc->breakpoint_sites().size() == 2);
	REQUIRE(!cproc->breakpoint_sites().empty());
	REQUIRE(cproc->breakpoint_sites().size() == 2);
}

TEST_CASE("Iterate breakpoint sites", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	const auto& cproc = proc;

	proc->create_breakpoint_site(virt_addr{ 24 });
	proc->create_breakpoint_site(virt_addr{ 25 });
	proc->create_breakpoint_site(virt_addr{ 26 });
	proc->create_breakpoint_site(virt_addr{ 27 });

	proc->breakpoint_sites().for_each(
		[addr = 24](auto& site) mutable {
			REQUIRE(site.address().addr() == addr++);
		});

	cproc->breakpoint_sites().for_each(
		[addr = 24](auto& site) mutable {
			REQUIRE(site.address().addr() == addr++);
		});
}

TEST_CASE("Breakpoint on address", "[breakpoint]") {
	bool close_on_exec = false;
	vdb::pipe channel(close_on_exec);

	auto proc = process::launch("targets/hello_vdb", true, channel.get_write());
	channel.close_write();

	auto offset = get_entry_point_offset("targets/hello_vdb");
	//REQUIRE(offset == 24);
	auto load_address = get_load_address(proc->pid(), offset);
	//REQUIRE(load_address.addr() == 24);

	proc->create_breakpoint_site(load_address).enable();
	proc->resume();
	auto reason = proc->wait_on_signal();

	REQUIRE(reason.reason == process_state::stopped);
	REQUIRE(reason.info == SIGTRAP);
	REQUIRE(proc->get_pc() == load_address);

	proc->resume();
	reason = proc->wait_on_signal();

	REQUIRE(reason.reason == process_state::exited);
	REQUIRE(reason.info == 0);

	auto data = channel.read();
	REQUIRE(to_string_view(data) == "Hello, vdb!\n");
}

TEST_CASE("Remove breakpoint sites", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	auto& site = proc->create_breakpoint_site(virt_addr{ 13 });
	proc->create_breakpoint_site(virt_addr{ 17 });
	REQUIRE(proc->breakpoint_sites().size() == 2);

	proc->breakpoint_sites().remove_by_id(site.id());
	proc->breakpoint_sites().remove_by_address(virt_addr{ 17 });
	REQUIRE(proc->breakpoint_sites().empty());
}

TEST_CASE("Reading and writing memory", "[memory]") {
	bool close_on_exec = false;
	vdb::pipe channel(close_on_exec);
	auto proc = process::launch("targets/memory", true, channel.get_write());
	channel.close_write();

	proc->resume();
	proc->wait_on_signal();

	auto a_pointer = from_bytes<std::uint64_t>(channel.read().data());
	auto data_vec = proc->read_memory(virt_addr{ a_pointer }, 8);
	auto data = from_bytes<std::uint64_t>(data_vec.data());
	REQUIRE(data == 0xcafebabe);

	proc->resume();
	proc->wait_on_signal();

	auto b_pointer = from_bytes<std::uint64_t>(channel.read().data());
	proc->write_memory(virt_addr{ b_pointer }, { as_bytes("Hewwo!"), 12 });

	proc->resume();
	proc->wait_on_signal();

	auto read = channel.read();
	REQUIRE(to_string_view(read) == "Hewwo!");
}

TEST_CASE("Hardware breakpoint evades memory checksums", "[breakpoint]") {
	bool close_on_exec = false;
	vdb::pipe channel(close_on_exec);
	auto proc = process::launch("targets/anti_debugger", true, channel.get_write());
	channel.close_write();

	proc->resume();
	proc->wait_on_signal();

	auto func = virt_addr(from_bytes<std::uint64_t>(channel.read().data()));

	auto& soft = proc->create_breakpoint_site(func, false);
	soft.enable();

	proc->resume();
	proc->wait_on_signal();

	REQUIRE(to_string_view(channel.read()) == "Putting mushrooms on pizza...\n");

	proc->breakpoint_sites().remove_by_id(soft.id());

	auto& hard = proc->create_breakpoint_site(func, true);
	hard.enable();

	proc->resume();
	proc->wait_on_signal();

	REQUIRE(proc->get_pc() == func);

	proc->resume();
	proc->wait_on_signal();

	REQUIRE(to_string_view(channel.read()) == "Putting pineapple on pizza...\n");
}

TEST_CASE("Watchpoint detects read", "[watchpoint]") {
	bool close_on_exec = false;
	vdb::pipe channel(close_on_exec);
	auto proc = process::launch("targets/anti_debugger", true, channel.get_write());
	channel.close_write();

	proc->resume();
	proc->wait_on_signal();

	auto func = virt_addr(from_bytes<std::uint64_t>(channel.read().data()));

	auto& watch = proc->create_watchpoint(func, vdb::stop_point_mode::read_write, 1);
	watch.enable();

	proc->resume();
	proc->wait_on_signal();

	proc->step_instruction();
	auto& soft = proc->create_breakpoint_site(func, false);
	soft.enable();

	proc->resume();
	auto reason = proc->wait_on_signal();

	REQUIRE(reason.info == SIGTRAP);

	proc->resume();
	proc->wait_on_signal();

	REQUIRE(to_string_view(channel.read()) == "Putting pineapple on pizza...\n");
}

TEST_CASE("Syscall mapping", "[syscall]") {
	REQUIRE(vdb::syscall_id_to_name(0) == "read");
	REQUIRE(vdb::syscall_name_to_id("read") == 0);
	REQUIRE(vdb::syscall_id_to_name(62) == "kill");
	REQUIRE(vdb::syscall_name_to_id("kill") == 62);
	REQUIRE(vdb::syscall_id_to_name(461) == "lsm_list_modules");
	REQUIRE(vdb::syscall_name_to_id("lsm_list_modules") == 461);
}

TEST_CASE("Syscall catchpoints", "[catchpoint]") {
	auto dev_null = open("/dev/null", O_WRONLY);
	auto proc = process::launch("targets/anti_debugger", true, dev_null);

	auto write_syscall = vdb::syscall_name_to_id("write");
	auto policy = vdb::syscall_catch_policy::catch_some({ write_syscall });
	proc->set_syscall_catch_policy(policy);

	proc->resume();
	auto reason = proc->wait_on_signal();

	REQUIRE(reason.reason == vdb::process_state::stopped);
	REQUIRE(reason.info == SIGTRAP);
	REQUIRE(reason.trap_reason == vdb::trap_type::syscall);
	REQUIRE(reason.syscall_info->id == write_syscall);
	REQUIRE(reason.syscall_info->entry == true);

	proc->resume();
	reason = proc->wait_on_signal();

	REQUIRE(reason.reason == vdb::process_state::stopped);
	REQUIRE(reason.info == SIGTRAP);
	REQUIRE(reason.trap_reason == vdb::trap_type::syscall);
	REQUIRE(reason.syscall_info->id == write_syscall);
	REQUIRE(reason.syscall_info->entry == false);

	close(dev_null);
}
