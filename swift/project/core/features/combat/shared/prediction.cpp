#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	void shared::lagcomp::predict_movement( extrapolation_data& data, std::uintptr_t skip_entity ) const
	{
		const auto sv_gravity = CONVAR( "sv_gravity" )->get<float>( );

		if ( data.flags & cstypes::entity_flags::on_ground )
		{
			data.velocity.z = 0.0f;
		}
		else
		{
			data.velocity.z -= sv_gravity * cstypes::tick_interval;
		}

		const auto move_end = data.origin + data.velocity * cstypes::tick_interval;

		auto trace_result = systems::g_tracing.trace_hull(
			data.origin, move_end,
			data.obb_mins, data.obb_maxs,
			skip_entity, 0x1c3003, 4
		);

		if ( trace_result.fraction != 1.0f )
		{
			for ( auto i = 0; i < 4; ++i )
			{
				const auto dot = data.velocity.dot( trace_result.normal );
				data.velocity -= trace_result.normal * dot;

				const auto adjust = data.velocity.dot( trace_result.normal );
				if ( adjust < 0.0f )
				{
					data.velocity -= trace_result.normal * adjust;
				}

				const auto remaining_fraction = 1.0f - trace_result.fraction;
				const auto clip_end = trace_result.end_pos + data.velocity * ( cstypes::tick_interval * remaining_fraction );

				trace_result = systems::g_tracing.trace_hull(
					trace_result.end_pos, clip_end,
					data.obb_mins, data.obb_maxs,
					skip_entity, 0x1c3003, 4
				);

				if ( trace_result.fraction == 1.0f )
				{
					break;
				}
			}
		}

		data.origin = trace_result.end_pos;

		const auto ground_end = math::vector3{ data.origin.x, data.origin.y, data.origin.z - 2.0f };
		const auto ground_trace = systems::g_tracing.trace_hull(
			data.origin, ground_end,
			data.obb_mins, data.obb_maxs,
			skip_entity, 0x1c3003, 4
		);

		data.flags &= ~cstypes::entity_flags::on_ground;

		if ( ground_trace.fraction != 1.0f && ground_trace.normal.z > 0.7f )
		{
			data.flags |= cstypes::entity_flags::on_ground;
		}
	}

	std::optional<shared::lagcomp::record> shared::lagcomp::extrapolate( std::uintptr_t pawn )
	{
		if ( !settings::g_combat.m_lagcomp.extrapolation.value )
		{
			return std::nullopt;
		}

		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return std::nullopt;
		}

		const auto& latest = it->second.front( );
		if ( !latest.is_valid( ) )
		{
			return std::nullopt;
		}

		const auto ticks_to_extrapolate = settings::g_combat.m_lagcomp.max_extrapolate_ticks.value;
		if ( ticks_to_extrapolate <= 0 )
		{
			return std::nullopt;
		}

		auto velocity = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );

		if ( it->second.size( ) > 1 )
		{
			const auto& prev = it->second[ 1 ];
			if ( prev.valid && latest.simulation_time > prev.simulation_time )
			{
				const auto dt = latest.simulation_time - prev.simulation_time;
				const auto recorded_vel = ( latest.origin - prev.origin ) / dt;

				const auto rec_speed = recorded_vel.length_2d( );
				const auto net_speed = velocity.length_2d( );

				// recorded velocity is smoother, blend by agreement
				if ( rec_speed > 0.1f && net_speed > 0.1f )
				{
					const auto dir_dot = ( recorded_vel / rec_speed ).dot( velocity / net_speed );
					if ( dir_dot > 0.0f )
					{
						velocity = recorded_vel * std::clamp( dir_dot, 0.35f, 1.0f ) + velocity * ( 1.0f - std::clamp( dir_dot, 0.35f, 1.0f ) );
					}
				}
				else if ( rec_speed > 0.1f )
				{
					velocity = recorded_vel;
				}
			}
		}

		const auto speed = std::sqrtf( velocity.x * velocity.x + velocity.y * velocity.y );

		if ( speed < 0.1f )
		{
			return std::nullopt;
		}

		// acceleration estimate from the last two record intervals — lets the
		// extrapolation track targets that are speeding up or slowing down
		math::vector3 acceleration{};
		if ( it->second.size( ) > 2 )
		{
			const auto& r1 = it->second[ 1 ];
			const auto& r2 = it->second[ 2 ];
			if ( r1.valid && r2.valid && r1.simulation_time > r2.simulation_time )
			{
				const auto v1 = ( r1.origin - r2.origin ) / ( r1.simulation_time - r2.simulation_time );
				const auto v2 = ( latest.origin - r1.origin ) / ( latest.simulation_time - r1.simulation_time );

				const auto dt2 = latest.simulation_time - r1.simulation_time;
				if ( dt2 > 0.0f )
				{
					acceleration = ( v2 - v1 ) / dt2;

					// trust it briefly, it decays every tick below
					constexpr auto k_max_accel{ 600.0f };
					if ( acceleration.length( ) > k_max_accel )
					{
						acceleration *= k_max_accel / acceleration.length( );
					}
				}
			}
		}

		float direction = 0.0f;
		if ( velocity.x != 0.0f || velocity.y != 0.0f )
		{
			direction = std::atan2f( velocity.y, velocity.x ) * ( 180.0f / 3.14159265f );
		}

		float direction_change = 0.0f;
		auto direction_samples{ 0 };

		for ( std::size_t si = 1; si < it->second.size( ) && direction_samples < 2; ++si )
		{
			const auto& prev = it->second[ si ];
			if ( !prev.valid )
			{
				continue;
			}

			const auto dt = latest.simulation_time - prev.simulation_time;
			if ( dt <= 0.0f )
			{
				continue;
			}

			const auto origin_delta = latest.origin - prev.origin;
			if ( origin_delta.length_2d( ) < 0.5f )
			{
				continue;
			}

			float prev_dir = direction;
			if ( origin_delta.x != 0.0f || origin_delta.y != 0.0f )
			{
				prev_dir = std::atan2f( origin_delta.y, origin_delta.x ) * ( 180.0f / 3.14159265f );
			}

			auto angle_diff = direction - prev_dir;
			while ( angle_diff > 180.0f ) angle_diff -= 360.0f;
			while ( angle_diff < -180.0f ) angle_diff += 360.0f;

			// sharp turn = unreliable, bail
			if ( std::fabsf( angle_diff ) > 35.0f )
			{
				return std::nullopt;
			}

			direction_change += ( angle_diff / dt ) * cstypes::tick_interval;
			++direction_samples;
		}

		if ( direction_samples > 0 )
		{
			direction_change /= static_cast< float >( direction_samples );
		}

		if ( std::fabsf( direction_change ) > 6.0f )
		{
			direction_change = 0.0f;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return std::nullopt;
		}

		const auto collision = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pCollision"_hash ) );
		math::vector3 obb_mins{}, obb_maxs{};

		if ( collision )
		{
			obb_mins = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
			obb_maxs = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );
		}
		else
		{
			obb_mins = { -16.0f, -16.0f, 0.0f };
			obb_maxs = { 16.0f, 16.0f, 72.0f };
		}

		const auto flags = memory::read<std::uint32_t>( pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );

		extrapolation_data data{};
		data.origin = latest.origin;
		data.velocity = velocity;
		data.obb_mins = obb_mins;
		data.obb_maxs = obb_maxs;
		data.flags = flags;
		data.sim_time = latest.simulation_time;
		data.direction = direction;

		for ( auto i = 0; i < ticks_to_extrapolate; ++i )
		{
			data.direction += direction_change;
			while ( data.direction > 180.0f ) data.direction -= 360.0f;
			while ( data.direction < -180.0f ) data.direction += 360.0f;

			const auto rad = data.direction * ( 3.14159265f / 180.0f );

			// accel nudges the planar velocity, decayed — low confidence fast
			data.velocity.x += acceleration.x * cstypes::tick_interval;
			data.velocity.y += acceleration.y * cstypes::tick_interval;
			acceleration *= 0.85f;

			const auto current_speed = std::sqrtf( data.velocity.x * data.velocity.x + data.velocity.y * data.velocity.y );

			data.velocity.x = std::cosf( rad ) * current_speed;
			data.velocity.y = std::sinf( rad ) * current_speed;

			data.sim_time += cstypes::tick_interval;

			this->predict_movement( data, pawn );
		}

		const auto origin_delta = data.origin - latest.origin;

		// too far = garbage, don't trust it
		if ( origin_delta.length_sqr( ) < 0.01f || origin_delta.length_2d( ) > 128.0f )
		{
			return std::nullopt;
		}

		record extrap_record = latest;
		extrap_record.origin = data.origin;
		extrap_record.simulation_time = data.sim_time;
		extrap_record.tick = cstypes::time_to_ticks( data.sim_time );
		extrap_record.extrapolated = true;

		for ( auto i = 0; i < extrap_record.bone_count && i < 128; ++i )
		{
			extrap_record.bones[ i ].position.x += origin_delta.x;
			extrap_record.bones[ i ].position.y += origin_delta.y;
			extrap_record.bones[ i ].position.z += origin_delta.z;
		}

		return extrap_record;
	}

	void shared::shoot_history::snapshot( std::uintptr_t local_pawn, std::uintptr_t weapon_services )
	{
		this->m_count = 0;

		if ( !weapon_services )
		{
			return;
		}

		{
			const auto net_client = addresses::globals::network_client_service;
			if ( !net_client )
			{
				return;
			}

			const auto tick_state = memory::call_vfunc<std::uintptr_t>( net_client, 23 );
			if ( !tick_state )
			{
				return;
			}

			this->m_server_tick = memory::read<int>( tick_state + 892 );
		}

		{
			const auto idx_raw = memory::read<int>( addresses::globals::frame_input_ring_idx );
			const auto idx = static_cast< unsigned >( idx_raw ) % 10u;
			const auto slot = addresses::globals::frame_input_ring_base + 40ull * idx;

			this->m_client_tick = memory::read<int>( slot + 0x0c );
			this->m_client_tick_frac = memory::read<float>( slot + 0x10 );
		}

		{
			const auto lerp_seconds = memory::call<float>( PATTERN( patterns::get_interp_amount ), local_pawn );
			const auto lerp_ticks_f = lerp_seconds * 64.0f;
			const auto rounded = std::round( lerp_ticks_f );

			if ( std::fabs( lerp_ticks_f - rounded ) < 1e-4f )
			{
				this->m_lerp_ticks_int = static_cast< int >( rounded );
				this->m_lerp_ticks_frac = 0.0f;
			}
			else
			{
				this->m_lerp_ticks_int = static_cast< int >( std::floor( lerp_ticks_f ) );
				this->m_lerp_ticks_frac = lerp_ticks_f - static_cast< float >( this->m_lerp_ticks_int );
			}
		}

		const auto tail = memory::read<int>( weapon_services + 872 );
		const auto count = memory::read<int>( weapon_services + 876 );

		if ( count <= 0 || count > 32 || tail < 0 || tail >= 32 )
		{
			return;
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto idx = ( tail + i ) % 32;
			const auto off = weapon_services + 232 + 20ull * idx;

			auto& e = this->m_entries[ i ];
			e.tick = memory::read<int>( off + 0x00 );
			e.fraction = memory::read<float>( off + 0x04 );
			e.position.x = memory::read<float>( off + 0x08 );
			e.position.y = memory::read<float>( off + 0x0C );
			e.position.z = memory::read<float>( off + 0x10 );
		}

		this->m_count = count;
	}

	shared::shoot_history::eye_candidates shared::shoot_history::get_candidates( ) const
	{
		eye_candidates out{};

		if ( this->m_count < 1 )
		{
			return out;
		}

		constexpr auto ring_slot{ 0.03125f };

		const auto newest_valid_tick = this->m_client_tick - this->m_lerp_ticks_int;
		const auto oldest_valid_tick = this->m_client_tick - this->m_lerp_ticks_int - 1;

		auto first_valid{ -1 };
		auto last_valid{ -1 };

		for ( auto i = 0; i < this->m_count; ++i )
		{
			const auto t = this->m_entries[ i ].tick;
			if ( t > newest_valid_tick || t < oldest_valid_tick )
			{
				continue;
			}

			if ( first_valid == -1 )
			{
				first_valid = i;
			}

			last_valid = i;
		}

		if ( last_valid == -1 )
		{
			return out;
		}

		const auto& newest = this->m_entries[ last_valid ];
		out.entries[ 0 ].position = newest.position;
		out.entries[ 0 ].player_tick = newest.tick;
		out.entries[ 0 ].player_frac = newest.fraction + ring_slot;
		out.entries[ 0 ].lerp_ticks_int = this->m_lerp_ticks_int;
		out.entries[ 0 ].lerp_ticks_frac = this->m_lerp_ticks_frac;
		out.count = 1;

		if ( first_valid != last_valid )
		{
			const auto& oldest = this->m_entries[ first_valid ];
			const auto delta = oldest.position - newest.position;

			if ( delta.x * delta.x + delta.y * delta.y + delta.z * delta.z >= 4.0f )
			{
				out.entries[ 1 ].position = oldest.position;
				out.entries[ 1 ].player_tick = oldest.tick;
				out.entries[ 1 ].player_frac = oldest.fraction + ring_slot;
				out.entries[ 1 ].lerp_ticks_int = this->m_lerp_ticks_int;
				out.entries[ 1 ].lerp_ticks_frac = this->m_lerp_ticks_frac;
				out.count = 2;
			}
		}

		return out;
	}

}
