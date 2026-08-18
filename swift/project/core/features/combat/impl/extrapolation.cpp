#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	void shared::lagcomp::predict_movement( extrapolation_data& data, std::uintptr_t skip_entity ) const
	{
		if ( !addresses::globals::game_trace_manager )
		{
			return;
		}

		const auto sv_gravity = CONVAR ("sv_gravity")->get<float>( );

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
			for ( auto i = 0; i < 2; ++i )
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

		// The final trace may start at a collision point. Its end position is the
		// clipped destination even when that follow-up trace completes fully.
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

		const auto net_client = addresses::globals::network_client_service;
		if ( !net_client )
		{
			return std::nullopt;
		}

		const auto tick_state = memory::call_vfunc<std::uintptr_t>( net_client, 23 );
		if ( !tick_state )
		{
			return std::nullopt;
		}

		const auto server_tick = memory::read<int>( tick_state + 892 );
		const auto delta_ticks = server_tick - latest.tick;

		if ( delta_ticks <= 0 )
		{
			return std::nullopt;
		}

		const auto max_extrap = settings::g_combat.m_lagcomp.max_extrapolate_ticks.value;
		if ( delta_ticks > max_extrap )
		{
			return std::nullopt;
		}

		const auto ticks_to_extrapolate = delta_ticks;

		const auto velocity = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
		const auto speed = std::sqrtf( velocity.x * velocity.x + velocity.y * velocity.y );

		if ( speed < 0.1f )
		{
			return std::nullopt;
		}

		float direction = 0.0f;
		if ( velocity.x != 0.0f || velocity.y != 0.0f )
		{
			direction = std::atan2f( velocity.y, velocity.x ) * ( 180.0f / 3.14159265f );
		}

		float direction_change = 0.0f;

		if ( it->second.size( ) > 1 )
		{
			const auto& prev = it->second[ 1 ];
			if ( prev.valid )
			{
				const auto dt = latest.simulation_time - prev.simulation_time;
				if ( dt > 0.0f )
				{
					const auto origin_delta = latest.origin - prev.origin;
					float prev_dir = 0.0f;

					if ( origin_delta.x != 0.0f || origin_delta.y != 0.0f )
					{
						prev_dir = std::atan2f( origin_delta.y, origin_delta.x ) * ( 180.0f / 3.14159265f );
					}

				auto angle_diff = direction - prev_dir;
				while ( angle_diff > 180.0f ) angle_diff -= 360.0f;
				while ( angle_diff < -180.0f ) angle_diff += 360.0f;

				if ( std::fabsf( angle_diff ) > 35.0f )
				{
					return std::nullopt;
				}

				direction_change = ( angle_diff / dt ) * cstypes::tick_interval;
				}
			}
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
			const auto current_speed = std::sqrtf( data.velocity.x * data.velocity.x + data.velocity.y * data.velocity.y );
			data.velocity.x = std::cosf( rad ) * current_speed;
			data.velocity.y = std::sinf( rad ) * current_speed;

			data.sim_time += cstypes::tick_interval;

			this->predict_movement( data, pawn );
		}

		const auto origin_delta = data.origin - latest.origin;

		if ( origin_delta.length_sqr( ) < 0.01f )
		{
			return std::nullopt;
		}

		record extrap_record = latest;
		extrap_record.origin = data.origin;
		extrap_record.simulation_time = data.sim_time;
		extrap_record.tick = cstypes::time_to_ticks( data.sim_time );
		extrap_record.extrapolated = true;

		// The extrapolated body also turns while moving. Rotate the translated pose
		// about the predicted origin by the accumulated yaw so the lead pose faces
		// the direction of travel instead of staying frozen in the last orientation.
		const auto total_yaw = direction_change * static_cast< float >( ticks_to_extrapolate );
		const auto rotate_pose = std::fabsf( total_yaw ) > 0.5f;
		const auto yaw_rad = math::helpers::deg_to_rad( total_yaw );
		const auto cos_yaw = std::cosf( yaw_rad );
		const auto sin_yaw = std::sinf( yaw_rad );
		const auto pivot = data.origin;

		for ( auto i = 0; i < extrap_record.bone_count && i < 128; ++i )
		{
			auto& pos = extrap_record.bones[ i ].position;
			pos.x += origin_delta.x;
			pos.y += origin_delta.y;
			pos.z += origin_delta.z;

			if ( rotate_pose )
			{
				const auto dx = pos.x - pivot.x;
				const auto dz = pos.z - pivot.z;
				pos.x = pivot.x + dx * cos_yaw - dz * sin_yaw;
				pos.z = pivot.z + dx * sin_yaw + dz * cos_yaw;
			}
		}

		return extrap_record;
	}

} // namespace features::combat
