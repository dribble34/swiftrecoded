#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>

#include "../movement.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

	namespace {

		constexpr auto k_slip_vel_min{ -8.293f };
		constexpr auto k_slip_vel_max{ -5.629f };
		constexpr auto k_snap_tolerance{ 30.0f };
		constexpr auto k_rad_to_deg{ 57.29577951308232f };

	} // namespace

	void pixelsurf::on_create_move( systems::input::usercmd* cmd )
	{
		const auto& cfg = settings::g_movement.m_pixelsurf;

		if ( !cfg.enabled.value )
		{
			this->reset( );
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			this->reset( );
			return;
		}

		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			this->reset( );
			return;
		}

		const auto& pre = systems::g_prediction.pre( );
		const auto on_ground = ( pre.flags & cstypes::entity_flags::on_ground ) != 0;

		// Latch "slipping" while standing on a pixel ledge, then keep it alive
		// through the slow-fall window that follows leaving the ledge.
		const auto vz = pre.networked_velocity.z;
		if ( on_ground )
		{
			const auto edge = this->detect_pixel_edge( local.pawn, pre, cfg.ledge_units.value );
			this->m_is_slipping = edge.found;
			if ( edge.found )
			{
				this->m_edge_direction = edge.direction;
			}
		}
		else if ( this->m_is_slipping )
		{
			if ( vz < k_slip_vel_min || vz > k_slip_vel_max )
			{
				this->m_is_slipping = false;
			}
		}

		const auto active = this->m_is_slipping;
		const auto button_value = cfg.button_type.value;
		
		// button_type: 0 = jump, 1 = attack2, 2 = none
		const auto button = ( button_value == 0 )
			? cstypes::command_buttons::in_jump
			: ( button_value == 1 )
			? cstypes::command_buttons::in_second_attack
			: 0ull; // none

		if ( active && button != 0 )
		{
			cmd->buttons.value |= button;
			cmd->buttons.value_changed |= button;
			cmd->buttons.value_scroll |= button;

			this->m_edge_type = static_cast< std::uint8_t >( cfg.button_type.value );
			this->m_was_active = true;

			if ( cfg.angle_correction.value )
			{
				systems::g_input.set_view_angles( math::vector3{ 0.0f, this->compute_target_yaw( pre ), 0.0f } );
			}
		}
		else if ( this->m_was_active )
		{
			if ( button != 0 )
			{
				cmd->buttons.value &= ~button;
				cmd->buttons.value_changed &= ~button;
				cmd->buttons.value_scroll &= ~button;
			}

			this->m_edge_type = 0;
			this->m_edge_direction = 0;
			this->m_was_active = false;
		}
	}

	pixelsurf::edge_result pixelsurf::detect_pixel_edge( std::uintptr_t pawn, const systems::prediction::state& pre, float ledge_units ) const
	{
		edge_result out{};

		const auto movement_services = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return out;
		}

		const auto mins = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash ) + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
		const auto maxs = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash ) + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );

		auto trace_mask{ 0ull };
		{
			const auto pawn_ptr = memory::read<std::uintptr_t>( movement_services + 56 );
			trace_mask = memory::read<std::uintptr_t>( pawn_ptr + 0xd48 );

			if ( !pawn_ptr || ( memory::read<std::uint32_t>( pawn_ptr + 0x3f8 ) & 0x10 ) )
			{
				trace_mask |= 0x20;
			}
		}

		const auto filter = systems::g_tracing.make_player_movement_filter( pawn, trace_mask, 11 );
		const auto bbox = systems::tracing::bbox_collision{ mins, maxs };
		const auto sv_standable_normal = CONVAR( "sv_standable_normal" )->get<float>( );

		const auto& vel = pre.networked_velocity;
		const auto vel2d_len = std::sqrtf( vel.x * vel.x + vel.y * vel.y );

		float perp_x{ 1.0f };
		float perp_y{ 0.0f };
		if ( vel2d_len > 0.01f )
		{
			perp_x = -vel.y / vel2d_len;
			perp_y = vel.x / vel2d_len;
		}

		const auto probe = [ & ]( float ox, float oy ) -> bool
		{
			const auto start = math::vector3{ pre.networked_origin.x + ox, pre.networked_origin.y + oy, pre.networked_origin.z + 2.0f };
			const auto end = math::vector3{ pre.networked_origin.x + ox, pre.networked_origin.y + oy, pre.networked_origin.z - 16.0f };
			const auto r = systems::g_tracing.trace_player_bbox( start, end, bbox, filter, movement_services );
			return r.fraction > 0.0f && r.fraction < 1.0f && r.normal.z >= sv_standable_normal;
		};

		const auto half = std::max( std::fabsf( maxs.x ), std::fabsf( maxs.y ) );

		const auto center_ok = probe( 0.0f, 0.0f );
		if ( !center_ok )
		{
			return out;
		}

		const auto side_dist = half + 0.5f;
		const auto left_ok = probe( -perp_x * side_dist, -perp_y * side_dist );
		const auto right_ok = probe( perp_x * side_dist, perp_y * side_dist );

		if ( left_ok && !right_ok )
		{
			out.found = true;
			out.direction = 1; // ledge drops to the right
		}
		else if ( right_ok && !left_ok )
		{
			out.found = true;
			out.direction = 0; // ledge drops to the left
		}
		else if ( left_ok && right_ok )
		{
			// Both sides standable — probe tighter for a sub-hull "pixel" strip.
			const auto pixel_dist = std::min( half, std::max( 0.25f, ledge_units ) );
			const auto pixel_left = probe( -perp_x * pixel_dist, -perp_y * pixel_dist );
			const auto pixel_right = probe( perp_x * pixel_dist, perp_y * pixel_dist );

			if ( pixel_left && !pixel_right )
			{
				out.found = true;
				out.direction = 1;
			}
			else if ( pixel_right && !pixel_left )
			{
				out.found = true;
				out.direction = 0;
			}
		}

		return out;
	}

	float pixelsurf::compute_target_yaw( const systems::prediction::state& pre ) const
	{
		const auto& vel = pre.networked_velocity;
		const auto len = std::sqrtf( vel.x * vel.x + vel.y * vel.y );
		if ( len < 1.0f )
		{
			return 0.0f;
		}

		const auto heading = std::atan2f( vel.y, vel.x ) * k_rad_to_deg;
		const auto snapped = std::roundf( heading / 90.0f ) * 90.0f;

		auto diff = std::fabsf( heading - snapped );
		diff = std::min( diff, 360.0f - diff );

		const auto normalize = [ ]( float a ) -> float
		{
			a = std::fmodf( a + 180.0f, 360.0f );
			if ( a < 0.0f )
			{
				a += 360.0f;
			}
			return a - 180.0f;
		};

		if ( diff > k_snap_tolerance )
		{
			return normalize( heading );
		}

		return normalize( snapped );
	}

	void pixelsurf::reset( )
	{
		this->m_edge_type = 0;
		this->m_edge_direction = 0;
		this->m_is_slipping = false;
		this->m_was_active = false;
	}

} // namespace features::movement
