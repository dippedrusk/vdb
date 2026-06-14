#include <libvdb/watchpoint.hpp>
#include <libvdb/process.hpp>
#include <libvdb/error.hpp>
#include <utility>

namespace {
	auto get_next_id() {
		static vdb::watchpoint::id_type id = 0;
		return ++id;
	}
}

vdb::watchpoint::watchpoint(process& proc, virt_addr address, stop_point_mode mode, std::size_t size)
	: process_{ &proc }, address_{ address }, is_enabled_{ false }, mode_{ mode }, size_{ size } {
	if ((address.addr() & (size - 1)) != 0) {
		error::send("Watchpoint must be aligned to size");
	}

	id_ = get_next_id();
	update_data();
}

void vdb::watchpoint::enable() {
	if (is_enabled_) return;

	hardware_register_index_ = process_->set_watchpoint(id_, address_, mode_, size_);
	is_enabled_ = true;
}

void vdb::watchpoint::disable() {
	if (!is_enabled_) return;

	process_->clear_hardware_stop_point(hardware_register_index_);
	is_enabled_ = false;
}

void vdb::watchpoint::update_data() {
	std::uint64_t new_data = 0;
	auto read = process_->read_memory(address_, size_);
	memcpy(&new_data, read.data(), size_);
	previous_data_ = std::exchange(data_, new_data);
}
