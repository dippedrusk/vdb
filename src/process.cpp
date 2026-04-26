#include <libvdb/process.hpp>
#include <libvdb/error.hpp>
#include <libvdb/pipe.hpp>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
	void exit_with_perror(
			vdb::pipe& channel, std::string const& prefix) {
		auto message = prefix + ": " + std::strerror(errno);
		channel.write(
				reinterpret_cast<std::byte*>(message.data()), message.size());
		exit(-1);
	}
}

std::unique_ptr<vdb::process> vdb::process::launch(
		std::filesystem::path path,
		bool debug,
		std::optional<int> stdout_replacement) {
	// gotta do this before forking,
	// otherwise the child and parent have their own (different) pipes
	pipe channel(/*close_on_exec=*/true);

	pid_t pid = 0;
	if ((pid = fork()) < 0) {
		error::send_errno("fork failed");
	}

	if (pid == 0) {
		// the child just writes and doesn't read
		channel.close_read();
		if (stdout_replacement) {
			if (dup2(*stdout_replacement, STDOUT_FILENO) < 0) {
				exit_with_perror(channel, "stdout replacement failed");
			}
		}
		if (debug and ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
			exit_with_perror(channel, "Tracing failed");
		}
		if (execlp(path.c_str(), path.c_str(), nullptr) < 0) {
			exit_with_perror(channel, "Exec failed");
		}
	}

	// the parent just reads and doesn't write
	channel.close_write();
	auto data = channel.read();
	channel.close_read();

	if (data.size() > 0) {
		waitpid(pid, nullptr, 0);
		auto chars = reinterpret_cast<char*>(data.data());
		error::send(std::string(chars, chars + data.size()));
	}

	std::unique_ptr<process> proc (new process(pid, /*terminate_on_end=*/true, debug));
	if (debug) {
		proc->wait_on_signal();
	}

	return proc;
}

std::unique_ptr<vdb::process> vdb::process::attach(
		pid_t pid) {
	// TODO: == or <=???
	if (pid <= 0) {
		error::send("Invalid pid");
	}
	if (ptrace(PTRACE_ATTACH, pid, /*addr=*/nullptr, /*data=*/nullptr) < 0) {
		error::send_errno("Could not attach");
	}

	std::unique_ptr<process> proc (new process(pid, /*terminate_on_end=*/false, /*attached=*/true));
	proc->wait_on_signal();

	return proc;
}

vdb::process::~process() {
	if (pid_ != 0) {
		int status;
		if (is_attached_) {
			if (state_ == process_state::running) {
				kill(pid_, SIGSTOP);
				waitpid(pid_, &status, 0);
			}
			ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
			kill(pid_, SIGCONT);

			if (terminate_on_end_) {
				kill(pid_, SIGKILL);
				waitpid(pid_, &status, 0);
			}
		}
	}
}

void vdb::process::resume() {
	if (ptrace(PTRACE_CONT, pid_, nullptr, nullptr) < 0) {
		error::send_errno("Could not resume");
	}
	state_ = process_state::running;
}

vdb::stop_reason::stop_reason(int wait_status) {
	if (WIFEXITED(wait_status)) {
		reason = process_state::exited;
		info = WEXITSTATUS(wait_status);
	}
	else if (WIFSIGNALED(wait_status)) {
		reason = process_state::terminated;
		info = WTERMSIG(wait_status);
	}
	else if (WIFSTOPPED(wait_status)) {
		reason = process_state::stopped;
		info = WSTOPSIG(wait_status);
	}
}

vdb::stop_reason vdb::process::wait_on_signal() {
	int wait_status;
	int options = 0;
	if (waitpid(pid_, &wait_status, options) < 0) {
		error::send_errno("waitpid failed");
	}
	stop_reason reason(wait_status);
	state_ = reason.reason;

	if (is_attached_ and state_ == process_state::stopped) {
		read_all_registers();
	}

	return reason;
}

void vdb::process::read_all_registers() {
	if (ptrace(PTRACE_GETREGS, pid_, nullptr, &get_registers().data_.regs) < 0) {
		error::send_errno("Could not read GP registers");
	}
	if (ptrace(PTRACE_GETFPREGS, pid_, nullptr, &get_registers().data_.i387) < 0) {
		error::send_errno("Could not read FP registers");
	}
	for (int i = 0; i < 8; ++i) {
		// we do math with ints and then cast back to a register ID because enum
		auto id = static_cast<int>(register_id::dr0) + i;
		auto info = register_info_by_id(static_cast<register_id>(id));

		errno = 0;
		std::int64_t data = ptrace(PTRACE_PEEKUSER, pid_, info.offset, nullptr);
		if (errno != 0) error::send_errno("Could not read debug register");

		get_registers().data_.u_debugreg[i] = data;
	}
}

void vdb::process::write_user_area(std::size_t offset, std::uint64_t data) {
	if (ptrace(PTRACE_POKEUSER, pid_, offset, data) < 0) {
		error::send_errno("Could not write to user area");
	}
}

void vdb::process::write_fprs(const user_fpregs_struct& fprs) {
	if (ptrace(PTRACE_SETFPREGS, pid_, nullptr, &fprs) < 0) {
		error::send_errno("Could not write floating point registers");
	}
}

void vdb::process::write_gprs(const user_regs_struct& gprs) {
	if (ptrace(PTRACE_SETREGS, pid_, nullptr, &gprs) < 0) {
		error::send_errno("Could not write general purpose registers");
	}
}

vdb::breakpoint_site& vdb::process::create_breakpoint_site(virt_addr address) {
	if (breakpoint_sites_.contains_address(address)) {
		error::send("Breakpoint site already created at address " +
				std::to_string(address.addr()));
	}
	return breakpoint_sites_.push(
			std::unique_ptr<breakpoint_site>(new breakpoint_site(*this, address)));
}
