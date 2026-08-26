#include <external/xorstr.hpp>

#include <utilities/logging/logging.hpp>

#include "../hooking.hpp"

namespace hooking::manager {

	bool create( const std::initializer_list<entry>& entries )
	{
		for ( const auto& entry : entries )
		{
			if ( !entry.hook || !entry.detour )
			{
				logging::console::print( xs( "invalid hook entry: {}" ), entry.name );
				return false;
			}

			if ( !entry.address )
			{
				logging::console::print( xs( "failed to hook: {}" ), entry.name );
				return false;
			}
		}

		const auto rollback = [ &entries ]( )
		{
			for ( const auto& entry : entries )
			{
				if ( entry.hook )
				{
					entry.hook->reset( );
				}
			}
		};

		for ( const auto& entry : entries )
		{
			if ( !entry.hook->create( reinterpret_cast< void* >( entry.address ), entry.detour ) )
			{
				logging::console::print( xs( "failed to hook: {}" ), entry.name );
				rollback( );
				return false;
			}

			if ( !entry.hook->enable( ) )
			{
				logging::console::print( xs( "failed to enable hook: {}" ), entry.name );
				rollback( );
				return false;
			}
		}

		return true;
	}

} 
