#include <utilities/math/math.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include "../rendering.hpp"

namespace rendering {

	void widgets::draw( )
	{
		auto& dl = xdraw::get( );

		if ( settings::g_misc.m_watermark.enabled.value )
		{
			this->watermark( dl );
		}

		this->keybinds( dl );
		this->indicators( dl );
		this->crosshair_indicators( dl );
	}

	void widgets::watermark( xdraw::draw_list& draw_list )
	{
		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto& wm = settings::g_misc.m_watermark;
		const auto framerate = xdraw::framerate( );
		const auto local = systems::g_local.get( );

		constexpr auto h{ 36.0f };
		constexpr auto margin{ 12.0f };
		constexpr auto r{ 18.0f };
		constexpr auto inner_r{ 13.0f };
		constexpr auto inner_pad{ 3.0f };
		constexpr auto text_pad_x{ 11.0f };
		constexpr auto text_nudge{ 0.5f };
		constexpr auto section_spacing{ 4.0f };
		constexpr auto logo_icon_pad{ 10.0f };

		
		SYSTEMTIME st{};
		GetLocalTime( &st );
		char time_buf[ 8 ]{};
		std::snprintf( time_buf, sizeof( time_buf ), "%02d:%02d", st.wHour, st.wMinute );

		
		static auto smoothed_fps{ 0.0f };
		if ( smoothed_fps == 0.0f ) smoothed_fps = framerate;
		smoothed_fps += ( framerate - smoothed_fps ) * std::min( 2.0f * xdraw::delta_time( ), 1.0f );
		char fps_val[ 8 ]{};
		std::snprintf( fps_val, sizeof( fps_val ), "%.0f", smoothed_fps );

		
		auto ping{ 0 };
		if ( local.is_alive && local.controller && systems::g_entities.exists( local.controller ) )
			ping = memory::read<std::uint32_t>( local.controller + SCHEMA( "CCSPlayerController", "m_iPing"_hash ) );
		char ping_val[ 8 ]{};
		std::snprintf( ping_val, sizeof( ping_val ), "%d", ping );

		
		const bool has_velocity = wm.show_velocity.value && local.is_alive && local.pawn;
		static auto smoothed_velocity{ 0.0f };
		char vel_val[ 8 ]{};
		if ( has_velocity )
		{
			const auto velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
			const auto speed = velocity.length_2d( );
			smoothed_velocity += ( speed - smoothed_velocity ) * std::min( 8.0f * xdraw::delta_time( ), 1.0f );
			std::snprintf( vel_val, sizeof( vel_val ), "%.0f", smoothed_velocity );
		}
		else
		{
			smoothed_velocity = 0.0f;
		}


		const auto inner_h     = h - inner_pad * 2.0f;

		
		const auto [name_tw, name_th] = xdraw::measure_text( "swift.fly" );
		const auto [user_tw, user_th] = xdraw::measure_text( "developer" );
		const auto [ping_vw, ping_vh] = xdraw::measure_text( ping_val );
		const auto [ping_uw, ping_uh] = xdraw::measure_text( " ms" );
		const auto [fps_vw,  fps_vh]  = xdraw::measure_text( fps_val );
		const auto [fps_uw,  fps_uh]  = xdraw::measure_text( " fps" );
		const auto [time_tw, time_th] = xdraw::measure_text( time_buf );

		float vel_vw{}, vel_vh{}, vel_uw{}, vel_uh{};
		if ( has_velocity )
		{
			std::tie( vel_vw, vel_vh ) = xdraw::measure_text( vel_val );
			std::tie( vel_uw, vel_uh ) = xdraw::measure_text( " u/s" );
		}


		
		const auto logo_pill_w = logo_icon_pad + name_tw + text_pad_x;
		const auto user_pill_w = user_tw + text_pad_x * 2.0f;
		const auto ping_pill_w = ping_vw + ping_uw + text_pad_x * 2.0f;
		const auto fps_pill_w  = fps_vw  + fps_uw  + text_pad_x * 2.0f;
		const auto time_pill_w = time_tw + text_pad_x * 2.0f;
		const auto vel_pill_w  = vel_vw + vel_uw + text_pad_x * 2.0f;

		
		float target_w = inner_pad + logo_pill_w + section_spacing;
		if ( wm.show_user.value ) target_w += user_pill_w + section_spacing;
		if ( wm.show_ping.value ) target_w += ping_pill_w + section_spacing;
		if ( has_velocity )       target_w += vel_pill_w  + section_spacing;
		if ( wm.show_fps.value )  target_w += fps_pill_w  + section_spacing;
		if ( wm.show_time.value ) target_w += time_pill_w + section_spacing;
		target_w = target_w - section_spacing + inner_pad;

		static auto smoothed_w{ 0.0f };
		if ( smoothed_w == 0.0f ) smoothed_w = target_w;
		smoothed_w += ( target_w - smoothed_w ) * std::min( 8.0f * xdraw::delta_time( ), 1.0f );

		const auto w = smoothed_w;
		const auto x = static_cast<float>( screen_w ) - w - margin;
		const auto y = margin;

		static float liquid_t{ 0.0f };
		liquid_t += xdraw::delta_time( );

		const auto breathe = 0.5f + 0.5f * std::sin( liquid_t * 1.4f );

		const auto glass_tint  = xdraw::color{ 255, 255, 255, 128 };
		const auto glass_base  = xdraw::color{ 255, 255, 255, 66 };
		const auto sheen_top   = xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 74 + 18.0f * breathe ) };
		const auto sheen_mid   = xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 32 + 8.0f * breathe ) };
		const auto glass_edge  = xdraw::color{ 255, 255, 255, 132 };
		const auto glass_fine  = xdraw::color{ 255, 255, 255, 44 };
		const auto pill_base   = xdraw::color{ 255, 255, 255, 40 };
		const auto pill_sheen  = xdraw::color{ 255, 255, 255, 58 };
		const auto pill_edge   = xdraw::color{ 255, 255, 255, 90 };
		const auto col_value   = xdraw::color{ 0, 0, 0, 255 };
		const auto col_unit    = xdraw::color{ 0, 0, 0, 255 };
		const auto col_soft    = xdraw::color{ 0, 0, 0, 255 };

		xdraw::backdrop( x, y, w, h, xdraw::corner_radius{ r }, glass_tint );

		draw_list.rect_filled( x, y, w, h, glass_base, xdraw::corner_radius{ r } );

		draw_list.rect_filled_gradient( x, y, w, h * 0.5f,
			sheen_top, sheen_mid,
			xdraw::color{ 255, 255, 255, 0 }, xdraw::color{ 255, 255, 255, 0 },
			xdraw::corner_radius::top( r ) );

		draw_list.rect_filled_gradient( x, y + h * 0.72f, w, h * 0.28f,
			xdraw::color{ 255, 255, 255, 0 }, xdraw::color{ 255, 255, 255, 0 },
			xdraw::color{ 0, 0, 0, 16 }, xdraw::color{ 0, 0, 0, 16 },
			xdraw::corner_radius::bottom( r ) );

		draw_list.rect( x, y, w, h, glass_edge, xdraw::corner_radius{ r }, 1.0f );
		draw_list.rect( x + 2.5f, y + 2.5f, w - 5.0f, h - 5.0f, glass_fine, xdraw::corner_radius{ r - 2.5f }, 1.0f );

		auto cx = x + inner_pad;

		auto draw_split_pill = [ & ]( const char* value, float vw, float vh, const char* unit, float /*uw*/, float uh, float pill_w )
			{
				draw_list.rect_filled( cx, y + inner_pad, pill_w, inner_h, pill_base, xdraw::corner_radius{ inner_r } );
				draw_list.rect_filled_gradient( cx, y + inner_pad, pill_w, inner_h * 0.55f,
					pill_sheen, xdraw::color{ 255, 255, 255, 26 },
					xdraw::color{ 255, 255, 255, 0 }, xdraw::color{ 255, 255, 255, 0 },
					xdraw::corner_radius::top( inner_r ) );
				draw_list.rect( cx, y + inner_pad, pill_w, inner_h, pill_edge, xdraw::corner_radius{ inner_r }, 1.0f );
				draw_list.text( cx + text_pad_x, y + ( h - vh ) * 0.5f + text_nudge, value, col_value );
				draw_list.text( cx + text_pad_x + vw, y + ( h - uh ) * 0.5f + text_nudge, unit, col_unit );
				cx += pill_w + section_spacing;
			};

		auto draw_pill = [ & ]( const char* text, float /*tw*/, float th, float pill_w )
			{
				draw_list.rect_filled( cx, y + inner_pad, pill_w, inner_h, pill_base, xdraw::corner_radius{ inner_r } );
				draw_list.rect_filled_gradient( cx, y + inner_pad, pill_w, inner_h * 0.55f,
					pill_sheen, xdraw::color{ 255, 255, 255, 26 },
					xdraw::color{ 255, 255, 255, 0 }, xdraw::color{ 255, 255, 255, 0 },
					xdraw::corner_radius::top( inner_r ) );
				draw_list.rect( cx, y + inner_pad, pill_w, inner_h, pill_edge, xdraw::corner_radius{ inner_r }, 1.0f );
				draw_list.text( cx + text_pad_x, y + ( h - th ) * 0.5f + text_nudge, text, col_soft );
				cx += pill_w + section_spacing;
			};

		draw_list.rect_filled( cx, y + inner_pad, logo_pill_w, inner_h, pill_base, xdraw::corner_radius{ inner_r } );
		draw_list.rect_filled_gradient( cx, y + inner_pad, logo_pill_w, inner_h * 0.55f,
			pill_sheen, xdraw::color{ 255, 255, 255, 26 },
			xdraw::color{ 255, 255, 255, 0 }, xdraw::color{ 255, 255, 255, 0 },
			xdraw::corner_radius::top( inner_r ) );
		draw_list.rect( cx, y + inner_pad, logo_pill_w, inner_h, pill_edge, xdraw::corner_radius{ inner_r }, 1.0f );
		draw_list.text( cx + logo_icon_pad,
			y + ( h - name_th ) * 0.5f + text_nudge, "swift.fly", col_value );
		cx += logo_pill_w + section_spacing;

		if ( wm.show_user.value ) draw_pill( "developer", user_tw, user_th, user_pill_w );
		if ( wm.show_ping.value ) draw_split_pill( ping_val, ping_vw, ping_vh, " ms",   ping_uw, ping_uh, ping_pill_w );
		if ( has_velocity )       draw_split_pill( vel_val,  vel_vw,  vel_vh,  " u/s",  vel_uw,  vel_uh,  vel_pill_w );
		if ( wm.show_fps.value )  draw_split_pill( fps_val,  fps_vw,  fps_vh,  " fps",  fps_uw,  fps_uh,  fps_pill_w );
		if ( wm.show_time.value ) draw_pill( time_buf, time_tw, time_th, time_pill_w );
	}

	void widgets::keybinds( xdraw::draw_list& draw_list )
	{
		
		if ( !settings::g_misc.keybinds_enabled.value )
			return;

		struct row_anim_t
		{
			animation::fade alpha;
			animation::spring offset_y;
			bool active_this_frame{ false };
		};

		static std::map<std::string, row_anim_t> row_states;
		static animation::fade container_alpha;
		static animation::spring smoothed_base_y;

		const auto [screen_w, screen_h] = xdraw::viewport_size( );

		auto scale{ 1.0f };
		auto font_size{ rendering::fonts::size::normal };
		switch ( settings::g_misc.keybind_scale_value.value )
		{
		case settings::misc::keybind_scale::half:         scale = 0.5f;  font_size = rendering::fonts::size::petite; break;
		case settings::misc::keybind_scale::three_quarter: scale = 0.75f; font_size = rendering::fonts::size::petite; break;
		case settings::misc::keybind_scale::full:         scale = 1.0f;  font_size = rendering::fonts::size::normal; break;
		case settings::misc::keybind_scale::one_half:     scale = 1.5f;  font_size = rendering::fonts::size::big;    break;
		}

		const auto margin{ 10.0f * scale };
		const auto row_spacing{ 4.0f * scale };
		const auto row_h{ 20.0f * scale };
		const auto header_h{ 32.0f * scale };
		const auto r{ 10.0f * scale };
		const auto pad_x{ 10.0f * scale };
		const auto pad_y{ 8.0f * scale };
		const auto underline_h{ 1.0f };
		const auto min_w{ 200.0f * scale };

		
		static float drag_offset_x{ 0.0f };
		static float drag_offset_y{ 0.0f };
		static bool is_dragging{ false };
		static float widget_x{ margin };
		static float widget_y{ 200.0f }; 

		struct bind_entry
		{
			const char* name;
		};

		bind_entry entries[ 32 ]{};
		auto count{ 0 };

		const auto& ctx = features::combat::g_shared.ctx( );
		const auto has_weapon = ctx.valid && ctx.weapon_type >= cstypes::weapon_type::pistol && ctx.weapon_type <= cstypes::weapon_type::lmg;

		for ( const auto setting : xui::binds::all( ) )
		{
			if ( !setting || setting->bind.key == 0 || !setting->bind.active || count >= 32 )
			{
				continue;
			}

			auto is_rage_group{ false };
			for ( auto i = 0u; i < settings::combat::ragebot::k_group_count; ++i )
			{
				const auto& g = settings::g_combat.m_ragebot.groups[ i ];
				if ( setting == &g.min_damage_override || setting == &g.hitchance_override || setting == &g.body_aim || setting == &g.silent || setting == &g.no_spread )
				{
					is_rage_group = true;
					break;
				}
			}

			if ( is_rage_group )
			{
				if ( !settings::g_combat.m_ragebot.enabled || !has_weapon )
				{
					continue;
				}

				const auto active_group = &settings::g_combat.m_ragebot.get_group( ctx.weapon_type );
				auto is_active{ false };

				for ( auto i = 0u; i < settings::combat::ragebot::k_group_count; ++i )
				{
					const auto& g = settings::g_combat.m_ragebot.groups[ i ];
					if ( &g == active_group )
					{
						if ( setting == &g.min_damage_override || setting == &g.hitchance_override || setting == &g.body_aim )
						{
							is_active = true;
						}
						break;
					}
				}

				if ( !is_active )
				{
					continue;
				}

				auto& e = entries[ count++ ];
				e.name = setting->name.c_str( );
				continue;
			}

			auto is_legit_group{ false };
			for ( auto i = 0u; i < settings::combat::legitbot::k_group_count; ++i )
			{
				const auto& g = settings::g_combat.m_legitbot.groups[ i ];
				if ( setting == &g.aimbot || setting == &g.rcs || setting == &g.standalone_rcs || setting == &g.triggerbot || setting == &g.autowall || setting == &g.visualize_fov || setting == &g.trigger_head_only || setting == &g.give_me_your_seed )
				{
					is_legit_group = true;
					break;
				}
			}

			if ( is_legit_group )
			{
				if ( !has_weapon )
				{
					continue;
				}

				const auto* active_group = &settings::g_combat.m_legitbot.get_group( ctx.weapon_type );
				auto is_active{ false };

				for ( auto i = 0u; i < settings::combat::legitbot::k_group_count; ++i )
				{
					if ( &settings::g_combat.m_legitbot.groups[ i ] == active_group )
					{
						const auto& g = settings::g_combat.m_legitbot.groups[ i ];
						if ( setting == &g.aimbot || setting == &g.rcs || setting == &g.standalone_rcs || setting == &g.triggerbot || setting == &g.autowall || setting == &g.visualize_fov || setting == &g.trigger_head_only || setting == &g.give_me_your_seed )
						{
							is_active = true;
						}

						if ( is_active && setting == &active_group->give_me_your_seed && !active_group->triggerbot.value )
						{
							is_active = false;
						}
						break;
					}
				}

				if ( !is_active )
				{
					continue;
				}

				auto& e = entries[ count++ ];
				e.name = setting->name.c_str( );
				continue;
			}

			if ( setting == &settings::g_combat.m_antiaim.enabled || setting == &settings::g_combat.m_antiaim.manual_left || setting == &settings::g_combat.m_antiaim.manual_right || setting == &settings::g_combat.m_antiaim.hide_shots || setting == &settings::g_combat.m_antiaim.avoid_backstab || setting == &settings::g_combat.m_antiaim.direction_indicator )
			{
				if ( !settings::g_combat.m_antiaim.enabled.value )
				{
					continue;
				}
			}

			auto& e = entries[ count++ ];
			e.name = setting->name.c_str( );
		}

		if ( count > 0 )
			container_alpha.fade_in( 0.2f );
		else
			container_alpha.fade_in( 0.2f ); 

		container_alpha.update( );
		if ( !container_alpha.visible( ) )
			return;

		const auto master_alpha = container_alpha.alpha( );
		const auto content_h = static_cast< float >( count ) * row_h + ( count > 1 ? ( count - 1 ) * row_spacing : 0.0f );
		
		
		float max_w = min_w;
		xdraw::push_font( rendering::g_fonts.sfpro_bold[ font_size ] );
		const auto [header_tw, header_th] = xdraw::measure_text( "keybinds" );
		
		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			const auto [nw, nh] = xdraw::measure_text( e.name );
			const auto row_w = pad_x + nw + pad_x + pad_x;
			if ( row_w > max_w ) max_w = row_w;
		}
		xdraw::pop_font( );
		
		const auto total_h = header_h + pad_y + content_h + pad_y;
		
		
		const auto& input = xui::ctx( ).input;
		const auto header_rect = xui::rect{ widget_x, widget_y, max_w, header_h };
		
		if ( input.in_rect( header_rect ) && input.mouse_clicked && !is_dragging )
		{
			is_dragging = true;
			drag_offset_x = input.mouse_x - widget_x;
			drag_offset_y = input.mouse_y - widget_y;
		}
		
		if ( is_dragging )
		{
			if ( input.mouse_down )
			{
				widget_x = input.mouse_x - drag_offset_x;
				widget_y = input.mouse_y - drag_offset_y;
				
				
				widget_x = std::max( 0.0f, std::min( widget_x, static_cast<float>( screen_w ) - max_w ) );
				widget_y = std::max( 0.0f, std::min( widget_y, static_cast<float>( screen_h ) - total_h ) );
			}
			else
			{
				is_dragging = false;
			}
		}

		const auto base_ry = widget_y;
		const auto x = widget_x;

		const auto w = max_w;

		
		const auto dark = rendering::g_menu.is_dark( );

		const auto backdrop_col = dark ? tokens::col_dark : xdraw::color{ 245, 246, 252, 210 };
		const auto fill_col = dark ? tokens::col_card.alpha( 110 ) : xdraw::color{ 255, 255, 255, 120 };
		const auto border_col = dark ? tokens::col_border.alpha( 35 ) : xdraw::color{ 24, 26, 38, 40 };

		xdraw::backdrop( x, base_ry, w, total_h, xdraw::corner_radius{ r }, backdrop_col );
		draw_list.rect_filled( x, base_ry, w, total_h, fill_col, xdraw::corner_radius{ r } );
		draw_list.rect( x, base_ry, w, total_h, border_col, xdraw::corner_radius{ r } );

		
		xdraw::push_font( rendering::g_fonts.sfpro_bold[ font_size ] );
		const auto header_text_x = x + ( w - header_tw ) * 0.5f;
		draw_list.text( header_text_x, base_ry + ( header_h - header_th ) * 0.5f, "keybinds", tokens::col_accent.alpha( static_cast< std::uint8_t >( tokens::col_accent.a * master_alpha ) ) );
		xdraw::pop_font( );
		
		
		const auto line_y = base_ry + header_h - underline_h;
		const auto transparent = tokens::col_accent.alpha( 0 );
		const auto bright = tokens::col_accent.alpha( static_cast< std::uint8_t >( 200.0f * master_alpha ) );
		
		
		draw_list.rect_filled_gradient( 
			x + w * 0.4f, 
			line_y, 
			w * 0.2f, 
			underline_h, 
			bright, bright, bright, bright
		);
		
		
		draw_list.rect_filled_gradient( 
			x, 
			line_y, 
			w * 0.4f, 
			underline_h, 
			transparent, bright, bright, transparent
		);
		
		
		draw_list.rect_filled_gradient( 
			x + w * 0.6f, 
			line_y, 
			w * 0.4f, 
			underline_h, 
			bright, transparent, transparent, bright
		);

		
		for ( auto& [name, state] : row_states )
			state.active_this_frame = false;

		float current_y = base_ry + header_h + pad_y;
		xdraw::push_font( rendering::g_fonts.sfpro_bold[ font_size ] );
		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			auto& anim = row_states[ e.name ];

			anim.active_this_frame = true;
			anim.alpha.fade_in( 0.2f );
			anim.alpha.update( );

			const auto row_alpha = anim.alpha.alpha( ) * master_alpha;
			const auto [nw, nh] = xdraw::measure_text( e.name );
			
			
			draw_list.text( x + pad_x, current_y + ( row_h - nh ) * 0.5f, e.name, tokens::col_text.alpha( static_cast< std::uint8_t >( 235.0f * row_alpha ) ) );

			current_y += row_h + row_spacing;
		}
		xdraw::pop_font( );

		
		for ( auto it = row_states.begin( ); it != row_states.end( ); )
		{
			if ( !it->second.active_this_frame )
			{
				it->second.alpha.fade_out( 0.15f );
				it->second.alpha.update( );

				if ( it->second.alpha.alpha( ) <= 0.001f )
				{
					it = row_states.erase( it );
					continue;
				}
			}
			++it;
		}
	}

		void widgets::indicators( xdraw::draw_list& draw_list )
		{
			if ( !settings::g_misc.indicators_enabled.value )
				return;

			struct indicator_entry
			{
				const xui::setting* setting{};
				const char* icon{};
				std::string label;
				bool always_show{ false };
				bool force_active{ false };
			};

			const auto& ctx = features::combat::g_shared.ctx( );
			const auto has_weapon = ctx.valid && ctx.weapon_type >= cstypes::weapon_type::pistol && ctx.weapon_type <= cstypes::weapon_type::lmg;

			constexpr auto max_count{ 3 };
			indicator_entry entries[ max_count ]{};
			auto entry_count{ 0 };

			const auto& rage = settings::g_combat.m_ragebot;
			if ( rage.enabled.value && has_weapon )
			{
				const auto& g = rage.get_group( ctx.weapon_type );

				auto& hc = entries[ entry_count++ ];
				hc.setting = &g.hitchance_override;
				hc.icon = "\xEF\x81\x9B";
				hc.always_show = true;
				hc.force_active = g.no_spread.value;
				if ( g.no_spread.value )
					hc.label = "NS";
				else
					hc.label = "HC " + std::to_string( g.hitchance_override.value ? g.hitchance_override_value.value : g.hitchance.value );

				auto& dmg = entries[ entry_count++ ];
				dmg.setting = &g.min_damage_override;
				dmg.icon = "\xEF\x85\x80";
				dmg.always_show = true;
				
				const auto dmg_val = g.min_damage_override.value ? g.min_damage_override_value.value : g.min_damage.value;
				if ( dmg_val == settings::combat::ragebot::k_lethal_min_damage )
					dmg.label = "DMG FL";
				else if ( dmg_val > 100 && dmg_val < settings::combat::ragebot::k_lethal_min_damage )
					dmg.label = "DMG HP+" + std::to_string( dmg_val - 100 );
				else
					dmg.label = "DMG " + std::to_string( dmg_val );
			}

			{
				auto& jb = entries[ entry_count++ ];
				jb.setting = &settings::g_movement.jumpbug;
				jb.icon = "\xEF\x85\x88";
				jb.label = "JB";
			}

			struct row_anim_t
			{
				animation::fade alpha;
			};

			static row_anim_t rows[ max_count ];
			static animation::progress height_anim;
			static float drag_offset_x{ 0.0f };
			static float drag_offset_y{ 0.0f };
			static bool is_dragging{ false };
			static bool positioned{ false };
			static float widget_x{ 16.0f };
			static float widget_y{ 200.0f };

			const auto [screen_w, screen_h] = xdraw::viewport_size( );
			const auto& input = xui::ctx( ).input;

			constexpr auto row_spacing{ 6.0f };
			constexpr auto row_h{ 32.0f };
			constexpr auto pad_x{ 8.0f };
			constexpr auto pad_y{ 10.0f };
			constexpr auto icon_gap{ 4.0f };
			constexpr auto right_pad{ 8.0f };

			float visible_count{ 0.0f };
			float row_widths[ max_count ]{};
			float widget_w{ 0.0f };

			for ( auto i = 0u; i < max_count; ++i )
			{
				const auto active = entries[ i ].setting && ( entries[ i ].always_show || entries[ i ].setting->bind.active );

				if ( active )
					rows[ i ].alpha.fade_in( 0.15f );
				else
					rows[ i ].alpha.fade_out( 0.15f );

				rows[ i ].alpha.update( );

				const auto row_alpha = rows[ i ].alpha.alpha( );
				if ( row_alpha <= 0.001f )
					continue;

				const auto& e = entries[ i ];
				if ( !e.setting )
					continue;

				visible_count += 1.0f;

				const auto icon_size = xdraw::measure_text( e.icon, rendering::g_fonts.fa_solid );
				const auto label_size = xdraw::measure_text( e.label, rendering::g_fonts.sfpro_bold[ rendering::fonts::size::normal ] );
				row_widths[ i ] = pad_x + icon_gap + static_cast< float >( icon_size.first ) + icon_gap + static_cast< float >( label_size.first ) + right_pad;
				widget_w = std::max( widget_w, row_widths[ i ] );
			}

			if ( visible_count <= 0.0f )
				return;

			const auto content_h = visible_count * row_h + ( visible_count > 1.0f ? ( visible_count - 1.0f ) * row_spacing : 0.0f );
			const auto total_h_target = content_h + pad_y * 2.0f;

			height_anim.set( total_h_target, 0.15f );
			height_anim.update( );
			const auto total_h = height_anim.value( );

			if ( !positioned )
			{
				positioned = true;
				widget_y = std::max( 0.0f, ( static_cast<float>( screen_h ) - total_h_target ) * 0.5f );
			}

			const auto hit_rect = xui::rect{ widget_x, widget_y, widget_w, total_h };

			if ( input.in_rect( hit_rect ) && input.mouse_clicked && !is_dragging )
			{
				is_dragging = true;
				drag_offset_x = input.mouse_x - widget_x;
				drag_offset_y = input.mouse_y - widget_y;
			}

			if ( is_dragging )
			{
				if ( input.mouse_down )
				{
					widget_x = input.mouse_x - drag_offset_x;
					widget_y = input.mouse_y - drag_offset_y;

					widget_x = std::max( 0.0f, std::min( widget_x, static_cast<float>( screen_w ) - widget_w ) );
					widget_y = std::max( 0.0f, std::min( widget_y, static_cast<float>( screen_h ) - total_h ) );
				}
				else
				{
					is_dragging = false;
				}
			}

			float current_y = widget_y + pad_y;

			for ( auto i = 0u; i < max_count; ++i )
			{
				const auto& e = entries[ i ];
				const auto row_alpha = rows[ i ].alpha.alpha( );

				if ( !e.setting || row_alpha <= 0.001f )
					continue;

				const auto row_x = widget_x + pad_x;
				const auto row_w = row_widths[ i ];
				const auto is_active = e.setting->value || e.force_active;

const auto row_alpha_byte = static_cast< std::uint8_t >( 255.0f * row_alpha );
			const auto dark_rows = rendering::g_menu.is_dark( );

			const auto row_backdrop = dark_rows ? tokens::col_dark : xdraw::color{ 250, 251, 255, 225 };
			const auto row_fill_base = dark_rows ? tokens::col_card.alpha( static_cast< std::uint8_t >( 24.0f * row_alpha ) ) : xdraw::color{ 24, 26, 38, static_cast< std::uint8_t >( 16.0f * row_alpha ) };
			const auto row_fill_active = dark_rows ? tokens::col_card.alpha( static_cast< std::uint8_t >( 40.0f * row_alpha ) ) : xdraw::color{ 24, 26, 38, static_cast< std::uint8_t >( 34.0f * row_alpha ) };
			const auto row_border = dark_rows ? tokens::col_border.alpha( static_cast< std::uint8_t >( 45.0f * row_alpha ) ) : xdraw::color{ 24, 26, 38, static_cast< std::uint8_t >( 45.0f * row_alpha ) };

			xdraw::backdrop( widget_x, current_y, row_w, row_h, xdraw::corner_radius{ 12.0f }, row_backdrop );

			auto bg = is_active ? row_fill_active : row_fill_base;
			draw_list.rect_filled( widget_x, current_y, row_w, row_h, bg, xdraw::corner_radius{ 12.0f } );
			draw_list.rect( widget_x, current_y, row_w, row_h, row_border, xdraw::corner_radius{ 12.0f } );

				auto icon_col = tokens::col_text_dim.alpha( static_cast< std::uint8_t >( 255.0f * row_alpha ) );
				if ( is_active )
					icon_col = tokens::col_accent.alpha( static_cast< std::uint8_t >( 255.0f * row_alpha ) );

				const auto icon_size = xdraw::measure_text( e.icon, rendering::g_fonts.fa_solid );
				draw_list.text( row_x + icon_gap, current_y + ( row_h - icon_size.second ) * 0.5f, e.icon, icon_col, rendering::g_fonts.fa_solid );

				auto text_col = tokens::col_text.alpha( static_cast< std::uint8_t >( 235.0f * row_alpha ) );
				if ( is_active )
					text_col = tokens::col_text.alpha( static_cast< std::uint8_t >( 255.0f * row_alpha ) );

				const auto label_size = xdraw::measure_text( e.label, rendering::g_fonts.sfpro_bold[ rendering::fonts::size::normal ] );
				draw_list.text( row_x + icon_gap + static_cast< float >( icon_size.first ) + icon_gap, current_y + ( row_h - label_size.second ) * 0.5f, e.label, text_col, rendering::g_fonts.sfpro_bold[ rendering::fonts::size::normal ] );

				current_y += row_h + row_spacing;
			}
		}

		void widgets::crosshair_indicators( xdraw::draw_list& draw_list )
		{
			const auto& cfg = settings::g_misc.m_crosshair_indicators;
			if ( !cfg.enabled.value )
				return;

			const auto local = systems::g_local.get( );
			if ( !local.is_alive || systems::g_local.is_in_cinematic( ) || systems::g_local.is_in_time_freeze( ) )
				return;

			struct entry_t
			{
				bool active{ false };
				std::string label{};
			};

			constexpr auto max_count{ 5 };
			entry_t entries[ max_count ]{};

			const auto& ctx = features::combat::g_shared.ctx( );
			const auto has_weapon = ctx.valid && ctx.weapon_type >= cstypes::weapon_type::pistol && ctx.weapon_type <= cstypes::weapon_type::lmg;

			const auto& rage = settings::g_combat.m_ragebot;
			const auto rage_ready = rage.enabled.value && has_weapon;


			if ( rage_ready )
			{
				const auto& g = rage.get_group( ctx.weapon_type );

				if ( g.hitchance_override.value || g.hitchance_override.bind.active )
				{
					entries[ 0 ].active = true;
					entries[ 0 ].label = "HC " + std::to_string( g.hitchance_override_value.value );
				}

				if ( g.min_damage_override.value || g.min_damage_override.bind.active )
				{
					const auto v = g.min_damage_override_value.value;
					entries[ 1 ].active = true;

					if ( v == settings::combat::ragebot::k_lethal_min_damage )
						entries[ 1 ].label = "MD FL";
					else if ( v > 100 && v < settings::combat::ragebot::k_lethal_min_damage )
						entries[ 1 ].label = "MD HP+" + std::to_string( v - 100 );
					else
						entries[ 1 ].label = "MD " + std::to_string( v );
				}

				if ( g.force_shot.value || g.force_shot.bind.active )
				{
					entries[ 3 ].active = true;
					entries[ 3 ].label = "FS";
				}
			}


			{
				const auto& aa = settings::g_combat.m_antiaim;
				if ( aa.enabled.value && aa.hide_shots.value )
				{
					entries[ 2 ].active = true;
					entries[ 2 ].label = "HS";
				}
			}


			{
				const auto& dp = settings::g_combat.m_duckpeek.enabled;
				if ( dp.value || dp.bind.active )
				{
					entries[ 4 ].active = true;
					entries[ 4 ].label = "DP";
				}
			}

			struct row_anim_t
			{
				animation::fade alpha;
			};

			static row_anim_t rows[ max_count ];
			static bool positioned{ false };
			static float center_x{ 0.0f };
			static float center_y{ 0.0f };
			static float drag_dx{ 0.0f };
			static float drag_dy{ 0.0f };
			static bool is_dragging{ false };

			const auto [ screen_w, screen_h ] = xdraw::viewport_size( );

			constexpr auto gap_below_crosshair{ 26.0f };
			if ( !positioned )
			{
				positioned = true;
				center_x = static_cast< float >( screen_w ) * 0.5f;
				center_y = static_cast< float >( screen_h ) * 0.5f + gap_below_crosshair;
			}

			auto* const font = rendering::g_fonts.sfpro_bold[ rendering::fonts::size::petite ];

			constexpr auto row_h{ 15.0f };
			constexpr auto row_spacing{ 3.0f };

			float visible_count{ 0.0f };
			float max_w{ 0.0f };

			for ( auto i = 0; i < max_count; ++i )
			{
				if ( entries[ i ].active )
					rows[ i ].alpha.fade_in( 0.12f );
				else
					rows[ i ].alpha.fade_out( 0.12f );

				rows[ i ].alpha.update( );

				if ( rows[ i ].alpha.alpha( ) <= 0.001f )
					continue;

				visible_count += 1.0f;
				max_w = std::max( max_w, xdraw::measure_text( entries[ i ].label, font ).first );
			}

			if ( visible_count <= 0.0f )
				return;

			const auto stack_h = visible_count * row_h + ( visible_count - 1.0f ) * row_spacing;

			const auto& input = xui::ctx( ).input;
			const auto hit_rect = xui::rect{ center_x - max_w * 0.5f - 6.0f, center_y - stack_h * 0.5f - 4.0f, max_w + 12.0f, stack_h + 8.0f };

			if ( input.in_rect( hit_rect ) && input.mouse_clicked && !is_dragging )
			{
				is_dragging = true;
				drag_dx = input.mouse_x - center_x;
				drag_dy = input.mouse_y - center_y;
			}

			if ( is_dragging )
			{
				if ( input.mouse_down )
				{
					center_x = std::max( max_w * 0.5f, std::min( input.mouse_x - drag_dx, static_cast< float >( screen_w ) - max_w * 0.5f ) );
					center_y = std::max( stack_h * 0.5f, std::min( input.mouse_y - drag_dy, static_cast< float >( screen_h ) - stack_h * 0.5f ) );
				}
				else
				{
					is_dragging = false;
				}
			}

			const auto base = cfg.color.value;
			auto& glow = xdraw::get_glow( );

			auto current_y = center_y - stack_h * 0.5f;

			for ( auto i = 0; i < max_count; ++i )
			{
				const auto a = rows[ i ].alpha.alpha( );
				if ( a <= 0.001f )
					continue;

				const auto sz = xdraw::measure_text( entries[ i ].label, font );
				const auto tx = center_x - sz.first * 0.5f;
				const auto ty = current_y + ( row_h - sz.second ) * 0.5f;

				const auto col = xdraw::color{ base.r, base.g, base.b, static_cast< std::uint8_t >( static_cast< float >( base.a ) * a ) };

				if ( cfg.glow.value )
				{
					const auto ga = static_cast< std::uint8_t >( static_cast< float >( base.a ) * cfg.glow_strength.value * a );
					glow.text( tx, ty, entries[ i ].label, xdraw::color{ base.r, base.g, base.b, ga }, font );
				}

				draw_list.text( tx, ty, entries[ i ].label, col, xdraw::text_style::shadowed, font );

				current_y += row_h + row_spacing;
			}
		}

}
