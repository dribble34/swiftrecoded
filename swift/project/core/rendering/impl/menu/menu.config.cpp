#include <utilities/math/math.hpp>
#include <core/settings.hpp>

#include <shellapi.h>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		std::string search_buf{};
		std::vector<std::wstring> config_list{};
		auto selected{ -1 };
		auto needs_refresh{ true };
		auto delete_pending{ false };
		auto delete_pending_tick{ 0.0f };

		static inline void wide_to_utf8( const std::wstring& wide, char* out, int out_size )
		{
			WideCharToMultiByte( CP_UTF8, 0, wide.c_str( ), -1, out, out_size, nullptr, nullptr );
		}

		static inline std::wstring utf8_to_wide( const std::string& utf8 )
		{
			wchar_t buf[ 128 ]{};
			MultiByteToWideChar( CP_UTF8, 0, utf8.c_str( ), -1, buf, 128 );
			return buf;
		}

		static inline bool config_matches_search( const std::wstring& wname )
		{
			if ( detail::search_buf.empty( ) )
			{
				return true;
			}

			char narrow[ 128 ]{};
			wide_to_utf8( wname, narrow, sizeof( narrow ) );

			std::string lower_name{ narrow };
			std::string lower_search{ detail::search_buf };

			for ( auto& c : lower_name )
			{
				c = static_cast< char >( std::tolower( c ) );
			}

			for ( auto& c : lower_search )
			{
				c = static_cast< char >( std::tolower( c ) );
			}

			return lower_name.find( lower_search ) != std::string::npos;
		}

		static inline std::string selected_name( )
		{
			if ( detail::selected < 0 || detail::selected >= static_cast< int >( detail::config_list.size( ) ) )
			{
				return {};
			}

			char narrow[ 128 ]{};
			wide_to_utf8( detail::config_list[ detail::selected ], narrow, sizeof( narrow ) );
			return narrow;
		}

	} 

	void menu::draw_config( float group_w )
	{
		( void )group_w;

		if ( detail::needs_refresh )
		{
			detail::config_list = config::registry::list( );
			detail::needs_refresh = false;

			if ( detail::selected >= static_cast< int >( detail::config_list.size( ) ) )
			{
				detail::selected = -1;
			}
		}

		auto& dl = xui::draw::current( );
		const auto& s = xui::ctx( ).style;
		const auto& input = xui::ctx( ).input;

		xui::layout::set_cursor( this->m_body_x - this->m_x, this->m_body_y - this->m_y );

		if ( !xui::begin_child( "##cfg_panel", this->m_body_w, this->m_body_h, false ) )
		{
			return;
		}

		xui::text_input( "##cfg_search", detail::search_buf, 64, "search configs..." );

		constexpr auto btn_h{ 28.0f };
		const auto [ avail_w, avail_h ] = xui::layout::avail( );
		const auto list_h = std::max( 80.0f, avail_h - btn_h - s.item_spacing_y );

		if ( xui::begin_child( "##cfg_list", avail_w, list_h, true ) )
		{
			const auto row_w = xui::layout::avail( ).first;
			constexpr auto row_h{ 28.0f };
			auto visible_rows{ 0 };

			for ( auto i = 0; i < static_cast< int >( detail::config_list.size( ) ); ++i )
			{
				const auto& wname = detail::config_list[ i ];

				if ( !detail::config_matches_search( wname ) )
				{
					continue;
				}

				char narrow[ 128 ]{};
				detail::wide_to_utf8( wname, narrow, sizeof( narrow ) );

				const auto row = xui::layout::item( row_w, row_h );
				const auto is_selected = ( detail::selected == i );
				const auto is_hovered = input.in_rect( row );

				if ( is_hovered && input.mouse_clicked && !xui::ctx( ).overlay_blocking( ) )
				{
					detail::selected = i;
					config::registry::load( wname );
					settings::finalize_binds( );
				}

				const auto hover_anim = xui::anim::lerp( xui::fnv1a( "cfgrow" ) + i, is_hovered ? 1.0f : 0.0f, 14.0f );
				const auto sel_anim = xui::anim::lerp( xui::fnv1a( "cfgsel" ) + i, is_selected ? 1.0f : 0.0f, 10.0f );

				if ( sel_anim > 0.01f )
				{
					dl.rect_filled( row.x, row.y, row.w, row.h, tokens::col_accent.alpha( static_cast< std::uint8_t >( 70.0f * sel_anim ) ), xdraw::corner_radius{ 6.0f } );
				}
				else if ( hover_anim > 0.01f )
				{
					dl.rect_filled( row.x, row.y, row.w, row.h, tokens::col_elevated.alpha( static_cast< std::uint8_t >( 255.0f * hover_anim * 0.5f ) ), xdraw::corner_radius{ 6.0f } );
				}

				const auto [ tw, th ] = xdraw::measure_text( narrow );
				const auto text_col = is_selected
					? xui::lerp( tokens::col_text, tokens::col_accent, sel_anim )
					: xui::lerp( tokens::col_text_dim, tokens::col_text, hover_anim );

				dl.text( row.x + 10.0f, row.y + ( row.h - th ) * 0.5f, narrow, text_col );

				visible_rows++;
			}

			if ( visible_rows == 0 )
			{
				const auto row = xui::layout::item( row_w, row_h );
				dl.text( row.x + 10.0f, row.y + 6.0f, detail::config_list.empty( ) ? "no configs found" : "no matches", tokens::col_text_dim );
			}

			xui::end_child( );
		}

		constexpr auto btn_count{ 4 };
		const auto btn_w = ( avail_w - s.item_spacing_x * ( btn_count - 1 ) ) / btn_count;
		const auto has_selection = detail::selected >= 0 && detail::selected < static_cast< int >( detail::config_list.size( ) );
		const auto save_name = has_selection ? detail::selected_name( ) : detail::search_buf;
		const auto can_save = !save_name.empty( );

		if ( xui::button( "create", btn_w, btn_h ) && !detail::search_buf.empty( ) )
		{
			config::registry::save( detail::utf8_to_wide( detail::search_buf ) );
			detail::needs_refresh = true;
		}

		xui::layout::same_line( );

		const auto dt = xdraw::delta_time( );
		detail::delete_pending_tick += dt;
		if ( detail::delete_pending && detail::delete_pending_tick > 3.0f )
		{
			detail::delete_pending = false;
		}

		if ( xui::button( detail::delete_pending ? "sure?" : "delete", btn_w, btn_h ) && has_selection )
		{
			if ( !detail::delete_pending )
			{
				detail::delete_pending = true;
				detail::delete_pending_tick = 0.0f;
			}
			else
			{
				std::error_code ec;
				std::filesystem::remove( config::registry::sanitize_name( detail::config_list[ detail::selected ] ), ec );
				detail::needs_refresh = true;
				detail::delete_pending = false;
			}
		}

		xui::layout::same_line( );

		if ( xui::button( "save", btn_w, btn_h ) && can_save )
		{
			config::registry::save( detail::utf8_to_wide( save_name ) );
			detail::needs_refresh = true;
		}

		xui::layout::same_line( );

		if ( xui::button( "open folder", btn_w, btn_h ) )
		{
			const auto dir = config::registry::directory( );

			std::error_code ec;
			std::filesystem::create_directories( dir, ec );

			ShellExecuteW( nullptr, L"open", dir.c_str( ), nullptr, nullptr, SW_SHOWNORMAL );
		}

		xui::end_child( );
	}

} 
