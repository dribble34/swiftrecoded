#include <core/resources/fonts/sfpro-bold.hpp>
#include <core/resources/fonts/font_awesome.hpp>
#include "../rendering.hpp"

namespace rendering {

	void fonts::initialize( )
	{
		this->load_family( this->sfpro_bold, std::as_bytes( std::span{ embedded_font } ), { 13.0f, 16.0f, 48.0f, 20.0f, 24.0f, 36.0f } );
		this->fa_solid = xdraw::load_font( std::as_bytes( std::span{ resources::fonts::font_awesome::solid } ), 18.0f );
		this->fa_solid_small = xdraw::load_font( std::as_bytes( std::span{ resources::fonts::font_awesome::solid } ), 14.0f );
	}

	void fonts::load_family( family_t& family, std::span<const std::byte> data, const std::array<float, static_cast< std::size_t >( size::count )>& sizes )
	{
		constexpr std::array<std::uint8_t, static_cast< std::size_t >( size::count )> order{ 1u, 0u, 2u, 3u, 4u, 5u };

		for ( const auto idx : order )
		{
			family.sizes[ idx ] = xdraw::load_font( data, sizes[ idx ] );
		}
	}

} 
