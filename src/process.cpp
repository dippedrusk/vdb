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
		std::filesystem::path path) {
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
		if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
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

	std::unique_ptr<process> proc (new process(pid, /*terminate_on_end=*/true));
	proc->wait_on_signal();

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

	std::unique_ptr<process> proc (new process(pid, /*terminate_on_end=*/false));
	proc->wait_on_signal();

	return proc;
}

vdb::process::~process() {
	if (pid_ != 0) {
		int status;
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
	return reason;
}
