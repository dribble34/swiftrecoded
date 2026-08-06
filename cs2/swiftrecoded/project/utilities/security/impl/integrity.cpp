#include <pch/pch.hpp>
#include "../security.hpp"

namespace security::integrity {

	namespace detail {
		std::vector<cached_hash> cached_hashes {};
	} // namespace detail

	bool initialize () {
		return true;
	}

	cached_hash* get (std::uintptr_t module_base) {
		for (auto& entry : detail::cached_hashes) {
			if (entry.module_base == module_base) {
				return &entry;
			}
		}

		return nullptr;
	}

} // namespace security::integrity
