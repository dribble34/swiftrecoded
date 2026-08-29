#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <windows.h>

// Manual-map-safe replacement for `thread_local`.
//
// Compiler `thread_local` storage needs the Windows loader to process the
// PE's TLS directory (LdrpHandleTlsData). Manual mappers skip that, leaving
// _tls_index at 0, so post-map `thread_local` access lands in whatever
// module owns TLS slot 0 (usually the host EXE).
//
// This avoids the TLS directory entirely: a fixed-capacity static array
// backs the values (no heap alloc, so it's safe from a vectored exception
// handler where the game's operator new could deadlock), and FLS (FlsAlloc)
// only records which slot belongs to the calling thread. FLS is a plain
// runtime call, independent of the loader and of DLL_THREAD_ATTACH. The FLS
// slot holds a 1-based index, not a pointer, so there's nothing to free and
// no dangling callback if the module is unmapped without a clean detach.
namespace tls {

	namespace detail {

		[[nodiscard]] inline unsigned long allocate_fls_slot( )
		{
			return FlsAlloc( nullptr );
		}

	} // namespace detail

	template <typename T, std::size_t capacity = 64>
	class dynamic_tls
	{
	public:
		[[nodiscard]] T& get( )
		{
			return this->m_storage[ this->slot_index( ) ];
		}

		dynamic_tls( ) = default;
		dynamic_tls( const dynamic_tls& ) = delete;
		dynamic_tls& operator=( const dynamic_tls& ) = delete;

	private:
		[[nodiscard]] unsigned long fls_index( )
		{
			static const unsigned long index = detail::allocate_fls_slot( );
			return index;
		}

		[[nodiscard]] std::size_t slot_index( )
		{
			const auto raw = FlsGetValue( this->fls_index( ) );
			const auto packed = reinterpret_cast<std::uintptr_t>( raw );
			if ( packed )
			{
				return static_cast<std::size_t>( packed - 1 );
			}

			auto index = this->m_next.fetch_add( 1, std::memory_order_relaxed );
			if ( index >= capacity )
			{
				// more than `capacity` threads have touched this slot; alias
				// the last slot rather than corrupt memory. those threads then
				// share state - a correctness tradeoff, not a safety one.
				index = capacity - 1;
			}

			FlsSetValue( this->fls_index( ), reinterpret_cast<void*>( index + 1 ) );
			return index;
		}

		std::array<T, capacity> m_storage{};
		std::atomic<std::size_t> m_next{};
	};

} // namespace tls
