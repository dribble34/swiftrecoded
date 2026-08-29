#include <numbers>
#include <chrono>
#include <deque>
#include <algorithm>

#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <core/rendering/rendering.hpp>
#include <protection/game_addresses.hpp>
#include <utilities//addresses/addresses.hpp>
#include <external/xdraw/xui/xui.hpp>

namespace features::misc {

	void hud::on_render( xdraw::draw_list& draw_list )
	{
		if ( systems::g_local.is_in_cinematic( ) || systems::g_local.is_in_time_freeze( ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn || !local.controller || !systems::g_entities.exists( local.controller ) || !local.is_alive )
		{
			return;
		}

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto cx = static_cast< float >( screen_w ) * 0.5f;
		const auto cy = static_cast< float >( screen_h ) * 0.5f;

		this->do_scope( draw_list, cx, cy, static_cast< float >( screen_h ), local.pawn );
		this->do_crosshair( draw_list, cx, cy );
		this->do_velocity( );
	}

	void hud::do_crosshair( xdraw::draw_list& draw_list, float cx, float cy ) const
	{
		const auto& cfg = settings::g_misc.m_hud.m_crosshair;
		if ( !cfg.enabled.value )
		{
			return;
		}

		if ( this->m_scope_anim > 0.01f )
		{
			return;
		}

		const auto s = cfg.size;
		const auto o = cfg.outline;

		if ( o > 0.0f )
		{
			draw_list.rect_filled( cx - s - o, cy - s - o, ( s + o ) * 2.0f, ( s + o ) * 2.0f, cfg.outline_color );
		}

		draw_list.rect_filled( cx - s, cy - s, s * 2.0f, s * 2.0f, cfg.color );
	}

	void hud::do_scope( xdraw::draw_list& draw_list, float cx, float cy, float screen_h, std::uintptr_t local_pawn )
	{
		const auto& cfg = settings::g_misc.m_hud.m_scope;
		if ( !cfg.enabled.value )
		{
			return;
		}

		const auto is_scoped = memory::read<bool>( local_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );

		this->m_scope_anim = std::lerp( this->m_scope_anim, is_scoped ? 1.0f : 0.0f, std::min( xdraw::delta_time( ) * cfg.anim_speed, 1.0f ) );
		if ( this->m_scope_anim < 0.01f )
		{
			this->m_cached_spread_pixels = 0.0f;
			this->m_scope_update_frame = 0;
			return;
		}

		if ( cfg.style.value == settings::misc::hud::scope::style_type::classic )
		{
			constexpr auto thickness = 1.0f;
			const auto alpha = static_cast< std::uint8_t >( cfg.color.value.a * this->m_scope_anim );
			const auto col = xdraw::color{ cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, alpha };

			const auto [sw, sh] = xdraw::viewport_size( );
			const float mid_x = std::floorf( static_cast<float>( sw ) * 0.5f );
			const float mid_y = std::floorf( static_cast<float>( sh ) * 0.5f );

			draw_list.line( mid_x, 0.0f, mid_x, static_cast<float>( sh ), col, thickness );
			draw_list.line( 0.0f, mid_y, static_cast<float>( sw ), mid_y, col, thickness );
			return;
		}

		++this->m_scope_update_frame;
		const auto should_recalc = this->m_scope_update_frame >= 2 || this->m_cached_spread_pixels <= 0.0f;

		if ( should_recalc )
		{
			this->m_scope_update_frame = 0;

			const auto weapon_services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
			if ( weapon_services )
			{
				const auto weapon_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
				const auto weapon = ( weapon_handle && weapon_handle != 0xffffffff ) ? systems::g_entities.lookup( weapon_handle ) : 0ull;

				if ( weapon )
				{
					const auto weapon_vdata = memory::read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 );

					if ( weapon_vdata )
					{
						const auto vel = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
						const auto fire_mode = memory::read<int>( weapon + SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash ) );
						const auto accuracy_penalty = memory::read<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_hash ) );
						const auto turning_inaccuracy = memory::read<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracy"_hash ) );

						const auto max_speed_pair = memory::read<std::pair<float, float>>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) );
						const auto inaccuracy_move_pair = memory::read<std::pair<float, float>>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyMove"_hash ) );
						const auto inaccuracy_jump_initial = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_hash ) );
						const auto inaccuracy_jump_apex = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );

						const auto max_speed = fire_mode ? max_speed_pair.second : max_speed_pair.first;
						const auto inaccuracy_move = fire_mode ? inaccuracy_move_pair.second : inaccuracy_move_pair.first;

						const auto speed = vel.length_2d( );
						const auto flags = memory::read<std::uint32_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );
						const auto is_walking = memory::read<bool>( local_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsWalking"_hash ) );
						const auto on_ground = ( flags & 1 ) != 0;

						const auto edge0 = max_speed * 0.34f;
						const auto edge1 = max_speed * 0.95f;

						auto move_factor = ( edge0 == edge1 ) ? ( speed >= edge1 ? 1.0f : 0.0f ) : std::clamp( ( speed - edge0 ) / ( edge1 - edge0 ), 0.0f, 1.0f );
						auto move_inac{ 0.0f };

						if ( move_factor > 0.0f )
						{
							if ( !is_walking )
							{
								move_factor = std::powf( move_factor, 0.25f );
							}

							move_inac = move_factor * inaccuracy_move;
						}

						auto air_inac{ 0.0f };

						if ( !on_ground )
						{
							const auto jump_impulse = CONVAR ("sv_jump_impulse")->get<float>( );
							const auto sqrt_threshold = std::sqrtf( std::fabsf( jump_impulse ) );
							const auto sqrt_vertical = std::sqrtf( std::fabsf( vel.z ) );
							const auto lo = sqrt_threshold * 0.25f;

							if ( lo == sqrt_threshold )
							{
								air_inac = ( sqrt_vertical >= sqrt_threshold ) ? inaccuracy_jump_initial : inaccuracy_jump_apex;
							}
							else
							{
								const auto frac = ( sqrt_vertical - lo ) / ( sqrt_threshold - lo );
								air_inac = inaccuracy_jump_apex + frac * ( inaccuracy_jump_initial - inaccuracy_jump_apex );
							}

							air_inac = std::clamp( air_inac, 0.0f, inaccuracy_jump_initial * 2.0f );
						}

						const auto inaccuracy = std::fminf( 1.0f, turning_inaccuracy + move_inac + air_inac + accuracy_penalty );

						const auto fov_rad = systems::g_view.fov( ) * ( std::numbers::pi_v<float> / 180.0f );
						this->m_cached_spread_pixels = inaccuracy * 320.0f / std::tanf( fov_rad * 0.5f ) * ( screen_h / 480.0f );
					}
				}
			}
		}

		// Fixed gap without spread recoil movement
		const auto gap = cfg.gap.value + ( 40.0f * ( 1.0f - this->m_scope_anim ) );
		const auto length = cfg.line_length * this->m_scope_anim;
		const auto alpha = static_cast< std::uint8_t >( cfg.color.value.a * this->m_scope_anim );

		const auto col_clear = xdraw::color{ cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, 0 };
		const auto col_solid = xdraw::color{ cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, alpha };
		const auto& col_start = cfg.fade_in ? col_clear : col_solid;

		if ( cfg.glow && alpha > 0 )
		{
			auto& glow = xdraw::get_glow( );
			const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( alpha ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, glow_a };

			const auto glow_draw_line = [ & ]( float x1, float y1, float x2, float y2 )
				{
					const auto mx = ( x1 + x2 ) * 0.5f;
					const auto my = ( y1 + y2 ) * 0.5f;

					const auto glow_thickness = cfg.thickness + 0.5f;

					const auto gc_clear = xdraw::color{ cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, 0 };
					const auto& gc_start = cfg.fade_in ? gc_clear : glow_col;

					const float pts[ ]{ x1, y1, mx, my, x2, y2 };
					const xdraw::color cols[ ]{ gc_start, glow_col, glow_col };

					glow.polyline_gradient( pts, cols, false, glow_thickness );
				};

			glow_draw_line( cx, cy - gap, cx, cy - gap - length );
			glow_draw_line( cx, cy + gap, cx, cy + gap + length );
			glow_draw_line( cx - gap, cy, cx - gap - length, cy );
			glow_draw_line( cx + gap, cy, cx + gap + length, cy );
		}

		const auto draw_line = [ & ]( float x1, float y1, float x2, float y2 )
			{
				const auto mx = ( x1 + x2 ) * 0.5f;
				const auto my = ( y1 + y2 ) * 0.5f;

				const float pts[ ]{ x1, y1, mx, my, x2, y2 };
				const xdraw::color cols[ ]{ col_start, col_solid, col_solid };

				draw_list.polyline_gradient( pts, cols, false, cfg.thickness );
			};

		draw_line( cx, cy - gap, cx, cy - gap - length );
		draw_line( cx, cy + gap, cx, cy + gap + length );
		draw_line( cx - gap, cy, cx - gap - length, cy );
		draw_line( cx + gap, cy, cx + gap + length, cy );

		if ( cfg.spread_circle )
		{
			const auto spread_radius = std::lerp( this->m_spread_smooth, this->m_cached_spread_pixels, std::min( xdraw::delta_time( ) * 50.0f, 1.0f ) );

			const auto col = xdraw::color{ cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, static_cast< std::uint8_t >( cfg.color.value.a * 0.5f ) };

			draw_list.circle_filled( cx, cy, spread_radius, col, 200 );
		}
	}

	void hud::do_velocity( )
	{
		
		g_velocity_graph.update();
		g_velocity_graph.render();
		g_velocity_graph.render_indicator();
	}

	
	
	

	namespace {
		struct velocity_data {
			std::deque<float> velocity_history;
			float max_velocity = 0.0f;
			float jump_height = 0.0f;
			float multiplicator = 1.0f;
			std::chrono::steady_clock::time_point last_max_reset;
			std::chrono::steady_clock::time_point last_mul_reset;
			static constexpr size_t max_history_size = 100;
			static constexpr float max_reset_interval = 1.0f; 
			static constexpr float mul_reset_interval = 2.0f; 
		};
		
		velocity_data g_velocity_data;
	}

	void velocity_graph::update() {
		const auto local = systems::g_local.get();
		if (!local.is_alive || !local.pawn)
			return;

		const auto velocity = memory::read<math::vector3>(local.pawn + SCHEMA("C_BaseEntity", "m_vecVelocity"_hash));
		const auto speed_2d = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
		
		g_velocity_data.jump_height = velocity.z;
		
		static float last_speed = 0.0f;
		static float max_jump_distance = 0.0f;
		const auto now = std::chrono::steady_clock::now();
		
		if (g_velocity_data.jump_height > 0.1f) {
			const float current_jump_distance = speed_2d * 0.1f;
			max_jump_distance = std::max(max_jump_distance, current_jump_distance);
		} else if (g_velocity_data.jump_height <= 0.0f && max_jump_distance > 0.0f) {
			const float ideal_distance = 250.0f;
			g_velocity_data.multiplicator = std::min(max_jump_distance / ideal_distance, 2.0f);
			max_jump_distance = 0.0f;
		}
		
		const auto elapsed_mul = std::chrono::duration<float>(now - g_velocity_data.last_mul_reset).count();
		if (elapsed_mul >= velocity_data::mul_reset_interval) {
			if (g_velocity_data.jump_height <= 0.0f) {
				g_velocity_data.multiplicator = 1.0f;
			}
			g_velocity_data.last_mul_reset = now;
		}
		
		last_speed = speed_2d;
		
		g_velocity_data.velocity_history.push_back(speed_2d);
		if (g_velocity_data.velocity_history.size() > velocity_data::max_history_size) {
			g_velocity_data.velocity_history.pop_front();
		}

		if (speed_2d > g_velocity_data.max_velocity) {
			g_velocity_data.max_velocity = speed_2d;
		}

		const auto elapsed = std::chrono::duration<float>(now - g_velocity_data.last_max_reset).count();
		
		if (elapsed >= velocity_data::max_reset_interval) {
			g_velocity_data.max_velocity = speed_2d;
			g_velocity_data.last_max_reset = now;
		}
	}

	void velocity_graph::render() {
		const auto& cfg = settings::g_misc.m_hud.m_velocity;
		if (!cfg.graph.value)
			return;
			
		const auto local = systems::g_local.get();
		if (!local.is_alive || !local.pawn || g_velocity_data.velocity_history.empty())
			return;

		auto& dl = xdraw::get();

		const float graph_width = cfg.graph_width.value;
		const float graph_height = cfg.graph_height.value;
		const float line_width = cfg.graph_line_width.value;
		
		const auto [screen_w, screen_h] = xdraw::viewport_size();
		const float x = (static_cast<float>(screen_w) - graph_width) * 0.5f;
		const float y = static_cast<float>(screen_h) - graph_height - cfg.graph_bottom_offset.value;

		if (g_velocity_data.velocity_history.size() >= 2) {
			const float fixed_max_scale = 500.0f;
			
			auto line_color = xdraw::color{ 255, 255, 255, 255 };
			
			for (size_t i = 1; i < g_velocity_data.velocity_history.size(); ++i) {
				const float speed1 = g_velocity_data.velocity_history[i - 1];
				const float speed2 = g_velocity_data.velocity_history[i];
				
				const float clamped_speed1 = std::min(speed1, fixed_max_scale);
				const float clamped_speed2 = std::min(speed2, fixed_max_scale);
				
				const float norm1 = clamped_speed1 / fixed_max_scale;
				const float norm2 = clamped_speed2 / fixed_max_scale;
				
				const float x1 = x + ((i - 1) * graph_width) / static_cast<float>(velocity_data::max_history_size - 1);
				const float y1 = y + graph_height - (norm1 * graph_height);
				const float x2 = x + (i * graph_width) / static_cast<float>(velocity_data::max_history_size - 1);
				const float y2 = y + graph_height - (norm2 * graph_height);
				
				dl.line(x1, y1, x2, y2, line_color, line_width);
			}
		}
	}

	void velocity_graph::render_indicator() {
		const auto& cfg = settings::g_misc.m_hud.m_velocity;
		if (!cfg.indicator.value)
			return;
			
		const auto local = systems::g_local.get();
		if (!local.is_alive || !local.pawn)
			return;

		auto& dl = xdraw::get();
		const auto [screen_w, screen_h] = xdraw::viewport_size();
		
		const float current_speed = g_velocity_data.velocity_history.empty() ? 0.0f : g_velocity_data.velocity_history.back();
		const float takeoff_speed = g_velocity_data.max_velocity;
		
		static float alpha = 0.0f;
		const float target_alpha = (current_speed > 5.0f) ? 1.0f : 0.0f;
		const float dt = xdraw::delta_time();
		alpha += (target_alpha - alpha) * std::min(1.0f, 8.0f * dt);
		if (alpha < 0.01f) alpha = 0.0f;
		if (alpha > 0.99f) alpha = 1.0f;
		
		if (alpha <= 0.0f)
			return;
		
		char vel_text[64];
		if (takeoff_speed > 1.0f)
			std::snprintf(vel_text, sizeof(vel_text), "%.0f (%.0f)", current_speed, takeoff_speed);
		else
			std::snprintf(vel_text, sizeof(vel_text), "%.0f", current_speed);
		
		xdraw::push_font(rendering::g_fonts.sfpro_bold[rendering::fonts::size::xlarge]);
		
		const auto [text_w, text_h] = xdraw::measure_text(vel_text);
		const float padding = 10.0f;
		const float box_w = text_w + padding * 2.0f;
		const float box_h = text_h + padding * 2.0f;
		
		const float pos_x = (static_cast<float>(screen_w) - box_w) * 0.5f;
		const float pos_y = static_cast<float>(screen_h) - box_h - cfg.indicator_y.value * static_cast<float>(screen_h);
		
		const float text_x = pos_x + padding + std::floor((box_w - padding * 2.0f - text_w) * 0.5f);
		const float text_y = pos_y + padding + std::floor((box_h - padding * 2.0f - text_h) * 0.5f);
		
		auto color = cfg.color.value;
		const auto shadow_col = xdraw::color{ 0, 0, 0, static_cast<uint8_t>(180.0f * alpha) };
		const auto text_col = xdraw::color{ color.r, color.g, color.b, static_cast<uint8_t>(color.a * alpha) };
		
		dl.text(text_x + 1.0f, text_y + 1.0f, vel_text, shadow_col);
		dl.text(text_x, text_y, vel_text, text_col);
		
		xdraw::pop_font();
	}

} 