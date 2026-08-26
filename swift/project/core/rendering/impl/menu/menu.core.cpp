#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/settings.hpp>
#include <utilities/steam/steam.hpp>
#include <core/systems/systems.hpp>
#include <external/config.hpp>

#include "../../rendering.hpp"

namespace rendering {

	void menu::draw( )
	{
		if ( this->m_last_open != this->m_open )
		{
			if ( this->m_open )
			{
				this->m_saved_relative_mouse = memory::read<std::uint8_t>( addresses::globals::input_system + 84 );
				memory::call_vfunc<void>( addresses::globals::input_system, 76, false );
				this->apply_saved_cursor( );
			}
			else
			{
				POINT pt{};
				if ( GetCursorPos( &pt ) )
				{
					this->m_saved_cursor_x = pt.x;
					this->m_saved_cursor_y = pt.y;
					this->m_has_saved_cursor = true;
				}

				memory::call_vfunc<void>( addresses::globals::input_system, 76, this->m_saved_relative_mouse != 0 );
			}

			this->m_last_open = this->m_open;
		}

		if ( !g_context.ui_assets_ready( ) )
		{
			return;
		}

		xui::begin( );
		this->apply_theme( );
		this->sync_theme_style( );
		{
			// dpi scale
			if ( this->m_dpi_scale <= 0.0f )
			{
				const auto dc = GetDC( nullptr );
				const auto dpi = GetDeviceCaps( dc, LOGPIXELSX );
				ReleaseDC( nullptr, dc );

				this->m_dpi_scale = std::clamp( static_cast< float >( dpi ) / 96.0f, 0.5f, 3.0f );
				this->m_w = 805.0f * this->m_dpi_scale;
				this->m_h = 644.0f * this->m_dpi_scale;
			}
			this->m_open_anim = this->m_open ? 1.0f : 0.0f;

			if ( this->m_open_anim < 0.01f && !this->m_open )
			{
				xui::end( );
				return;
			}

			if ( !xui::begin_window( "##menu", this->m_x, this->m_y, this->m_w, this->m_h, false, 200.0f, 200.0f, 1.0f ) )
			{
				return;
			}

			const auto backdrop_col = this->m_dark_mode ? xdraw::color{ 12, 13, 18, 190 } : xdraw::color{ 245, 246, 252, 210 };
			xdraw::backdrop( this->m_x, this->m_y, this->m_w, this->m_h, xdraw::corner_radius{ xui::ctx( ).style.window_rounding }, backdrop_col );

			xdraw::push_font( rendering::g_fonts.sfpro_bold[ rendering::fonts::size::normal ] );
			auto& dl = xui::draw::current( );
			const auto wx = this->m_x;
			const auto wy = this->m_y;
			const auto ww = this->m_w;
			const auto wh = this->m_h;

			const auto top_bar_h = 48.0f;
			const auto bottom_bar_h = 56.0f;

			this->draw_top_bar( wx, wy, ww, top_bar_h );

			if ( this->m_tab != this->m_last_tab )
			{
				this->m_tab_dir = ( this->m_tab > this->m_last_tab ) ? 1.0f : -1.0f;
				this->m_tab_slide = 0.0f;
				this->m_last_tab = this->m_tab;
			}

			this->m_tab_slide = std::min( 1.0f, this->m_tab_slide + xdraw::delta_time( ) * 5.5f );
			const auto slide_t = xui::ease::out_cubic( std::clamp( this->m_tab_slide, 0.0f, 1.0f ) );
			const auto slide_off = this->m_tab_dir * ( 1.0f - slide_t ) * 60.0f;

			const auto content_x = wx + tokens::gap;
			const auto content_y = wy + top_bar_h + tokens::gap;
			const auto content_w = ww - tokens::gap * 2.0f;
			const auto content_h = wh - top_bar_h - bottom_bar_h - tokens::gap * 3.0f;

			const auto& subtab_def = k_subtab_defs[ this->m_tab ];
			const auto has_subtabs = subtab_def.count > 1;

			if ( has_subtabs )
			{
				this->draw_subtab_bar( content_x, content_y, content_w );
			}

			const auto body_y = content_y + ( has_subtabs ? tokens::subtab_bar_h + tokens::gap : 0.0f );
			const auto body_h = content_h - ( has_subtabs ? tokens::subtab_bar_h : 0.0f ) - tokens::gap;
			const auto col_w = ( content_w - tokens::gap ) * 0.5f;

			this->m_body_x = content_x + slide_off;
			this->m_body_y = body_y;
			this->m_body_w = content_w;
			this->m_body_h = body_h;

			dl.push_clip( content_x, body_y, content_w, body_h + tokens::gap );
			xui::layout::set_cursor( this->m_body_x - wx, body_y - wy );

			switch ( this->m_tab )
			{
			case 0: this->draw_ragebot( col_w ); break;
			case 1: this->draw_legitbot( col_w ); break;
			case 2: this->draw_player( col_w ); break;
			case 3: this->draw_world( col_w ); break;
			case 4: this->draw_skins( col_w ); break;
			case 5: this->draw_misc( col_w ); break;
			case 6: this->draw_config( col_w ); break;
			}

			dl.pop_clip( );

			this->draw_bottom_bar( wx, wy + wh - bottom_bar_h, ww, bottom_bar_h );

			xui::end_window( );

			dl.rect( wx + 0.75f, wy + 0.75f, ww - 1.5f, wh - 1.5f, tokens::col_border.alpha( 68 ), xdraw::corner_radius{ xui::ctx( ).style.window_rounding - 1.0f }, 1.0f );
			dl.rect( wx + 2.0f, wy + 2.0f, ww - 4.0f, wh - 4.0f, tokens::col_border.alpha( 20 ), xdraw::corner_radius{ xui::ctx( ).style.window_rounding - 2.0f }, 1.0f );
		}

		if ( this->m_profile_settings_open )
		{
			this->draw_profile_settings( );
		}

		xui::end( );
		xdraw::pop_font( );
	}

	void menu::shutdown( ) const
	{
		if ( this->m_open )
		{
			memory::call_vfunc<void>( addresses::globals::input_system, 76, this->m_saved_relative_mouse != 0 );
		}
	}

	void menu::apply_saved_cursor( )
	{
		if ( !this->m_has_saved_cursor )
		{
			return;
		}

		const auto vx = GetSystemMetrics( SM_XVIRTUALSCREEN );
		const auto vy = GetSystemMetrics( SM_YVIRTUALSCREEN );
		const auto vw = GetSystemMetrics( SM_CXVIRTUALSCREEN );
		const auto vh = GetSystemMetrics( SM_CYVIRTUALSCREEN );

		const auto x = std::clamp( this->m_saved_cursor_x, vx, vx + std::max( 1, vw ) - 1 );
		const auto y = std::clamp( this->m_saved_cursor_y, vy, vy + std::max( 1, vh ) - 1 );

		SetCursorPos( x, y );
		SetCursor( LoadCursor( nullptr, IDC_ARROW ) );
	}

	void menu::apply_theme( ) const
	{
		if ( this->m_dark_mode )
		{
			tokens::col_accent = xdraw::color{ 240, 242, 255, 255 };
			tokens::col_text = xdraw::color{ 250, 250, 252, 242 };
			tokens::col_text_dim = xdraw::color{ 235, 235, 242, 150 };
			tokens::col_card = xdraw::color{ 255, 255, 255, 34 };
			tokens::col_elevated = xdraw::color{ 255, 255, 255, 50 };
			tokens::col_dark = xdraw::color{ 12, 13, 17, 220 };
			tokens::col_border = xdraw::color{ 255, 255, 255, 55 };
		}
		else
		{
			tokens::col_accent = xdraw::color{ 28, 30, 40, 255 };
			tokens::col_text = xdraw::color{ 24, 25, 32, 245 };
			tokens::col_text_dim = xdraw::color{ 40, 42, 52, 150 };
			tokens::col_card = xdraw::color{ 24, 26, 38, 22 };
			tokens::col_elevated = xdraw::color{ 24, 26, 38, 40 };
			tokens::col_dark = xdraw::color{ 255, 255, 255, 235 };
			tokens::col_border = xdraw::color{ 24, 26, 38, 55 };
		}

		xui::anim_speed_multiplier = 1.0f;
	}

	void menu::sync_theme_style( ) const
	{
		auto& style = xui::ctx( ).style;

		if ( this->m_dark_mode )
		{
			style.window_bg = xdraw::color{ 18, 19, 24, 96 };
			style.child_bg = xdraw::color{ 255, 255, 255, 26 };
			style.child_border = xdraw::color{ 255, 255, 255, 24 };
			style.combo_popup_bg = xdraw::color{ 30, 31, 38, 224 };
			style.combo_popup_border = xdraw::color{ 255, 255, 255, 52 };
			style.combo_popup_item_hovered = xdraw::color{ 255, 255, 255, 22 };
			style.combo_popup_item_selected = xdraw::color{ 255, 255, 255, 10 };
			style.popup_bg = xdraw::color{ 30, 31, 38, 224 };
			style.picker_popup_bg = xdraw::color{ 30, 31, 38, 224 };
		}
		else
		{
			style.window_bg = xdraw::color{ 240, 241, 248, 160 };
			style.child_bg = xdraw::color{ 20, 20, 40, 18 };
			style.child_border = xdraw::color{ 20, 20, 40, 22 };
			style.combo_popup_bg = xdraw::color{ 248, 249, 254, 244 };
			style.combo_popup_border = xdraw::color{ 20, 20, 40, 40 };
			style.combo_popup_item_hovered = xdraw::color{ 20, 20, 40, 18 };
			style.combo_popup_item_selected = xdraw::color{ 20, 20, 40, 10 };
			style.popup_bg = xdraw::color{ 248, 249, 254, 244 };
			style.picker_popup_bg = xdraw::color{ 248, 249, 254, 244 };
		}

		style.border_thickness = 1.0f;
		style.window_rounding = 22.0f;
		style.rounding = 14.0f;
		style.button_rounding = 12.0f;
		style.popup_rounding = 16.0f;
		style.combo_rounding = 10.0f;
		style.combo_popup_rounding = 10.0f;
		style.combo_h = 26.0f;
		style.combo_item_h = 30.0f;
		style.text_input_rounding = 12.0f;
		style.item_spacing_x = 10.0f;
		style.item_spacing_y = 10.0f;
		style.checkbox_bg = tokens::col_card;
		style.checkbox_mark = tokens::col_accent;
		style.checkbox_mark_icon = tokens::col_dark;
		style.slider_track = tokens::col_card;
		style.slider_fill = tokens::col_accent;
		style.button_bg = tokens::col_card;
		style.button_hovered = tokens::col_elevated;
		style.button_active = tokens::col_accent;
		style.keybind_bg = tokens::col_card;
		style.keybind_waiting = tokens::col_accent;
		style.combo_bg = tokens::col_card;
		style.combo_border = tokens::col_border;
		style.combo_arrow = tokens::col_text_dim;
		style.combo_hovered = tokens::col_card.alpha( 120 );
		style.picker_bg = tokens::col_card;
		style.text_input_bg = tokens::col_card;
		style.separator = tokens::col_border.alpha( 38 );
		style.text = tokens::col_text;
		style.text_dim = tokens::col_text_dim;
		style.accent = tokens::col_accent;
	}

	void menu::draw_top_bar( float x, float y, float w, float h )
	{
		auto& dl = xui::draw::current( );
		const auto& input = xui::ctx( ).input;

		dl.rect_filled( x, y, w, h, tokens::col_card.alpha( 18 ), xdraw::corner_radius::top( xui::ctx( ).style.window_rounding ) );
		dl.line( x, y + h, x + w, y + h, tokens::col_border.alpha( 28 ), 1.0f );

		const auto pad = tokens::gap;
		const auto btn_size = h - pad * 2.0f;

		xdraw::push_font( rendering::g_fonts.sfpro_bold[ rendering::fonts::size::title ] );
		const auto title = "swift";
		const auto [ title_tw, title_th ] = xdraw::measure_text( title );
		dl.text( x + pad, y + ( h - title_th ) * 0.5f, title, tokens::col_accent );
		xdraw::pop_font( );

		const auto theme_btn_x = x + w - pad - btn_size;

		const auto theme_rect = xui::rect{ theme_btn_x, y + pad, btn_size, btn_size };
		const auto theme_hovered = input.in_rect( theme_rect );
		if ( theme_hovered && input.mouse_clicked && !xui::ctx( ).overlay_blocking( ) )
		{
			this->m_dark_mode = !this->m_dark_mode;
		}

		const auto theme_anim = xui::anim::lerp( xui::fnv1a( "top_theme" ), theme_hovered ? 1.0f : 0.0f, 14.0f );
		dl.rect_filled( theme_rect.x, theme_rect.y, theme_rect.w, theme_rect.h,
			xui::lerp( tokens::col_card, tokens::col_elevated, theme_anim ),
			xdraw::corner_radius{ tokens::btn_rounding } );
		xdraw::push_font( rendering::g_fonts.fa_solid_small );
		const auto theme_icon = this->m_dark_mode ? "\xEF\x86\x86" : "\xEF\x86\x85";
		const auto [ ti_w, ti_h ] = xdraw::measure_text( theme_icon );
		dl.text( theme_rect.x + ( btn_size - ti_w ) * 0.5f, theme_rect.y + ( btn_size - ti_h ) * 0.5f, theme_icon, tokens::col_text );
		xdraw::pop_font( );
	}

	void menu::draw_subtab_bar( float content_x, float bar_y, float w )
	{
		auto& dl = xui::draw::current( );
		const auto& input = xui::ctx( ).input;

		const auto& def = k_subtab_defs[ this->m_tab ];
		const auto subtab_count = def.count;

		if ( subtab_count <= 1 )
		{
			return;
		}

		const auto inner_pad{ 4.0f };
		const auto subtab_h = tokens::subtab_bar_h - inner_pad * 2.0f;

		const auto btn_w = ( w - inner_pad * 2.0f ) / static_cast< float >( subtab_count );

		dl.rect_filled( content_x, bar_y, w, tokens::subtab_bar_h, tokens::col_card, xdraw::corner_radius{ tokens::card_rounding } );

		const auto by = bar_y + ( tokens::subtab_bar_h - subtab_h ) * 0.5f;

		const auto pill_target_x = content_x + inner_pad + btn_w * static_cast< float >( this->m_subtab );

		if ( this->m_subtab_pill_tab != this->m_tab || this->m_subtab_pill_x < 0.0f )
		{
			this->m_subtab_pill_x = pill_target_x;
			this->m_subtab_pill_tab = this->m_tab;
		}
		else
		{
			const auto dt = xdraw::delta_time( );
			this->m_subtab_pill_x += ( pill_target_x - this->m_subtab_pill_x ) * std::min( 18.0f * dt, 1.0f );
		}

		dl.rect_filled( this->m_subtab_pill_x, by, btn_w, subtab_h, tokens::col_elevated, xdraw::corner_radius{ tokens::btn_rounding } );
		dl.rect( this->m_subtab_pill_x, by, btn_w, subtab_h, tokens::col_border.alpha( 62 ), xdraw::corner_radius{ tokens::btn_rounding }, 1.0f );

		for ( auto i = 0; i < subtab_count; ++i )
		{
			const auto bx = content_x + inner_pad + btn_w * i;
			const auto btn = xui::rect{ bx, by, btn_w, subtab_h };

			const auto hovered = input.in_rect( btn );
			const auto is_active = ( this->m_subtab == i );

			if ( hovered && input.mouse_clicked && !xui::ctx( ).overlay_blocking( ) )
			{
				this->m_subtab = i;
			}

			const auto active_anim = xui::anim::lerp( xui::fnv1a( "subtab_text" ) + i, is_active ? 1.0f : 0.0f, 12.0f );
			const auto hover_anim = xui::anim::lerp( xui::fnv1a( "subtab_hover" ) + i, hovered && !is_active ? 1.0f : 0.0f, 14.0f );

			const auto [tw, th] = xdraw::measure_text( def.names[ i ] );
			const auto tx = std::floor( btn.x + ( btn.w - tw ) * 0.5f );
			const auto ty = std::floor( btn.y + ( btn.h - th ) * 0.5f );

			auto text_col = xui::lerp( tokens::col_text_dim, tokens::col_accent, active_anim );
			text_col = xui::lerp( text_col, tokens::col_accent, hover_anim * 0.5f );
			dl.text( tx, ty, def.names[ i ], text_col );
		}
	}

	void menu::draw_bottom_bar( float x, float y, float w, float h )
	{
		auto& dl = xui::draw::current( );
		const auto& input = xui::ctx( ).input;

		dl.rect_filled( x, y, w, h, tokens::col_card.alpha( 18 ), xdraw::corner_radius::bottom( xui::ctx( ).style.window_rounding ) );
		dl.line( x, y, x + w, y, tokens::col_border.alpha( 28 ), 1.0f );

		const struct tab_item { const char* name; int id; const char* icon; };
		const tab_item tabs[] = {
			{ "ragebot", 0, "\xEF\x81\x9B" },
			{ "legitbot", 1, "\xEF\xA3\x8C" },
			{ "players", 2, "\xEF\x80\x87" },
			{ "world", 3, "\xEF\x82\xAC" },
			{ "skins", 4, "\xEF\x97\xBD" },
			{ "misc", 5, "\xEF\x80\x93" },
			{ "configs", 6, "\xEF\x81\xBB" }
		};
		constexpr auto tab_count = 7;

		const auto pad = tokens::gap;
		const auto btn_w = ( w - pad * ( tab_count + 1 ) ) / tab_count;
		const auto btn_h = h - pad * 2.0f;

		for ( auto i = 0; i < tab_count; ++i )
		{
			const auto bx = x + pad + ( btn_w + pad ) * i;
			const auto by = y + pad;
			const auto btn = xui::rect{ bx, by, btn_w, btn_h };

			const auto hovered = input.in_rect( btn );
			const auto is_active = ( this->m_tab == tabs[ i ].id );

			if ( hovered && input.mouse_clicked && !xui::ctx( ).overlay_blocking( ) )
			{
				this->m_tab = tabs[ i ].id;
				this->m_subtab = 0;
			}

			const auto anim = xui::anim::lerp( xui::fnv1a( "bottombar_tab" ) + i, ( hovered || is_active ) ? 1.0f : 0.0f, 14.0f );

			if ( is_active )
			{
				dl.rect_filled( bx, by, btn_w, btn_h,
					tokens::col_accent.alpha( static_cast< std::uint8_t >( 40.0f + 30.0f * anim ) ),
					xdraw::corner_radius{ tokens::btn_rounding } );
				dl.rect( bx, by, btn_w, btn_h,
					xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 70.0f * anim ) },
					xdraw::corner_radius{ tokens::btn_rounding }, 1.0f );
			}
			else if ( anim > 0.01f )
			{
				dl.rect_filled( bx, by, btn_w, btn_h,
					xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 24.0f * anim ) },
					xdraw::corner_radius{ tokens::btn_rounding } );
			}

			auto text_col = is_active ? tokens::col_accent : tokens::col_text_dim;
			text_col = xui::lerp( text_col, tokens::col_text, anim * ( is_active ? 0.0f : 0.4f ) );

			xdraw::push_font( rendering::g_fonts.fa_solid_small );
			const auto [ icon_w, icon_h ] = xdraw::measure_text( tabs[ i ].icon );
			dl.text( bx + ( btn_w - icon_w ) * 0.5f, by + btn_h * 0.28f - icon_h * 0.5f, tabs[ i ].icon, text_col );
			xdraw::pop_font( );

			xdraw::push_font( rendering::g_fonts.sfpro_bold[ rendering::fonts::size::petite ] );
			const auto [ text_w, text_h ] = xdraw::measure_text( tabs[ i ].name );
			dl.text( bx + ( btn_w - text_w ) * 0.5f, by + btn_h * 0.72f - text_h * 0.5f, tabs[ i ].name, text_col );
			xdraw::pop_font( );
		}
	}

	void menu::draw_profile_settings( )
	{
		const auto& input = xui::ctx( ).input;

		float settings_w = 300.0f;
		float settings_h = 400.0f;
		float settings_x = this->m_x + this->m_w - settings_w - 20.0f;
		float settings_y = this->m_y + this->m_h - settings_h - 20.0f;

		if ( input.mouse_clicked && !xui::rect{ settings_x, settings_y, settings_w, settings_h }.contains( input.mouse_x, input.mouse_y ) )
		{
			this->m_profile_settings_open = false;
			return;
		}

		if ( !xui::begin_window( "##profile_settings", settings_x, settings_y, settings_w, settings_h, false ) )
		{
			return;
		}

		auto& dl = xui::draw::current( );

		dl.rect_filled( settings_x, settings_y, settings_w, settings_h, tokens::col_card, xdraw::corner_radius{ xui::ctx( ).style.window_rounding } );
		dl.rect( settings_x, settings_y, settings_w, settings_h, tokens::col_accent.alpha( 64 ), xdraw::corner_radius{ xui::ctx( ).style.window_rounding }, 1.0f );

		const auto pad = xui::ctx( ).style.window_pad_x;
		xui::layout::set_cursor( pad, pad );

		if ( xui::begin_child( "##settings_content", settings_w - pad * 2.0f, settings_h - pad * 2.0f ) )
		{
			{
				xui::text( "Language", tokens::col_text );
				xui::layout::same_line( );

				const char* languages[] = { "English", "Russian" };
				if ( xui::combo( "##language", this->m_language_setting, languages, 2 ) )
				{

				}
			}

			xui::layout::spacing( );

			{
				xui::text( "Menu Scale", tokens::col_text );
				xui::layout::same_line( );

				if ( xui::slider_int( "##menu_scale", this->m_menu_scale, 50, 200, "%d%%" ) )
				{

				}
			}

			xui::layout::spacing( );

			xui::end_child( );
		}

		xui::end_window( );
	}

}
