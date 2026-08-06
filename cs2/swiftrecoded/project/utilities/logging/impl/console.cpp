#include <pch/pch.hpp>
#include <utilities/diag.hpp>
#include "../logging.hpp"

namespace logging::console {

	bool initialize () {
		return true;
	}

	void print_raw (const char* text) {
		if ( !text ) {
			return;
		}

		const bool was_emitting = emitting;
		emitting = true;
		diag::write( diag::level::info, text );
		emitting = was_emitting;
	}

} // namespace logging::console
