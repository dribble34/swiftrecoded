#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/tls/dynamic_tls.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	std::uint32_t shared::get_spread_seed( const math::vector3& angles, int tick ) const
	{
		return memory::call<std::uint32_t>(PATTERN (patterns::get_tick_view_angles), nullptr, &angles, tick );
	}

	math::vector2 shared::calculate_spread( int seed, float accuracy, float spread, float recoil_index, int item_def_idx, int num_bullets ) const
	{
		math::vector2 out{};

		memory::call<void>(PATTERN (patterns::weapon_calculate_spread), static_cast< std::int16_t >( item_def_idx ), num_bullets, 0, static_cast< std::uint32_t >( seed + 1 ), accuracy, spread, recoil_index, &out.x, &out.y );

		return out;
	}

	math::vector3 shared::get_aim_punch( std::uintptr_t local_pawn ) const
	{
		math::vector3 out{};

		memory::call<void>(PATTERN (patterns::get_aim_punch), memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_CSPlayerPawn", "m_pAimPunchServices"_hash ) ), &out, 0u );

		return out;
	}

	float shared::calculate_hitchance( const math::vector3& shoot_position, const math::vector3& aim_angle, const systems::hitboxes::entry& hitbox, const systems::bones::data& bone, float inaccuracy, float spread, int samples ) const
	{
		const auto total = spread + inaccuracy;
		if ( total < 0.0001f )
		{
			return 1.0f;
		}

		if ( samples <= 0 )
		{
			return 0.0f;
		}

		const auto capsule_start = bone.rotation.rotate_vector( hitbox.mins ) + bone.position;
		const auto capsule_end = bone.rotation.rotate_vector( hitbox.maxs ) + bone.position;
		const auto is_capsule = hitbox.radius > 0.001f;
		auto inverse_rotation = bone.rotation;
		inverse_rotation.x = -inverse_rotation.x;
		inverse_rotation.y = -inverse_rotation.y;
		inverse_rotation.z = -inverse_rotation.z;
		const auto box_ray_origin = inverse_rotation.rotate_vector( shoot_position - bone.position );

		const auto ray_vs_box = [ & ]( const math::vector3& ray_direction )
		{
			const auto direction = inverse_rotation.rotate_vector( ray_direction );
			auto entry{ 0.0f };
			auto exit{ 1.0f };

			const auto intersect_axis = [ & ]( float origin, float delta, float minimum, float maximum )
			{
				if ( std::fabs( delta ) < 1.0e-8f )
				{
					return origin >= minimum && origin <= maximum;
				}

				auto first = ( minimum - origin ) / delta;
				auto second = ( maximum - origin ) / delta;
				if ( first > second )
				{
					std::swap( first, second );
				}

				entry = std::max( entry, first );
				exit = std::min( exit, second );
				return entry <= exit;
			};

			return intersect_axis( box_ray_origin.x, direction.x, hitbox.mins.x, hitbox.maxs.x ) &&
				intersect_axis( box_ray_origin.y, direction.y, hitbox.mins.y, hitbox.maxs.y ) &&
				intersect_axis( box_ray_origin.z, direction.z, hitbox.mins.z, hitbox.maxs.z );
		};

		math::vector3 forward{}, left{}, up{};
		math::helpers::angle_vectors_left( aim_angle, &forward, &left, &up );

		struct spread_cache
		{
			float inaccuracy{};
			float spread{};
			float recoil_index{};
			int item_def_idx{};
			int num_bullets{};
			int count{};
			bool initialized{};
			std::array<math::vector2, 256> values{};
		};

		static tls::dynamic_tls<spread_cache> cache_slot{};
		auto& cache = cache_slot.get( );
		if ( !cache.initialized || cache.inaccuracy != inaccuracy || cache.spread != spread ||
			cache.recoil_index != this->m_ctx.recoil_index || cache.item_def_idx != this->m_ctx.item_def_idx ||
			cache.num_bullets != this->m_ctx.num_bullets )
		{
			cache.inaccuracy = inaccuracy;
			cache.spread = spread;
			cache.recoil_index = this->m_ctx.recoil_index;
			cache.item_def_idx = this->m_ctx.item_def_idx;
			cache.num_bullets = this->m_ctx.num_bullets;
			cache.count = 0;
			cache.initialized = true;
		}

		const auto cached_samples = std::min( samples, static_cast< int >( cache.values.size( ) ) );
		for ( auto i = cache.count; i < cached_samples; ++i )
		{
			cache.values[ i ] = this->calculate_spread( i, inaccuracy, spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );
		}
		cache.count = std::max( cache.count, cached_samples );

		auto hits{ 0 };

		for ( auto i = 0; i < samples; ++i )
		{
			const auto calculated_spread = i < cached_samples
				? cache.values[ i ]
				: this->calculate_spread( i, inaccuracy, spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );
			const auto direction = forward + ( left * calculated_spread.x ) + ( up * calculated_spread.y );
			const auto ray_end = direction.normalized( ) * 8192.0f;

			auto hit{ false };
			if ( is_capsule )
			{
				auto fraction{ 1.0f };
				hit = this->ray_vs_capsule( shoot_position, ray_end, capsule_start, capsule_end, hitbox.radius, fraction );
			}
			else
			{
				hit = ray_vs_box( ray_end );
			}

			if ( hit )
			{
				++hits;
			}
		}

		return static_cast< float >( hits ) / static_cast< float >( samples );
	}

	math::vector3 shared::find_spread_correction( const math::vector3& aim_angle, int tick ) const
	{
		for ( auto i = 0; i < 720; i++ )
		{
			const auto test_angles = math::vector3{ static_cast< float >( i ) / 2.0f, aim_angle.y, 0.0f };
			const auto seed = this->get_spread_seed( test_angles, tick );
			const auto spread = this->calculate_spread( seed, this->m_ctx.inaccuracy, this->m_ctx.spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );

			auto adj_angle = aim_angle;
			adj_angle.x += math::helpers::rad_to_deg( std::atan( std::sqrt( spread.x * spread.x + spread.y * spread.y ) ) );
			adj_angle.z = -math::helpers::rad_to_deg( std::atan2( spread.x, spread.y ) );

			if ( this->get_spread_seed( adj_angle, tick ) == seed )
			{
				return adj_angle;
			}
		}

		return {};
	}

	math::vector3 shared::get_eye_position( std::uintptr_t local_pawn ) const
	{
		const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto view_offset = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		return origin + view_offset;
	}

	math::vector3 shared::get_shoot_position( ) const
	{
		math::vector3 out{};
		memory::call_vfunc<void>( this->m_ctx.weapon_services, 29, reinterpret_cast< std::uintptr_t >( &out ) );
		return out;
	}

	math::vector3 shared::get_interpolated_shoot_position( std::uintptr_t local_pawn, bool newest ) const
	{
		const auto ws = this->m_ctx.weapon_services;
		const auto head = memory::read<int>( ws + 872 );
		const auto count = memory::read<int>( ws + 876 );

		if ( count < 1 )
		{
			return this->get_shoot_position( );
		}

		if ( newest )
		{
			const auto newest_idx = ( head + count - 1 ) % 32;
			const auto newest_off = ws + 232 + 20ull * newest_idx;
			return memory::read<math::vector3>( newest_off + 8 );
		}

		if ( count < 2 )
		{
			return this->get_shoot_position( );
		}

		const auto interp = memory::call<float>(PATTERN (patterns::get_interp_amount), local_pawn );
		const auto newest_idx = ( head + count - 1 ) % 32;
		const auto newest_off = ws + 232 + 20ull * newest_idx;
		const auto newest_tick = memory::read<int>( newest_off );
		const auto newest_frac = memory::read<float>( newest_off + 4 );

		const auto target = cstypes::tick_fraction{ newest_tick, newest_frac }.subtract_value( interp * 64.0f );

		for ( auto i = 0; i < count - 1; ++i )
		{
			const auto idx_a = ( head + static_cast< std::size_t >( i ) ) % 32;
			const auto idx_b = ( head + static_cast< std::size_t >( i ) + 1 ) % 32;

			const auto a_off = ws + 232 + 20ull * idx_a;
			const auto b_off = ws + 232 + 20ull * idx_b;

			const auto a_tick = memory::read<int>( a_off );
			const auto a_frac = memory::read<float>( a_off + 4 );
			const auto b_tick = memory::read<int>( b_off );
			const auto b_frac = memory::read<float>( b_off + 4 );

			const auto a_before = a_tick < target.tick || ( a_tick == target.tick && a_frac <= target.frac );
			if ( !a_before )
			{
				break;
			}

			const auto b_after = b_tick > target.tick || ( b_tick == target.tick && b_frac >= target.frac );
			if ( !b_after )
			{
				continue;
			}

			const auto a_pos = memory::read<math::vector3>( a_off + 8 );
			const auto b_pos = memory::read<math::vector3>( b_off + 8 );

			const auto span = cstypes::tick_fraction{ b_tick, b_frac }.subtract( { a_tick, a_frac } );
			const auto partial = target.subtract( { a_tick, a_frac } );

			const auto total_f = static_cast< float >( span.tick ) + span.frac;
			const auto partial_f = static_cast< float >( partial.tick ) + partial.frac;

			const auto t = total_f > 0.0f ? partial_f / total_f : 0.0f;

			return a_pos + ( b_pos - a_pos ) * t;
		}

		return this->get_shoot_position( );
	}

	int shared::calculate_stop_ticks( const math::vector3& velocity, float max_speed, std::uintptr_t local_pawn ) const
	{
		auto vel = velocity;
		vel.z = 0.0f;

		auto ticks{ 0 };
		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );
		const auto sv_accelerate = CONVAR ("sv_accelerate")->get<float>( );
		const auto surface_friction = systems::g_prediction.pre( ).surface_friction;
		const auto accurate_threshold = max_speed * 0.34f;

		const auto is_scoped = this->m_ctx.is_scoped;
		auto max_move_speed{ 250.0f };

		if ( is_scoped && local_pawn )
		{
			const auto movement_services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
			if ( movement_services )
			{
				max_move_speed = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );
			}
		}

		while ( vel.length_2d( ) > accurate_threshold && ticks < 15 )
		{
			const auto speed = vel.length_2d( );
			if ( speed <= 0.0f )
			{
				break;
			}

			const auto control = std::fmaxf( speed, sv_stopspeed );
			const auto drop = sv_friction * surface_friction * control * cstypes::tick_interval;
			auto new_speed = std::fmaxf( speed - drop, 0.0f );

			auto accel = sv_accelerate;

			if ( is_scoped )
			{
				const auto weapon_ratio = std::fminf( 1.0f, max_speed / 250.0f );
				const auto scoped_max = std::fmaxf( 250.0f, max_move_speed ) * weapon_ratio * 0.52f;

				if ( new_speed > scoped_max - 5.0f )
				{
					const auto t = 1.0f - std::fmaxf( 0.0f, new_speed - ( scoped_max - 5.0f ) ) / std::fmaxf( 0.01f, 5.0f );
					accel *= std::clamp( t, 0.0f, 1.0f );
				}
			}

			const auto accel_speed = std::fminf( accel * max_speed * surface_friction * cstypes::tick_interval, new_speed );
			new_speed = std::fmaxf( new_speed - accel_speed, 0.0f );

			vel *= ( new_speed / speed );
			ticks++;
		}

		return ticks;
	}

	float shared::get_spread( ) const
	{
		static const auto get_spread = PATTERN( patterns::get_spread );
		return memory::call<float>( get_spread, this->m_ctx.weapon );
	}

	float shared::get_inaccuracy( bool update_accuracy_penalty ) const
	{
		const auto accuracy_state_begin = SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracyDelta"_hash );
		const auto accuracy_state_end = SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash );
		if ( !this->m_ctx.weapon || accuracy_state_begin <= 0 || accuracy_state_end < accuracy_state_begin )
		{
			return 0.0f;
		}

		const auto accuracy_state_size = static_cast< std::size_t >( accuracy_state_end - accuracy_state_begin ) + sizeof( float );
		if ( accuracy_state_size > 0x100 )
		{
			return 0.0f;
		}

		std::vector<std::uint8_t> backup( accuracy_state_size );
		std::memcpy( backup.data( ), reinterpret_cast< const void* >( this->m_ctx.weapon + accuracy_state_begin ), accuracy_state_size );

		if ( update_accuracy_penalty )
		{
			memory::call<void>(PATTERN (patterns::weapon_update_accuracy), this->m_ctx.weapon );
		}

		static const auto get_inaccuracy = PATTERN( patterns::get_inaccuracy );
		const auto inaccuracy = memory::call<float>(
			get_inaccuracy, this->m_ctx.weapon,
			static_cast<float*>( nullptr ), static_cast<float*>( nullptr ) );

		std::memcpy( reinterpret_cast< void* >( this->m_ctx.weapon + accuracy_state_begin ), backup.data( ), accuracy_state_size );

		return inaccuracy;
	}

	float shared::get_inaccuracy_at_velocity( std::uintptr_t local_pawn, const math::vector3& velocity ) const
	{
		const auto accuracy_state_begin = SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracyDelta"_hash );
		const auto accuracy_state_end = SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash );
		if ( !this->m_ctx.weapon || !local_pawn || accuracy_state_begin <= 0 || accuracy_state_end < accuracy_state_begin )
		{
			return 0.0f;
		}

		const auto accuracy_state_size = static_cast< std::size_t >( accuracy_state_end - accuracy_state_begin ) + sizeof( float );
		if ( accuracy_state_size > 0x100 )
		{
			return 0.0f;
		}

		std::vector<std::uint8_t> backup( accuracy_state_size );
		std::memcpy( backup.data( ), reinterpret_cast< const void* >( this->m_ctx.weapon + accuracy_state_begin ), accuracy_state_size );

		const auto old_velocity = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
		const auto old_eflags = memory::read<std::uint32_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ) );

		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ), old_eflags & ~0x1000u );
		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ), velocity );

		memory::call<void>(PATTERN (patterns::weapon_update_accuracy), this->m_ctx.weapon );

		static const auto get_inaccuracy = PATTERN( patterns::get_inaccuracy );
		const auto inaccuracy = memory::call<float>(
			get_inaccuracy, this->m_ctx.weapon,
			static_cast<float*>( nullptr ), static_cast<float*>( nullptr ) );

		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ), old_velocity );
		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ), old_eflags );

		std::memcpy( reinterpret_cast< void* >( this->m_ctx.weapon + accuracy_state_begin ), backup.data( ), accuracy_state_size );

		return inaccuracy;
	}

	float shared::get_air_inaccuracy( float vertical_speed, float jump_initial, float jump_apex ) const
	{
		constexpr auto sqrt_threshold{ 17.37795666f };
		if ( !std::isfinite(vertical_speed) || !std::isfinite(jump_initial) || !std::isfinite(jump_apex) )
			return jump_apex;
		const auto val = ( ( std::sqrtf( std::fabsf( vertical_speed ) ) - sqrt_threshold * 0.25f ) * ( jump_initial - jump_apex ) ) / ( sqrt_threshold * 0.75f ) + jump_apex;
		return std::clamp( val, 0.0f, jump_initial * 2.2f );
	}

	float shared::get_inaccuracy_for_rate( float base_inaccuracy, float velocity_2d, bool on_ground, bool ducking, bool scoped ) const
	{
		float v = base_inaccuracy;
		if ( !std::isfinite(v) )
			v = 0.0f;
		v = std::clamp(v, 0.0f, 2.0f);
		float vel_factor = velocity_2d * 0.0018f;
		if ( !on_ground )
			vel_factor *= 1.85f;
		if ( ducking && on_ground )
			vel_factor *= 0.72f;
		if ( scoped && on_ground )
			vel_factor *= 0.65f;
		float out = v + vel_factor;
		if ( !on_ground )
		{
			float air = this->get_air_inaccuracy( 0.0f, v + 0.25f, v );
			out = std::max(out, air);
		}
		return std::clamp(out, 0.0f, 2.0f);
	}

	float shared::get_spread_for_rate( float base_spread, int recoil_idx ) const
	{
		float s = base_spread;
		if ( !std::isfinite(s) )
			s = 0.0f;
		s = std::clamp(s, 0.0f, 1.0f);
		if ( recoil_idx > 0 )
		{
			float recoil_factor = std::clamp((float)recoil_idx * 0.008f, 0.0f, 0.12f);
			s += recoil_factor;
		}
		return std::clamp(s, 0.0f, 1.0f);
	}

	int shared::get_rate_tick_compensation( int tick_base, int next_tick ) const
	{
		int delta = next_tick - tick_base;
		if ( delta <= 0 )
			return 0;
		if ( delta > 8 )
			return delta;
		const auto sv_maxusrcmd = CONVAR("sv_maxusrcmdprocessticks")->get<int>();
		if ( sv_maxusrcmd > 0 && delta > sv_maxusrcmd )
			return delta;
		return delta;
	}

	bool shared::can_shoot( systems::input::usercmd* cmd, std::uintptr_t local_controller, bool check_next_attack ) const
	{
		if ( this->m_ctx.weapon_type != cstypes::weapon_type::knife )
		{
			if ( memory::read<bool>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_bInReload"_hash ) ) )
			{
				return false;
			}

			if ( memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) ) <= 0 )
			{
				return false;
			}
		}

		if ( !check_next_attack )
		{
			return true;
		}

		const auto tick_base = memory::read<int>( local_controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto base_cmd = cmd->csgo_user_cmd.base( );
		const auto client_tick = base_cmd ? base_cmd->client_tick( ) : tick_base;
		const auto next_primary = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );

		if ( this->m_ctx.weapon_type == cstypes::weapon_type::knife )
		{
			const auto next_secondary = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash ) );
			return tick_base >= this->m_last_shoot_tick + 1 && ( client_tick >= next_primary || client_tick >= next_secondary );
		}

		return tick_base >= this->m_last_shoot_tick + 1 && client_tick >= next_primary;
	}

	bool shared::is_max_accuracy( float inaccuracy ) const
	{
		const auto& prestate = systems::g_prediction.pre( );
		const auto on_ground = ( prestate.flags & 1 ) != 0;
		const auto is_ducking = ( prestate.flags & 4 ) != 0;
		const auto speed = prestate.networked_velocity.length_2d( );

		if ( on_ground )
		{
			if ( this->m_ctx.weapon_type == cstypes::weapon_type::sniper )
			{
				if ( !this->m_ctx.is_scoped )
				{
					return false;
				}

				if ( is_ducking )
				{
					const auto rounded = std::floorf( inaccuracy * 300.0f ) / 300.0f;
					return rounded < inaccuracy;
				}

				if ( speed <= 0.1f )
				{
					const auto rounded = std::floorf( inaccuracy * 170.0f ) / 170.0f;
					return rounded < inaccuracy;
				}

				return false;
			}

			return speed <= this->m_ctx.weapon_max_speed * 0.34f;
		}

		const auto inaccuracy_jump_apex = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );
		const auto accuracy_penalty = memory::read<float>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_hash ) );
		const auto min_air_inaccuracy = accuracy_penalty + inaccuracy_jump_apex;

		constexpr auto tolerance{ 0.001f };
		return inaccuracy <= min_air_inaccuracy + tolerance;
	}

	math::vector3 shared::simulate_aim_punch( int recoil_index ) const
	{
		if ( recoil_index <= 0 || !this->m_ctx.valid )
		{
			return {};
		}

		const auto weapon_mode = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash ) );
		const auto cycle_time = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flCycleTime"_hash ) );

		constexpr auto decay_rate{ 4.5f };
		constexpr auto decay2_exp{ 8.0f };
		constexpr auto decay2_lin{ 18.0f };
		constexpr auto recoil_scale{ 2.0f };

		math::vector3 punch{};
		math::vector3 punch_vel{};

		auto hybrid_decay = [ ]( math::vector3& v, float exp, float lin, float dt )
			{
				v *= std::expf( -exp * dt );

				const auto mag = v.length( );
				if ( mag > lin * dt )
				{
					v *= ( 1.0f - ( lin * dt ) / mag );
				}
				else
				{
					v = {};
				}
			};

		for ( auto i = 0; i < recoil_index; ++i )
		{
			float angle{}, magnitude{};
			memory::call<void>(PATTERN (patterns::weapon_get_recoil_offset), addresses::globals::weapon_recoil_data, this->m_ctx.weapon, weapon_mode, i, &angle, &magnitude );

			math::vector3 offset{};
			offset.x = std::cosf( math::helpers::deg_to_rad( angle ) ) * magnitude;
			offset.y = std::sinf( math::helpers::deg_to_rad( angle ) ) * magnitude;

			punch_vel -= offset;

			for ( auto time = 0.0f; time <= cycle_time; time += cstypes::tick_interval )
			{
				hybrid_decay( punch, decay2_exp, decay2_lin, cstypes::tick_interval );

				punch += punch_vel * cstypes::tick_interval * 0.5f;
				punch_vel *= std::expf( -decay_rate * cstypes::tick_interval );

				if ( punch_vel.length( ) < 0.03125f )
				{
					punch_vel = {};
				}

				punch += punch_vel * cstypes::tick_interval * 0.5f;
			}
		}

		return punch * recoil_scale;
	}

} // namespace features::combat
