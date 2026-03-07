#include <catch2/catch_test_macros.hpp>
#include <libvdb/process.hpp>
#include <libvdb/error.hpp>
#include <sys/types.h>
#include <signal.h>
#include <fstream>

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
