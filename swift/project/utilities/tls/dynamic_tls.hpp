#pragma once

// Manual-map-safe replacement for `thread_local`.
//
// Compiler-generated `thread_local` storage depends on the PE's TLS
// directory being processed by the Windows loader (LdrpHandleTlsData),
// which writes a per-thread slot into every thread's TEB before any code
// in the module runs. Generic manual mappers never do this, so any
// `thread_local` access after a manual map reads/writes garbage --
// `_tls_index` is left at 0, so the compiled access pattern lands in
// whatever module happens to occupy TLS slot 0 (usually the host EXE).
//
// This gets per-thread storage without touching the TLS directory at
// all: a fixed-capacity static array backs the values (no heap
// allocation, so it's safe to call from a vectored exception handler,
// where routing through the game's overridden operator new could
// deadlock on its allocator lock), and Fiber-Local Storage (FlsAlloc) is
// used only to remember which array slot belongs to the calling thread.
// FLS is a plain runtime API call, independent of the loader and of
// DLL_THREAD_ATTACH notifications (which this project disables via
// DisableThreadLibraryCalls). The FLS slot holds a 1-based index, never
// a pointer, so there is nothing to free -- no destructor callback, and
// therefore no risk of a dangling callback if the module is ever
// unmapped without a clean DLL_PROCESS_DETACH.
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
				// More distinct threads have touched this slot than
				// `capacity` allows for. Degrade to aliasing the last slot
				// rather than corrupting memory -- a correctness tradeoff
				// (those threads share state), not a safety one.
				index = capacity - 1;
			}

			FlsSetValue( this->fls_index( ), reinterpret_cast<void*>( index + 1 ) );
			return index;
		}

		std::array<T, capacity> m_storage{};
		std::atomic<std::size_t> m_next{};
	};

} // namespace tls
