#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	namespace
	{
		constexpr auto k_min_history_velocity{ 0.05f };
		constexpr auto k_max_history_velocity{ 520.0f };
		constexpr auto k_max_record_dt{ 0.30f };
		constexpr auto k_max_step_per_tick{ 52.0f };
		constexpr auto k_max_fall_speed{ 3500.0f };
		constexpr auto k_max_turn_rate{ 28.0f };
		constexpr auto k_max_accel{ 64.0f };
		constexpr auto k_gravity_default{ 800.0f };

		float get_gravity()
		{
			const auto v = CONVAR("sv_gravity")->get<float>();
			if ( !std::isfinite(v) || v < 100.0f || v > 2000.0f )
				return k_gravity_default;
			return v;
		}

		float get_friction()
		{
			const auto v = CONVAR("sv_friction")->get<float>();
			if ( !std::isfinite(v) || v < 1.0f || v > 20.0f )
				return 5.2f;
			return v;
		}

		float get_stopspeed()
		{
			const auto v = CONVAR("sv_stopspeed")->get<float>();
			if ( !std::isfinite(v) || v < 20.0f || v > 500.0f )
				return 80.0f;
			return v;
		}

		float get_accelerate()
		{
			const auto v = CONVAR("sv_accelerate")->get<float>();
			if ( !std::isfinite(v) || v < 1.0f || v > 20.0f )
				return 5.5f;
			return v;
		}

		float get_airaccel()
		{
			const auto v = CONVAR("sv_airaccelerate")->get<float>();
			if ( !std::isfinite(v) || v < 1.0f || v > 100.0f )
				return 12.0f;
			return v;
		}

		math::vector3 clamp_horiz( const math::vector3& v, float limit )
		{
			const float l = std::sqrtf(v.x*v.x+v.y*v.y);
			if ( l <= limit || l < 0.01f )
				return v;
			const float s = limit / l;
			return { v.x*s, v.y*s, v.z };
		}

		bool is_valid_move( const math::vector3& org )
		{
			if ( !std::isfinite(org.x) || !std::isfinite(org.y) || !std::isfinite(org.z) )
				return false;
			if ( org.length_sqr() > 1e12f )
				return false;
			return true;
		}
	}

	void shared::lagcomp::predict_movement( extrapolation_data& data, std::uintptr_t skip_entity ) const
	{
		const auto sv_gravity = get_gravity();
		const auto sv_friction = get_friction();
		const auto sv_stopspeed = get_stopspeed();
		const auto sv_accelerate = get_accelerate();
		const auto sv_airaccel = get_airaccel();
		const auto on_ground = ( data.flags & cstypes::entity_flags::on_ground ) != 0;
		const auto ducking = ( data.flags & cstypes::entity_flags::ducking ) != 0;

		if ( data.turn_rate != 0.0f )
		{
			const float ang = data.turn_rate * cstypes::tick_interval;
			const float c = std::cosf(ang);
			const float s = std::sinf(ang);
			const float vx = data.velocity.x * c - data.velocity.y * s;
			const float vy = data.velocity.x * s + data.velocity.y * c;
			data.velocity.x = vx;
			data.velocity.y = vy;
			if ( on_ground && std::fabsf(data.turn_rate) > 6.0f )
			{
				const float speed = std::sqrtf(data.velocity.x*data.velocity.x + data.velocity.y*data.velocity.y);
				if ( speed > 5.0f )
				{
					const float loss = std::clamp(std::fabsf(data.turn_rate)*0.0018f, 0.0f, 0.025f);
					data.velocity.x *= (1.0f - loss);
					data.velocity.y *= (1.0f - loss);
				}
			}
		}

		if ( data.acceleration.x != 0.0f || data.acceleration.y != 0.0f )
		{
			auto accel = data.acceleration;
			float dot = accel.dot(data.velocity);
			if ( on_ground && dot < 0.0f )
				accel *= 2.15f;
			else if ( !on_ground && dot < 0.0f )
				accel *= 1.35f;
			if ( ducking && on_ground )
				accel *= 0.88f;
			float alen = std::sqrtf(accel.x*accel.x + accel.y*accel.y);
			if ( alen > k_max_accel )
			{
				float sc = k_max_accel / alen;
				accel.x *= sc;
				accel.y *= sc;
			}
			data.velocity.x += accel.x;
			data.velocity.y += accel.y;
		}

		float speed2d = std::sqrtf(data.velocity.x*data.velocity.x + data.velocity.y*data.velocity.y);
		if ( on_ground && speed2d > 0.5f )
		{
			const float control = std::fmaxf(speed2d, sv_stopspeed);
			const float drop = sv_friction * 1.0f * control * cstypes::tick_interval;
			float newspeed = std::fmaxf(speed2d - drop, 0.0f);
			if ( newspeed != speed2d )
			{
				newspeed /= speed2d;
				data.velocity.x *= newspeed;
				data.velocity.y *= newspeed;
				speed2d *= newspeed;
			}
			if ( speed2d < data.max_speed )
			{
				float wish = sv_accelerate * cstypes::tick_interval * data.max_speed;
				wish = std::min(wish, data.max_speed - speed2d);
				if ( wish > 0.0f && speed2d > 1.0f )
				{
					float dirx = data.velocity.x / speed2d;
					float diry = data.velocity.y / speed2d;
					data.velocity.x += dirx * wish * 0.12f;
					data.velocity.y += diry * wish * 0.12f;
				}
			}
		}
		else if ( !on_ground && speed2d > data.max_speed * 0.4f )
		{
			float drag = std::clamp(sv_airaccel * 0.007f, 0.0f, 0.018f);
			data.velocity.x *= (1.0f - drag);
			data.velocity.y *= (1.0f - drag);
		}

		speed2d = std::sqrtf(data.velocity.x*data.velocity.x + data.velocity.y*data.velocity.y);
		if ( speed2d > data.max_speed )
		{
			float sc = data.max_speed / speed2d;
			data.velocity.x *= sc;
			data.velocity.y *= sc;
		}
		if ( speed2d < 0.12f && on_ground )
		{
			data.velocity.x = 0.0f;
			data.velocity.y = 0.0f;
		}

		if ( on_ground )
		{
			if ( data.velocity.z < 0.0f )
				data.velocity.z = 0.0f;
			if ( data.velocity.z > 320.0f )
				data.velocity.z = std::min(data.velocity.z, 320.0f);
		}
		else
		{
			data.velocity.z -= sv_gravity * cstypes::tick_interval;
			if ( data.velocity.z < -k_max_fall_speed )
				data.velocity.z = -k_max_fall_speed;
			if ( data.velocity.z > k_max_fall_speed )
				data.velocity.z = k_max_fall_speed;
		}

		if ( ducking && on_ground )
		{
			if ( data.obb_maxs.z > 54.5f )
				data.obb_maxs.z = 54.0f;
		}
		else
		{
			if ( data.obb_maxs.z < 70.0f )
				data.obb_maxs.z = 72.0f;
		}

		if ( !is_valid_move(data.origin) )
		{
			data.velocity = {};
			return;
		}

		const auto move_end = data.origin + data.velocity * cstypes::tick_interval;
		auto trace_result = systems::g_tracing.trace_hull(
			data.origin, move_end,
			data.obb_mins, data.obb_maxs,
			skip_entity, 0x1c3003, 4
		);

		int bumps = 0;
		for ( bumps = 0; bumps < 4 && trace_result.fraction < 1.0f; ++bumps )
		{
			float dot = data.velocity.dot(trace_result.normal);
			if ( dot < 0.0f )
				data.velocity -= trace_result.normal * dot;
			float adj = data.velocity.dot(trace_result.normal);
			if ( adj < 0.0f )
				data.velocity -= trace_result.normal * adj;
			if ( trace_result.normal.z > 0.7f )
			{
				if ( data.velocity.z < 0.0f )
					data.velocity.z = 0.0f;
				float horiz = std::sqrtf(data.velocity.x*data.velocity.x + data.velocity.y*data.velocity.y);
				if ( horiz < 4.0f )
				{
					data.velocity.x = 0.0f;
					data.velocity.y = 0.0f;
				}
			}
			if ( trace_result.normal.z < 0.25f && std::fabsf(trace_result.normal.z) < 0.08f )
			{
				float walldot = std::fabsf(trace_result.normal.x*data.velocity.x + trace_result.normal.y*data.velocity.y);
				if ( walldot > 0.5f )
				{
					data.velocity.x *= 0.985f;
					data.velocity.y *= 0.985f;
				}
			}
			float remaining = 1.0f - trace_result.fraction;
			if ( remaining <= 0.001f )
				break;
			const auto clip_end = trace_result.end_pos + data.velocity * (cstypes::tick_interval * remaining);
			auto next_trace = systems::g_tracing.trace_hull(
				trace_result.end_pos, clip_end,
				data.obb_mins, data.obb_maxs,
				skip_entity, 0x1c3003, 4
			);
			if ( next_trace.fraction < 0.001f && next_trace.normal.length_sqr() < 0.001f )
				break;
			trace_result = next_trace;
		}

		data.origin = trace_result.end_pos;
		if ( trace_result.fraction < 1.0f && bumps == 4 )
		{
			float vlen = data.velocity.length();
			if ( vlen > 12.0f )
				data.velocity *= 0.6f;
		}

		data.flags &= ~cstypes::entity_flags::on_ground;
		{
			const auto ground_end = math::vector3{ data.origin.x, data.origin.y, data.origin.z - 2.4f };
			const auto ground_trace = systems::g_tracing.trace_hull(
				data.origin, ground_end,
				data.obb_mins, data.obb_maxs,
				skip_entity, 0x1c3003, 4
			);
			if ( ground_trace.fraction != 1.0f && ground_trace.normal.z > 0.7f && ground_trace.fraction < 1.0f )
			{
				data.flags |= cstypes::entity_flags::on_ground;
				if ( data.velocity.z < 0.0f )
					data.velocity.z = 0.0f;
				float gdist = data.origin.z - ground_trace.end_pos.z;
				if ( gdist < 0.6f && gdist > -0.6f )
					data.origin.z = ground_trace.end_pos.z;
			}
			else
			{
				const auto fall_end = math::vector3{ data.origin.x, data.origin.y, data.origin.z - 20.0f };
				const auto fall_trace = systems::g_tracing.trace_hull(
					data.origin, fall_end,
					data.obb_mins, data.obb_maxs,
					skip_entity, 0x1c3003, 4
				);
				if ( fall_trace.fraction == 1.0f )
				{
					data.flags &= ~cstypes::entity_flags::on_ground;
				}
				else if ( fall_trace.normal.z > 0.6f )
				{
					if ( data.velocity.z < -180.0f && fall_trace.fraction < 0.45f )
						data.velocity.z *= 0.35f;
				}
			}
		}
		if ( !is_valid_move(data.origin) )
			data.velocity = {};
	}

	std::optional<shared::lagcomp::record> shared::lagcomp::extrapolate( std::uintptr_t pawn )
	{
		if ( !settings::g_combat.m_lagcomp.extrapolation.value )
			return std::nullopt;

		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.size( ) < 2 )
			return std::nullopt;

		const auto& latest = it->second.front( );
		if ( !latest.is_valid( ) )
		{
			if ( !latest.valid )
				return std::nullopt;
			const auto global_vars = memory::read<std::uintptr_t>(addresses::globals::global_vars);
			if ( global_vars )
			{
				float cur = memory::read<float>(global_vars + 0x30);
				if ( std::fabsf(cur - latest.simulation_time) > 0.25f )
					return std::nullopt;
			}
		}

		const auto net_client = addresses::globals::network_client_service;
		if ( !net_client )
			return std::nullopt;

		const auto tick_state = memory::call_vfunc<std::uintptr_t>( net_client, 23 );
		if ( !tick_state )
			return std::nullopt;

		const auto server_tick = memory::read<int>( tick_state + 892 );
		auto delta_ticks = server_tick - latest.tick;
		if ( delta_ticks <= 0 )
		{
			const auto gv = memory::read<std::uintptr_t>(addresses::globals::global_vars);
			if ( gv )
			{
				int cur = memory::read<int>(gv + 0x44);
				delta_ticks = cur - latest.tick + 1;
				if ( delta_ticks <= 0 )
					return std::nullopt;
			}
			else
				return std::nullopt;
		}

		auto max_extrap = settings::g_combat.m_lagcomp.max_extrapolate_ticks.value;
		max_extrap = std::clamp(max_extrap, 1, 64);
		if ( delta_ticks > max_extrap )
			delta_ticks = max_extrap;
		if ( delta_ticks > 64 )
			delta_ticks = 64;
		if ( delta_ticks <= 0 )
			return std::nullopt;

		const auto& prev = it->second[ 1 ];
		if ( !prev.valid )
			return std::nullopt;

		const auto dt = latest.simulation_time - prev.simulation_time;
		if ( dt <= 0.0001f || dt > k_max_record_dt )
			return std::nullopt;

		const auto moved = latest.origin - prev.origin;
		const float moved_len = moved.length( );
		const float max_step = k_max_step_per_tick * ( dt / cstypes::tick_interval ) * 1.35f;
		if ( moved_len > max_step )
		{
			if ( moved_len > max_step * 1.9f )
				return std::nullopt;
		}

		const auto base_vel = moved / dt;
		const float hist_speed = base_vel.length_2d( );

		if ( hist_speed < k_min_history_velocity )
		{
			const auto live_tmp = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
			if ( live_tmp.length_2d() < 1.2f )
				return std::nullopt;
		}
		if ( hist_speed > k_max_history_velocity )
			return std::nullopt;

		math::vector3 accel_tick{};
		math::vector3 prev_vel{};
		bool has_prev_vel{ false };
		float avg_speed = hist_speed;
		int samples = 1;

		if ( it->second.size( ) >= 3 )
		{
			const auto& older = it->second[ 2 ];
			if ( older.valid )
			{
				const float dt_old = prev.simulation_time - older.simulation_time;
				if ( dt_old > 0.0001f && dt_old <= k_max_record_dt )
				{
					prev_vel = ( prev.origin - older.origin ) / dt_old;
					has_prev_vel = true;
					accel_tick = ( base_vel - prev_vel ) / dt * cstypes::tick_interval;
					avg_speed += prev_vel.length_2d();
					samples++;
				}
			}
		}

		if ( it->second.size( ) >= 4 )
		{
			const auto& oldest = it->second[ 3 ];
			if ( oldest.valid && has_prev_vel )
			{
				float dt_a = it->second[2].simulation_time - oldest.simulation_time;
				if ( dt_a > 0.0001f && dt_a <= k_max_record_dt )
				{
					auto ancient = ( it->second[2].origin - oldest.origin ) / dt_a;
					avg_speed += ancient.length_2d();
					samples++;
					auto accel2 = ( prev_vel - ancient ) / dt * cstypes::tick_interval;
					accel_tick = ( accel_tick + accel2 ) * 0.5f;
				}
			}
		}
		avg_speed /= (float)samples;

		float turn_rate{ 0.0f };
		if ( has_prev_vel && hist_speed > 1.0f )
		{
			float prev_speed = prev_vel.length_2d( );
			if ( prev_speed > 1.0f )
			{
				float cross = base_vel.x * prev_vel.y - base_vel.y * prev_vel.x;
				float dot = base_vel.x * prev_vel.x + base_vel.y * prev_vel.y;
				float ang = std::atan2f( cross, dot );
				turn_rate = std::clamp( ang / dt, -k_max_turn_rate, k_max_turn_rate );
				if ( std::fabsf(ang) > 1.15f )
					turn_rate *= 0.55f;
				float ratio = std::min(hist_speed, prev_speed) / std::max(hist_speed, prev_speed);
				if ( ratio < 0.55f )
					turn_rate *= ratio;
			}
		}

		const auto live_vel = memory::read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
		const float live_speed2d = live_vel.length_2d( );
		if ( live_speed2d > 1.0f && hist_speed > 1.0f )
		{
			float dot = base_vel.dot( live_vel ) / ( hist_speed * live_speed2d );
			if ( dot < -0.68f && hist_speed > 45.0f )
				return std::nullopt;
			if ( dot < -0.42f && hist_speed > 140.0f )
			{
				accel_tick *= 0.55f;
				turn_rate *= 0.55f;
			}
		}

		float max_speed = 260.0f;
		const auto ms = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( ms )
		{
			float v = memory::read<float>( ms + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );
			if ( std::isfinite(v) && v > 10.0f && v < 600.0f )
				max_speed = v;
		}
		max_speed = std::clamp( std::max({hist_speed, live_speed2d, avg_speed}) * 1.32f + 12.0f, 80.0f, 600.0f );
		if ( !std::isfinite(max_speed) )
			max_speed = 310.0f;

		float accel_limit = std::clamp(max_speed * 0.22f, 12.0f, k_max_accel);
		accel_tick.x = std::clamp(accel_tick.x, -accel_limit, accel_limit);
		accel_tick.y = std::clamp(accel_tick.y, -accel_limit, accel_limit);
		if ( std::fabsf(accel_tick.x) < 0.015f ) accel_tick.x = 0.0f;
		if ( std::fabsf(accel_tick.y) < 0.015f ) accel_tick.y = 0.0f;
		if ( std::fabsf(turn_rate) < 0.012f ) turn_rate = 0.0f;

		const auto gsn = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !gsn )
			return std::nullopt;
		const auto collision = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pCollision"_hash ) );
		math::vector3 obb_mins{ -16.0f, -16.0f, 0.0f };
		math::vector3 obb_maxs{ 16.0f, 16.0f, 72.0f };
		if ( collision )
		{
			auto mins = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
			auto maxs = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );
			if ( std::isfinite(mins.x) && std::isfinite(maxs.x) )
			{
				obb_mins = mins;
				obb_maxs = maxs;
				obb_mins.x = std::clamp(obb_mins.x, -32.0f, 0.0f);
				obb_mins.y = std::clamp(obb_mins.y, -32.0f, 0.0f);
				obb_maxs.x = std::clamp(obb_maxs.x, 0.0f, 32.0f);
				obb_maxs.y = std::clamp(obb_maxs.y, 0.0f, 32.0f);
				obb_maxs.z = std::clamp(obb_maxs.z, 36.0f, 82.0f);
			}
		}
		auto flags = memory::read<std::uint32_t>( pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );
		const auto eflags = memory::read<std::uint32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ) );
		if ( (eflags & 0x1000) != 0 )
			flags &= ~cstypes::entity_flags::on_ground;
		if ( ( flags & cstypes::entity_flags::on_ground ) && live_vel.z > 110.0f )
			flags &= ~cstypes::entity_flags::on_ground;
		if ( latest.origin.z - prev.origin.z > 2.2f && hist_speed > 12.0f )
			flags &= ~cstypes::entity_flags::on_ground;

		extrapolation_data data{};
		data.origin = latest.origin;
		data.velocity = base_vel;
		data.velocity.z = live_vel.z;
		if ( !std::isfinite(data.velocity.z) )
			data.velocity.z = 0.0f;
		if ( std::fabsf(data.velocity.z) < 1.2f && !(flags & cstypes::entity_flags::on_ground) )
		{
			if ( hist_speed > 18.0f )
				data.velocity.z = -1.5f;
		}
		data.acceleration = accel_tick;
		data.turn_rate = turn_rate;
		data.obb_mins = obb_mins;
		data.obb_maxs = obb_maxs;
		data.flags = flags;
		data.sim_time = latest.simulation_time;
		data.max_speed = max_speed;

		for ( auto i = 0; i < delta_ticks; ++i )
		{
			data.sim_time += cstypes::tick_interval;
			this->predict_movement( data, pawn );
			if ( !is_valid_move(data.origin) )
				return std::nullopt;
		}

		const auto origin_delta = data.origin - latest.origin;
		if ( origin_delta.length() > max_step * delta_ticks * 1.85f )
			return std::nullopt;
		if ( origin_delta.length() > 820.0f )
			return std::nullopt;

		record extrap_record = latest;
		extrap_record.origin = data.origin;
		extrap_record.simulation_time = data.sim_time;
		extrap_record.tick = cstypes::time_to_ticks( data.sim_time );
		extrap_record.extrapolated = true;
		extrap_record.valid = true;

		for ( auto i = 0; i < extrap_record.bone_count && i < 128; ++i )
		{
			extrap_record.bones[ i ].position.x += origin_delta.x;
			extrap_record.bones[ i ].position.y += origin_delta.y;
			extrap_record.bones[ i ].position.z += origin_delta.z;
		}

		return extrap_record;
	}

}
