#include <pch/pch.hpp>
#include <utilities/math/math.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../rendering.hpp"
#include <utilities/security/security.hpp>

namespace rendering {

	void widgets::draw( )
	{
		auto& dl = xdraw::get( );

		if ( settings::g_misc.m_watermark.enabled.value )
		{
			this->watermark( dl );
		}

		this->keybinds( dl );
	}

	void widgets::watermark( xdraw::draw_list& draw_list )
	{
		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto& s  = xui::ctx( ).style;
		const auto& wm = settings::g_misc.m_watermark;
		const auto framerate = xdraw::framerate( );
		const auto local = systems::g_local.get( );

		constexpr auto h{ 24.0f };
		constexpr auto margin{ 10.0f };
		constexpr auto r{ 8.0f };
		constexpr auto inner_r{ 6.0f };
		constexpr auto inner_pad{ 2.0f };
		constexpr auto text_pad_x{ 8.0f };
		constexpr auto text_nudge{ 0.5f };
		constexpr auto section_spacing{ 2.0f };
		constexpr auto logo_icon_pad{ 7.0f };

		// ── time ────────────────────────────────────────────────────────────
		SYSTEMTIME st{};
		GetLocalTime( &st );
		char time_buf[ 8 ]{};
		std::snprintf( time_buf, sizeof( time_buf ), "%02d:%02d", st.wHour, st.wMinute );

		// ── fps ─────────────────────────────────────────────────────────────
		static auto smoothed_fps{ 0.0f };
		if ( smoothed_fps == 0.0f ) smoothed_fps = framerate;
		smoothed_fps += ( framerate - smoothed_fps ) * std::min( 2.0f * xdraw::delta_time( ), 1.0f );
		char fps_val[ 8 ]{};
		std::snprintf( fps_val, sizeof( fps_val ), "%.0f", smoothed_fps );

		// ── ping ────────────────────────────────────────────────────────────
		auto ping{ 0 };
		if ( local.is_alive && local.controller && systems::g_entities.exists( local.controller ) )
			ping = memory::read<std::uint32_t>( local.controller + SCHEMA( "CCSPlayerController", "m_iPing"_hash ) );
		char ping_val[ 8 ]{};
		std::snprintf( ping_val, sizeof( ping_val ), "%d", ping );

		// ── map name (stored reliably from level_initialization hook) ────────
		const bool has_map = wm.show_map.value && !s_map_name.empty( );

		// ── tick rate (measured from server_tick delta over ~2 s of real time) ─
		static auto last_server_tick{ 0 };
		static auto last_curtime{ 0.0f };
		static auto measured_tickrate{ 0 };

		if ( local.controller )
		{
			const auto net_for_tick = addresses::globals::network_client_service;
			const auto tick_state   = net_for_tick ? memory::call_vfunc<std::uintptr_t>( net_for_tick, 23 ) : 0;
			const auto server_tick  = tick_state   ? memory::read<int>( tick_state + 892 ) : 0;
			const auto gv           = memory::read<std::uintptr_t>( addresses::globals::global_vars );
			const auto curtime      = gv ? memory::read<float>( gv + 0x30 ) : 0.0f;

			if ( server_tick > 0 && last_server_tick > 0 && curtime - last_curtime >= 2.0f )
			{
				const auto tick_delta = server_tick - last_server_tick;
				const auto time_delta = curtime - last_curtime;
				if ( tick_delta > 0 && time_delta > 0.5f )
				{
					const auto rate = static_cast<int>( std::round( tick_delta / time_delta ) );
					if ( rate >= 16 && rate <= 256 ) measured_tickrate = rate;
				}
				last_server_tick = server_tick;
				last_curtime     = curtime;
			}
			else if ( last_server_tick == 0 && server_tick > 0 )
			{
				last_server_tick = server_tick;
				last_curtime     = curtime;
			}
		}
		else
		{
			last_server_tick = 0;
			last_curtime     = 0.0f;
			measured_tickrate = 0;
		}

		const bool has_tick = wm.show_tick.value && local.controller && measured_tickrate > 0;
		char tick_val[ 8 ]{};
		if ( has_tick ) std::snprintf( tick_val, sizeof( tick_val ), "%d", measured_tickrate );

		// ── velocity ──────────────────────────────────────────────────────
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

		// ── measure text ─────────────────────────────────────────────────────
		const auto [name_tw, name_th] = xdraw::measure_text( "swift.fly" );
		const auto [user_tw, user_th] = xdraw::measure_text( "developer" );
		const auto [ping_vw, ping_vh] = xdraw::measure_text( ping_val );
		const auto [ping_uw, ping_uh] = xdraw::measure_text( " ms" );
		const auto [fps_vw,  fps_vh]  = xdraw::measure_text( fps_val );
		const auto [fps_uw,  fps_uh]  = xdraw::measure_text( " fps" );
		const auto [time_tw, time_th] = xdraw::measure_text( time_buf );

		float map_tw{}, map_th{};
		if ( has_map ) std::tie( map_tw, map_th ) = xdraw::measure_text( s_map_name.c_str( ) );

		float tick_vw{}, tick_vh{}, tick_uw{}, tick_uh{};
		if ( has_tick )
		{
			std::tie( tick_vw, tick_vh ) = xdraw::measure_text( tick_val );
			std::tie( tick_uw, tick_uh ) = xdraw::measure_text( " tick" );
		}

		float vel_vw{}, vel_vh{}, vel_uw{}, vel_uh{};
		if ( has_velocity )
		{
			std::tie( vel_vw, vel_vh ) = xdraw::measure_text( vel_val );
			std::tie( vel_uw, vel_uh ) = xdraw::measure_text( " u/s" );
		}


		// ── pill widths ──────────────────────────────────────────────────────
		const auto logo_pill_w = logo_icon_pad + name_tw + text_pad_x;
		const auto user_pill_w = user_tw + text_pad_x * 2.0f;
		const auto ping_pill_w = ping_vw + ping_uw + text_pad_x * 2.0f;
		const auto fps_pill_w  = fps_vw  + fps_uw  + text_pad_x * 2.0f;
		const auto time_pill_w = time_tw + text_pad_x * 2.0f;
		const auto map_pill_w  = map_tw  + text_pad_x * 2.0f;
		const auto tick_pill_w = tick_vw + tick_uw + text_pad_x * 2.0f;
		const auto vel_pill_w  = vel_vw + vel_uw + text_pad_x * 2.0f;

		// ── dynamic total width ──────────────────────────────────────────────
		float target_w = inner_pad + logo_pill_w + section_spacing;
		if ( wm.show_user.value ) target_w += user_pill_w + section_spacing;
		if ( has_map )            target_w += map_pill_w  + section_spacing;
		if ( wm.show_ping.value ) target_w += ping_pill_w + section_spacing;
		if ( has_velocity )       target_w += vel_pill_w  + section_spacing;
		if ( wm.show_fps.value )  target_w += fps_pill_w  + section_spacing;
		if ( has_tick )           target_w += tick_pill_w + section_spacing;
		if ( wm.show_time.value ) target_w += time_pill_w + section_spacing;
		target_w = target_w - section_spacing + inner_pad;

		static auto smoothed_w{ 0.0f };
		if ( smoothed_w == 0.0f ) smoothed_w = target_w;
		smoothed_w += ( target_w - smoothed_w ) * std::min( 8.0f * xdraw::delta_time( ), 1.0f );

		const auto w = smoothed_w;
		const auto x = static_cast<float>( screen_w ) - w - margin;
		const auto y = margin;

		draw_list.rect_filled_blurred( x, y, w, h, xdraw::corner_radius{ r } );
		draw_list.rect_filled( x, y, w, h, s.window_bg, xdraw::corner_radius{ r } );

		auto cx = x + inner_pad;

		auto draw_split_pill = [ & ]( const char* value, float vw, float vh, const char* unit, float uw, float uh, float pill_w )
			{
				draw_list.rect_filled( cx, y + inner_pad, pill_w, inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );
				draw_list.text( cx + text_pad_x, y + ( h - vh ) * 0.5f + text_nudge, value, s.accent );
				draw_list.text( cx + text_pad_x + vw, y + ( h - uh ) * 0.5f + text_nudge, unit, s.text_dim );
				cx += pill_w + section_spacing;
			};

		auto draw_pill = [ & ]( const char* text, float tw, float th, float pill_w )
			{
				draw_list.rect_filled( cx, y + inner_pad, pill_w, inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );
				draw_list.text( cx + text_pad_x, y + ( h - th ) * 0.5f + text_nudge, text, s.accent );
				cx += pill_w + section_spacing;
			};

		// logo pill (always shown)
		draw_list.rect_filled( cx, y + inner_pad, logo_pill_w, inner_h, s.accent, xdraw::corner_radius{ inner_r } );
		draw_list.text( cx + logo_icon_pad,
			y + ( h - name_th ) * 0.5f + text_nudge, "swift.fly", s.checkbox_mark_icon );
		cx += logo_pill_w + section_spacing;

		if ( wm.show_user.value ) draw_pill( "developer", user_tw, user_th, user_pill_w );
		if ( has_map )            draw_pill( s_map_name.c_str( ), map_tw, map_th, map_pill_w );
		if ( wm.show_ping.value ) draw_split_pill( ping_val, ping_vw, ping_vh, " ms",   ping_uw, ping_uh, ping_pill_w );
		if ( has_velocity )       draw_split_pill( vel_val,  vel_vw,  vel_vh,  " u/s",  vel_uw,  vel_uh,  vel_pill_w );
		if ( wm.show_fps.value )  draw_split_pill( fps_val,  fps_vw,  fps_vh,  " fps",  fps_uw,  fps_uh,  fps_pill_w );
		if ( has_tick )           draw_split_pill( tick_val, tick_vw, tick_vh, " tick", tick_uw, tick_uh, tick_pill_w );
		if ( wm.show_time.value ) draw_pill( time_buf, time_tw, time_th, time_pill_w );
	}

	void widgets::keybinds( xdraw::draw_list& draw_list )
	{
		// Check if keybinds widget is enabled
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

		constexpr auto margin{ 10.0f };
		constexpr auto row_spacing{ 4.0f };
		constexpr auto row_h{ 22.0f };
		constexpr auto header_h{ 38.0f }; // Increased from 28 to 38 for more vertical space
		constexpr auto r{ 10.0f };
		constexpr auto pad_x{ 12.0f };
		constexpr auto pad_y{ 8.0f };
		constexpr auto underline_h{ 1.0f };
		constexpr auto min_w{ 200.0f };

		// Dragging state
		static float drag_offset_x{ 0.0f };
		static float drag_offset_y{ 0.0f };
		static bool is_dragging{ false };
		static float widget_x{ margin };
		static float widget_y{ 200.0f }; // Initial Y position

		struct bind_entry
		{
			const char* name;
			char mode_text[ 32 ];
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
				if ( setting == &g.min_damage_override || setting == &g.hitchance_override || setting == &g.force_shot || setting == &g.force_shot_air || setting == &g.body_aim || setting == &g.silent || setting == &g.no_spread )
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
						if ( setting == &g.min_damage_override || setting == &g.hitchance_override || setting == &g.force_shot || setting == &g.force_shot_air || setting == &g.body_aim )
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
				
				// Format mode text - capitalize first letter
				const char* mode_str = xui::binds::mode_name( setting->bind.mode );
				if ( std::strcmp( mode_str, "toggle" ) == 0 )
					std::snprintf( e.mode_text, sizeof( e.mode_text ), "Toggle" );
				else if ( std::strcmp( mode_str, "hold on" ) == 0 )
					std::snprintf( e.mode_text, sizeof( e.mode_text ), "Hold" );
				else if ( std::strcmp( mode_str, "hold off" ) == 0 )
					std::snprintf( e.mode_text, sizeof( e.mode_text ), "Always" );
				else
					std::snprintf( e.mode_text, sizeof( e.mode_text ), "Toggle" );
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
				if ( !settings::g_combat.m_legitbot.enabled.value || !has_weapon )
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
				const char* mode_str = xui::binds::mode_name( setting->bind.mode );
				if ( std::strcmp( mode_str, "toggle" ) == 0 )
					std::snprintf( e.mode_text, sizeof( e.mode_text ), "Toggle" );
				else if ( std::strcmp( mode_str, "hold on" ) == 0 )
					std::snprintf( e.mode_text, sizeof( e.mode_text ), "Hold" );
				else if ( std::strcmp( mode_str, "hold off" ) == 0 )
					std::snprintf( e.mode_text, sizeof( e.mode_text ), "Always" );
				else
					std::snprintf( e.mode_text, sizeof( e.mode_text ), "Toggle" );
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
			const char* mode_str = xui::binds::mode_name( setting->bind.mode );
			if ( std::strcmp( mode_str, "toggle" ) == 0 )
				std::snprintf( e.mode_text, sizeof( e.mode_text ), "Toggle" );
			else if ( std::strcmp( mode_str, "hold on" ) == 0 )
				std::snprintf( e.mode_text, sizeof( e.mode_text ), "Hold" );
			else if ( std::strcmp( mode_str, "hold off" ) == 0 )
				std::snprintf( e.mode_text, sizeof( e.mode_text ), "Always" );
			else
				std::snprintf( e.mode_text, sizeof( e.mode_text ), "Toggle" );
		}

		if ( count > 0 )
			container_alpha.fade_in( 0.2f );
		else
			container_alpha.fade_in( 0.2f ); // Always show keybinds widget

		container_alpha.update( );
		if ( !container_alpha.visible( ) )
			return;

		const auto master_alpha = container_alpha.alpha( );
		const auto content_h = static_cast< float >( count ) * row_h + ( count > 1 ? ( count - 1 ) * row_spacing : 0.0f );
		
		// Calculate max width first
		float max_w = min_w;
		xdraw::push_font( rendering::g_fonts.hurme_black[ rendering::fonts::size::normal ] );
		const auto [header_tw, header_th] = xdraw::measure_text( "keybinds" );
		xdraw::pop_font( );
		
		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			const auto [nw, nh] = xdraw::measure_text( e.name );
			const auto [mw, mh] = xdraw::measure_text( e.mode_text );
			const auto row_w = pad_x + nw + pad_x + mw + pad_x;
			if ( row_w > max_w ) max_w = row_w;
		}
		
		const auto total_h = header_h + pad_y + content_h + pad_y;
		
		// Handle dragging
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
				
				// Clamp to screen bounds
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
		const auto master_u8 = static_cast< std::uint8_t >( 255.0f * master_alpha );

		// Draw container background
		draw_list.rect_filled_blurred( x, base_ry, w, total_h, xdraw::corner_radius{ r }, xdraw::color{ 255, 255, 255, master_u8 } );
		draw_list.rect_filled( x, base_ry, w, total_h, xdraw::color{ 17, 17, 17, static_cast< std::uint8_t >( 230.0f * master_alpha ) }, xdraw::corner_radius{ r } );

		// Draw header with hurme_black font (centered)
		xdraw::push_font( rendering::g_fonts.hurme_black[ rendering::fonts::size::normal ] );
		const auto header_text_x = x + ( w - header_tw ) * 0.5f; // Center the text
		draw_list.text( header_text_x, base_ry + ( header_h - header_th ) * 0.5f, "keybinds", tokens::col_accent.alpha( static_cast< std::uint8_t >( tokens::col_accent.a * master_alpha ) ) );
		xdraw::pop_font( );
		
		// Draw gradient underline below header (like tab underline)
		const auto line_y = base_ry + header_h - underline_h;
		const auto transparent = tokens::col_accent.alpha( 0 );
		const auto bright = tokens::col_accent.alpha( static_cast< std::uint8_t >( 200.0f * master_alpha ) );
		
		// Center part (20% width)
		draw_list.rect_filled_gradient( 
			x + w * 0.4f, 
			line_y, 
			w * 0.2f, 
			underline_h, 
			bright, bright, bright, bright
		);
		
		// Left fade gradient (40% width)
		draw_list.rect_filled_gradient( 
			x, 
			line_y, 
			w * 0.4f, 
			underline_h, 
			transparent, bright, bright, transparent
		);
		
		// Right fade gradient (40% width)
		draw_list.rect_filled_gradient( 
			x + w * 0.6f, 
			line_y, 
			w * 0.4f, 
			underline_h, 
			bright, transparent, transparent, bright
		);

		// Draw entries
		for ( auto& [name, state] : row_states )
			state.active_this_frame = false;

		float current_y = base_ry + header_h + pad_y;
		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			auto& anim = row_states[ e.name ];

			anim.active_this_frame = true;
			anim.alpha.fade_in( 0.2f );
			anim.alpha.update( );

			const auto row_alpha = anim.alpha.alpha( ) * master_alpha;
			const auto [nw, nh] = xdraw::measure_text( e.name );
			const auto [mw, mh] = xdraw::measure_text( e.mode_text );
			
			// Draw name on left
			draw_list.text( x + pad_x, current_y + ( row_h - nh ) * 0.5f, e.name, xdraw::color{ 200, 200, 200, static_cast< std::uint8_t >( 255.0f * row_alpha ) } );
			
			// Draw mode on right
			draw_list.text( x + w - pad_x - mw, current_y + ( row_h - mh ) * 0.5f, e.mode_text, tokens::col_accent.alpha( static_cast< std::uint8_t >( tokens::col_accent.a * row_alpha ) ) );

			current_y += row_h + row_spacing;
		}

		// Clean up inactive animations
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

} // namespace rendering
