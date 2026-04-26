#ifndef VDB_STOP_POINT_COLLECTION_HPP
#define VDB_STOP_POINT_COLLECTION_HPP

#include <vector>
#include <memory>
#include <algorithm>
#include <libvdb/types.hpp>
#include <libvdb/error.hpp>

namespace vdb {
	template <class StopPoint> class stop_point_collection {
		public:
			StopPoint& push(std::unique_ptr<StopPoint> bs);

			bool contains_id(typename StopPoint::id_type id) const;
			bool contains_address(virt_addr address) const;
			bool enabled_stop_point_at_address(virt_addr address) const;

			StopPoint& get_by_id(typename StopPoint::id_type id);
			const StopPoint& get_by_id(typename StopPoint::id_type id) const;
			StopPoint& get_by_address(virt_addr address);
			const StopPoint& get_by_address(virt_addr address) const;

			void remove_by_id(typename StopPoint::id_type id);
			void remove_by_address(virt_addr address);

			template <class F> void for_each(F f);
			template <class F> void for_each(F f) const;

			std::size_t size() const { return stop_points_.size(); }
			bool empty() const { return stop_points_.empty(); }

		private:
			using points_t = std::vector<std::unique_ptr<StopPoint>>;

			typename points_t::iterator find_by_id(typename StopPoint::id_type id);
			typename points_t::const_iterator find_by_id(typename StopPoint::id_type id) const;
			typename points_t::iterator find_by_address(virt_addr address);
			typename points_t::const_iterator find_by_address(virt_addr address) const;

			points_t stop_points_;
	};

	template <class StopPoint> StopPoint& stop_point_collection<StopPoint>::push(std::unique_ptr<StopPoint> bs) {
		stop_points_.push_back(std::move(bs));
		return *stop_points_.back();
	}

	template <class StopPoint> auto stop_point_collection<StopPoint>::find_by_id(typename StopPoint::id_type id)
		-> typename points_t::iterator {
		return std::find_if(begin(stop_points_), end(stop_points_),
				[=](auto& point) { return point->id() == id; });
	}

	template <class StopPoint> auto stop_point_collection<StopPoint>::find_by_id(typename StopPoint::id_type id) const
		-> typename points_t::const_iterator {
		return const_cast<stop_point_collection*>(this)->find_by_id(id);
	}

	template <class StopPoint> auto stop_point_collection<StopPoint>::find_by_address(virt_addr address)
		-> typename points_t::iterator {
		return std::find_if(begin(stop_points_), end(stop_points_),
				[=](auto& point) { return point->at_address(address); });
	}

	template <class StopPoint> auto stop_point_collection<StopPoint>::find_by_address(virt_addr address) const
		-> typename points_t::const_iterator {
		return const_cast<stop_point_collection*>(this)->find_by_address(address);
	}

	template <class StopPoint> bool stop_point_collection<StopPoint>::contains_id(typename StopPoint::id_type id) const {
		return find_by_id(id) != end(stop_points_);
	}

	template <class StopPoint> bool stop_point_collection<StopPoint>::contains_address(virt_addr address) const {
		return find_by_address(address) != end(stop_points_);
	}

	template <class StopPoint> bool stop_point_collection<StopPoint>::enabled_stop_point_at_address(virt_addr address) const {
		return contains_address(address) and get_by_address(address).is_enabled();
	}

	template <class StopPoint> StopPoint& stop_point_collection<StopPoint>::get_by_id(typename StopPoint::id_type id) {
		auto it = find_by_id(id);
		if (it == end(stop_points_)) {
			error::send("Invalid stop point ID");
		}
		return **it;
	}

	template <class StopPoint> const StopPoint& stop_point_collection<StopPoint>::get_by_id(
			typename StopPoint::id_type id) const {
		return const_cast<stop_point_collection*>(this)->get_by_id(id);
	}

	template <class StopPoint> StopPoint& stop_point_collection<StopPoint>::get_by_address(virt_addr address) {
		auto it = find_by_address(address);
		if (it == end(stop_points_)) {
			error::send("Stop point with given address not found");
		}
		return **it;
	}

	template <class StopPoint> const StopPoint& stop_point_collection<StopPoint>::get_by_address(virt_addr address) const {
		return const_cast<stop_point_collection*>(this)->get_by_address(address);
	}

	template <class StopPoint> void stop_point_collection<StopPoint>::remove_by_id(typename StopPoint::id_type id) {
		auto it = find_by_id(id);
		(**it).disable();
		stop_points_.erase(it);
	}

	template <class StopPoint> void stop_point_collection<StopPoint>::remove_by_address(virt_addr address) {
		auto it = find_by_address(address);
		(**it).disable();
		stop_points_.erase(it);
	}

	template <class StopPoint> template <class F> void stop_point_collection<StopPoint>::for_each(F f) {
		for (auto& point : stop_points_) {
			f(*point);
		}
	}

	template <class StopPoint> template <class F> void stop_point_collection<StopPoint>::for_each(F f) const {
		for (const auto& point : stop_points_) {
			f(*point);
		}
	}
}

#endif
