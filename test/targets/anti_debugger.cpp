#include <unistd.h>
#include <signal.h>
#include <numeric>
#include <cstdio>

void an_innocent_function() {
	std::puts("Putting pineapple on pizza...");
}

void an_innocent_function_end() {}

int checksum() {
	auto start = reinterpret_cast<volatile const char*>(&an_innocent_function);
	auto end = reinterpret_cast<volatile const char*>(&an_innocent_function_end);
	return std::accumulate(start, end, 0);
}

int main() {
	auto safe = checksum();

	auto ptr = reinterpret_cast<void*>(&an_innocent_function);
	write(STDOUT_FILENO, &ptr, sizeof(void*));
	fflush(stdout);

	raise(SIGTRAP);

	while (true) {
		if (checksum() == safe) {
			an_innocent_function();
		}
		else {
			puts("Putting mushrooms on pizza...");
		}

		fflush(stdout);
		raise(SIGTRAP);
	}
}
