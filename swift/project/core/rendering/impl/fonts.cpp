#include <pch/pch.hpp>
#include <core/resources/fonts/inter.hpp>
#include <core/resources/fonts/pixel7.hpp>
#include <core/resources/fonts/font_awesome.hpp>
#include <core/resources/fonts/hurme_geometric.hpp>
#include "../rendering.hpp"

namespace rendering {

	void fonts::initialize( )
	{
		this->load_family( this->inter_medium, std::as_bytes( std::span{ resources::fonts::inter::regular } ), { 13.0f, 16.0f, 20.0f, 20.0f, 24.0f } );
		this->load_family( this->inter_bold, std::as_bytes( std::span{ resources::fonts::inter::bold } ), { 13.0f, 16.0f, 40.0f, 20.0f, 24.0f } );
		this->load_family( this->smallest_pixel7, std::as_bytes( std::span{ resources::fonts::pixel7::smallest } ), { 9.0f, 10.5f, 14.0f, 14.0f, 14.0f } );
		this->load_family( this->hurme_black, std::as_bytes( std::span{ resources::fonts::hurme_geometric::black } ), { 13.0f, 16.0f, 40.0f, 20.0f, 24.0f } );
		this->fa_solid = xdraw::load_font( std::as_bytes( std::span{ resources::fonts::font_awesome::solid } ), 18.0f );
		this->fa_solid_small = xdraw::load_font( std::as_bytes( std::span{ resources::fonts::font_awesome::solid } ), 14.0f );
	}

	void fonts::load_family( family_t& family, std::span<const std::byte> data, const std::array<float, static_cast< std::size_t >( size::count )>& sizes )
	{
		for ( auto i = 0ull; i < sizes.size( ); ++i )
		{
			family.sizes[ i ] = xdraw::load_font( data, sizes[ i ] );
		}
	}

} // namespace rendering
