#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/tls/dynamic_tls.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

#include <mutex>

namespace features::combat {

	namespace detail {

		// every fraction from the trace is normalized over this span
		// 4, not 8: hit_elements is 0xc0 bytes = 8 records, and each penetration
		// can emit a solid span plus an air gap
		inline constexpr auto k_max_penetrations{ 4 };
		inline constexpr auto k_trace_range{ 8192.0f };

		// layout verified against client.dll
		struct bullet_trace_record
		{
			// fractions over the full trace ray
			float enter_fraction;
			float exit_fraction;

			// damage still carried AFTER this span, not damage dealt at it.
			// zeroed on the record where the bullet dies.
			float damage_applied;

			// penetrations left in the budget (was mislabelled team_at_contact)
			int penetrations_remaining;

			// indices into the surface array; entity handle at element + 0x2c
			std::uint16_t enter_contact_ix;
			std::uint16_t exit_contact_ix;

			// bit 0 set = span inside solid material, clear = air gap.
			// really "is solid", not "can be penetrated".
			std::uint8_t can_penetrate;
			std::uint8_t pad[ 3 ];
		};

		inline std::mutex g_trace_mutex{};

		struct bullet_path
		{
			int num_hits{};
			const bullet_trace_record* records{};
			std::uintptr_t surface_array{};
		};

		inline bool fire_bullet(
			const math::vector3& start,
			const math::vector3& direction,
			const systems::tracing::filter& filter,
			const shared::penetration::weapon_data& weapon,
			std::uintptr_t local_pawn,
			int local_team,
			shared::lagcomp::record* autowall_record,
			bool track_record,
			bullet_path& out )
		{
			const auto trace_delta = direction * k_trace_range;

			static tls::dynamic_tls<systems::tracing::trace_data> trace_storage_slot{};
			auto& trace_storage = trace_storage_slot.get( );
			trace_storage = {};
			auto* trace = &trace_storage;
			trace->array_pointer = &trace->elements;
			trace->hit_array_pointer = &trace->hit_elements;

			std::lock_guard trace_lock( detail::g_trace_mutex );

			if ( track_record )
			{
				g_shared.begin_autowall( autowall_record );
			}

			systems::g_tracing.setup_trace( trace, start, trace_delta, filter, k_max_penetrations, true );

			if ( track_record )
			{
				g_shared.end_autowall( );
			}

			out.num_hits = trace->num_hits;

			if ( out.num_hits <= 0 )
			{
				return false;
			}

			out.surface_array = reinterpret_cast< std::uintptr_t >( trace->array_pointer );
			out.records = reinterpret_cast< const bullet_trace_record* >( trace->hit_array_pointer );

			memory::call<void>( PATTERN( patterns::trace_bullet ), trace, weapon.damage, weapon.penetration, weapon.range_modifier, k_max_penetrations, local_team, static_cast< std::uintptr_t >( 0 ) );

			return true;
		}

		inline bool intersect_capsule(
			const shared& inst,
			const math::vector3& start,
			const math::vector3& delta,
			const math::vector3& capsule_a,
			const math::vector3& capsule_b,
			float radius,
			float& out_fraction )
		{
			return inst.ray_vs_capsule( start, delta, capsule_a, capsule_b, radius, out_fraction );
		}

		inline bool intersect_box(
			const math::quaternion& bone_rotation,
			const math::vector3& bone_position,
			const math::vector3& start,
			const math::vector3& delta,
			const math::vector3& mins,
			const math::vector3& maxs,
			float& out_fraction )
		{
			auto inverse = bone_rotation;
			inverse.x = -inverse.x;
			inverse.y = -inverse.y;
			inverse.z = -inverse.z;

			const auto local_origin = inverse.rotate_vector( start - bone_position );
			const auto local_delta = inverse.rotate_vector( delta );

			auto entry{ 0.0f };
			auto exit{ 1.0f };

			const auto axis = [ & ]( float origin, float delta_axis, float minimum, float maximum )
			{
				if ( std::fabsf( delta_axis ) < 1.0e-8f )
				{
					return origin >= minimum && origin <= maximum;
				}

				auto first = ( minimum - origin ) / delta_axis;
				auto second = ( maximum - origin ) / delta_axis;
				if ( first > second ) std::swap( first, second );
				entry = std::max( entry, first );
				exit = std::min( exit, second );
				return entry <= exit;
			};

			if ( !axis( local_origin.x, local_delta.x, mins.x, maxs.x ) ||
				!axis( local_origin.y, local_delta.y, mins.y, maxs.y ) ||
				!axis( local_origin.z, local_delta.z, mins.z, maxs.z ) )
			{
				return false;
			}

			// entry is parameterized over delta, so it is directly comparable to
			// the engine's enter_fraction
			out_fraction = std::clamp( entry, 0.0f, 1.0f );
			return true;
		}

	}

	void shared::penetration::prepare( std::uintptr_t weapon_vdata, std::uintptr_t weapon )
	{
		if ( !weapon_vdata || !weapon )
		{
			return;
		}

		this->m_weapon_data = weapon_data
		{
			.damage = static_cast< float >( memory::read<int>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_nDamage"_hash ) ) ),
			.penetration = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flPenetration"_hash ) ),
			.range_modifier = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRangeModifier"_hash ) ),
			.range = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRange"_hash ) ),
			.armor_ratio = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flArmorRatio"_hash ) ),
			.headshot_multiplier = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flHeadshotMultiplier"_hash ) )
		};
	}

	shared::penetration::target_static shared::penetration::prepare_target_static( std::uintptr_t target_pawn ) const
	{
		target_static ts{};

		ts.armor = memory::read<int>( target_pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );
		ts.team = memory::read<int>( target_pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );

		if ( ts.armor > 0 )
		{
			const auto services = memory::read<std::uintptr_t>( target_pawn + SCHEMA( "C_BasePlayerPawn", "m_pItemServices"_hash ) );
			if ( services )
			{
				ts.has_helmet = memory::read<bool>( services + SCHEMA( "CCSPlayer_ItemServices", "m_bHasHelmet"_hash ) );
			}
		}

		ts.scales =
		{
			.ct_head = CONVAR ( "mp_damage_scale_ct_head" )->get<float>( ),
			.t_head = CONVAR ( "mp_damage_scale_t_head" )->get<float>( ),
			.ct_body = CONVAR ( "mp_damage_scale_ct_body" )->get<float>( ),
			.t_body = CONVAR ( "mp_damage_scale_t_body" )->get<float>( )
		};

		ts.armor_ratio = this->m_weapon_data.armor_ratio;
		ts.headshot_multiplier = this->m_weapon_data.headshot_multiplier;

		return ts;
	}

	shared::penetration::run_context shared::penetration::prepare_target( std::uintptr_t target_pawn, lagcomp::record* record ) const
	{
		run_context ctx{};
		ctx.target_pawn = target_pawn;
		ctx.record = record;
		if ( record && record->game_scene_node )
		{
			ctx.hitboxes = systems::g_hitboxes.query( record->game_scene_node );
		}

		ctx.target_armor = memory::read<int>( target_pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );
		ctx.target_team = memory::read<int>( target_pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );

		if ( ctx.target_armor > 0 )
		{
			const auto services = memory::read<std::uintptr_t>( target_pawn + SCHEMA( "C_BasePlayerPawn", "m_pItemServices"_hash ) );
			if ( services )
			{
				ctx.has_helmet = memory::read<bool>( services + SCHEMA( "CCSPlayer_ItemServices", "m_bHasHelmet"_hash ) );
			}
		}

		ctx.scales =
		{
			.ct_head = CONVAR ( "mp_damage_scale_ct_head" )->get<float>( ),
			.t_head = CONVAR ( "mp_damage_scale_t_head" )->get<float>( ),
			.ct_body = CONVAR ( "mp_damage_scale_ct_body" )->get<float>( ),
			.t_body = CONVAR ( "mp_damage_scale_t_body" )->get<float>( )
		};

		ctx.armor_ratio = this->m_weapon_data.armor_ratio;
		ctx.headshot_multiplier = this->m_weapon_data.headshot_multiplier;

		return ctx;
	}

	shared::penetration::run_context shared::penetration::prepare_target( std::uintptr_t target_pawn, lagcomp::record* record, const target_static& ts ) const
	{
		run_context ctx{};
		ctx.target_pawn = target_pawn;
		ctx.record = record;
		if ( record && record->game_scene_node )
		{
			ctx.hitboxes = systems::g_hitboxes.query( record->game_scene_node );
		}

		ctx.target_armor = ts.armor;
		ctx.target_team = ts.team;
		ctx.has_helmet = ts.has_helmet;
		ctx.scales = ts.scales;
		ctx.armor_ratio = ts.armor_ratio;
		ctx.headshot_multiplier = ts.headshot_multiplier;

		return ctx;
	}

bool shared::penetration::run( const math::vector3& start, const math::vector3& end, const run_context& ctx, std::uintptr_t local_pawn, int local_team, result& out ) const
{
	out = {};

	if ( this->m_weapon_data.damage <= 0.0f || this->m_weapon_data.penetration <= 0.0f )
	{
		return false;
	}

	const auto to_target = end - start;
	const auto target_distance_sq = to_target.dot( to_target );

	if ( target_distance_sq < 1.0f )
	{
		return false;
	}

	const auto direction = to_target / std::sqrtf( target_distance_sq );

	const auto filter = systems::g_tracing.make_filter( local_pawn, 0x1c300b, 3, 15 );

	detail::bullet_path path{};
	if ( !detail::fire_bullet( start, direction, filter, this->m_weapon_data, local_pawn, local_team, ctx.record, ctx.record != nullptr, path ) )
	{
		return false;
	}

	// nearest hitbox along the ray, nearest centre as fallback. do NOT match
	// against the record's enter_fraction: on a penetrating shot that marks
	// where the air gap after the wall began, so it diverges by the
	// wall-to-target distance and kills long wallbangs.
	auto actual_hitbox{ -1 };
	auto fallback_hitbox{ -1 };
	auto closest_hitbox_fraction{ 1.0f };
	auto closest_hitbox_projection{ 1.0f };

	if ( ctx.record )
	{
		for ( const auto& hitbox : ctx.hitboxes )
		{
			if ( hitbox.bone < 0 || hitbox.bone >= ctx.record->bone_count || hitbox.index < 0 )
			{
				continue;
			}

			const auto& bone = ctx.record->bones[ hitbox.bone ];
			auto fraction{ 1.0f };
			auto intersects{ false };

			if ( hitbox.radius > 0.001f )
			{
				const auto capsule_start = bone.rotation.rotate_vector( hitbox.mins ) + bone.position;
				const auto capsule_end = bone.rotation.rotate_vector( hitbox.maxs ) + bone.position;
				intersects = detail::intersect_capsule( g_shared, start, direction * detail::k_trace_range, capsule_start, capsule_end, hitbox.radius, fraction );
			}
			else
			{
				intersects = detail::intersect_box( bone.rotation, bone.position, start, direction * detail::k_trace_range, hitbox.mins, hitbox.maxs, fraction );
			}

			if ( intersects && fraction < closest_hitbox_fraction )
			{
				closest_hitbox_fraction = fraction;
				actual_hitbox = hitbox.index;
			}

			const auto center = bone.rotation.rotate_vector( ( hitbox.mins + hitbox.maxs ) * 0.5f ) + bone.position;
			const auto projection = ( center - start ).dot( direction ) / detail::k_trace_range;

			if ( projection >= 0.0f && projection < closest_hitbox_projection )
			{
				closest_hitbox_projection = projection;
				fallback_hitbox = hitbox.index;
			}
		}
	}

	if ( actual_hitbox < 0 )
	{
		actual_hitbox = fallback_hitbox;
	}

	auto penetrated{ false };

	for ( auto i = 0; i < path.num_hits; ++i )
	{
		const auto& hit = path.records[ i ];

		if ( hit.damage_applied <= 0.0f )
		{
			break;
		}

		// players are found on the cleared-bit records -- matching them on the
		// set-bit ones instead made the ragebot stop finding targets entirely.
		if ( ( hit.can_penetrate & 1 ) != 0 )
		{
			penetrated = true;

			if ( hit.exit_fraction == 1.0f )
			{
				break;
			}

			continue;
		}

		const auto trace_holder = path.surface_array + sizeof( systems::tracing::trace_array_element ) * ( hit.enter_contact_ix & 0x7fff );
		const auto hit_handle = memory::safe_read<std::uint32_t>( trace_holder + 0x2c ).value_or( 0 );
		const auto hit_entity = hit_handle ? systems::g_entities.lookup( hit_handle ) : 0;

		if ( !hit_entity || hit_entity != ctx.target_pawn )
		{
			continue;
		}

		const auto best_hitbox = actual_hitbox;
		if ( best_hitbox < 0 )
		{
			continue;
		}

		auto final_damage = hit.damage_applied;

		this->scale_damage( systems::g_hitboxes.hitgroup_from_hitbox( best_hitbox ), ctx.target_armor, ctx.has_helmet, ctx.target_team, ctx.armor_ratio, ctx.headshot_multiplier, ctx.scales, final_damage );

		const auto max_possible = this->get_max_damage( systems::g_hitboxes.hitgroup_from_hitbox( best_hitbox ), ctx.target_armor, ctx.has_helmet, ctx.target_team );
		final_damage = std::clamp( final_damage, 0.0f, std::max( max_possible, 1.0f ) );

		out.hitbox = best_hitbox;
		out.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( best_hitbox );
		out.penetrated = penetrated;
		out.damage = final_damage;

		return true;
	}

	return false;
}

bool shared::penetration::can( const math::vector3& start, const math::vector3& direction, float& out_damage, const systems::local::snapshot& local ) const
{
	out_damage = 0.0f;

	if ( this->m_weapon_data.damage <= 0.0f || this->m_weapon_data.penetration <= 0.0f )
	{
		return false;
	}

	const auto dir = direction.normalized( );
	const auto local_team = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
	const auto filter = systems::g_tracing.make_filter( local.pawn, 0x1c300b, 3, 15 );

	detail::bullet_path path{};
	if ( !detail::fire_bullet( start, dir, filter, this->m_weapon_data, local.pawn, local_team, nullptr, false, path ) )
	{
		return false;
	}

	for ( auto i = 0; i < path.num_hits; ++i )
	{
		const auto& hit = path.records[ i ];

		if ( hit.damage_applied <= 0.0f )
		{
			break;
		}

		if ( ( hit.can_penetrate & 1 ) != 0 )
		{
			// Pure "can this shot get through the wall" check: don't treat
			// "this was the last recorded surface" as blocked -- that's
			// just as true for a bare wall with nothing behind it as it is
			// for one that fully absorbed the shot, and the two need to be
			// told apart by can_penetrate, not by whether anything further
			// was in the trace.
			//
			// damage_applied here is raw, pre scale_damage -- no armor/hitgroup
			// context reaches this call. armor only ever reduces the delivered
			// amount, and run()'s output is always floored to whole HP, so
			// anything under 1 raw unit is guaranteed to floor to 0 real damage
			// even unarmored. 0.03 let that dead range read as "penetrable".
			if ( hit.damage_applied < 1.0f )
			{
				return false;
			}

			out_damage = hit.damage_applied;
			return true;
		}
	}

	return false;
}

	float shared::penetration::penetration_cost( float damage, float thickness, float surface_modifier, float damage_scale ) const
	{
		// Transcribed from client.dll's per-contact penetration handler:
		//
		//   m    = max( 0, 1 / S )
		//   cost = 3m * max( 0, 3.75 / P )  +  k * D  +  ( T^2 * m ) / 24
		//
		// with D = damage carried in, P = the weapon's m_flPenetration,
		// S = surface modifier, T = thickness, k = 0.16 by default. Note the
		// T^2 term: penetration falls off quadratically with wall thickness,
		// not linearly, which is why a wall being twice as thick costs far
		// more than twice the damage.
		//
		// The engine substitutes S and k for a handful of surface classes when
		// the entry and exit materials match -- props 85/87 -> S 3.0, prop 76
		// -> S 2.0, thin 71/89 -> S 3.0 with k 0.05, and a 0x2000-flagged pair
		// -> S 32.0 with k 0.00001 when thin (glass, essentially free) or
		// S 3.0 when thick. Callers that know the surface class should pass
		// the substituted values rather than the raw ones.
		if ( surface_modifier < k_min_surface_modifier || this->m_weapon_data.penetration <= 0.0f )
		{
			return std::numeric_limits<float>::max( );
		}

		const auto m = std::max( 0.0f, 1.0f / surface_modifier );
		const auto weapon_term = std::max( 0.0f, 3.75f / this->m_weapon_data.penetration );
		const auto cost = 3.0f * m * weapon_term
			+ damage_scale * damage
			+ ( thickness * thickness * m ) / 24.0f;

		return std::max( 0.0f, cost );
	}

	float shared::penetration::get_max_damage( int hitgroup, int target_armor, bool has_helmet, int target_team ) const
	{
		if ( this->m_weapon_data.damage <= 0.0f )
		{
			return 0.0f;
		}

		const damage_scales scales
		{
			.ct_head = CONVAR ( "mp_damage_scale_ct_head" )->get<float>( ),
			.t_head = CONVAR ( "mp_damage_scale_t_head" )->get<float>( ),
			.ct_body = CONVAR ( "mp_damage_scale_ct_body" )->get<float>( ),
			.t_body = CONVAR ( "mp_damage_scale_t_body" )->get<float>( )
		};

		auto damage = this->m_weapon_data.damage;
		this->scale_damage( hitgroup, target_armor, has_helmet, target_team, this->m_weapon_data.armor_ratio, this->m_weapon_data.headshot_multiplier, scales, damage );
		return damage;
	}

	void shared::penetration::scale_damage( int hitgroup, int armor, bool has_helmet, int team, float armor_ratio, float headshot_multiplier, const damage_scales& scales, float& damage ) const
	{
		const auto is_ct = ( team == 3 );
		const auto head_scale = is_ct ? scales.ct_head : scales.t_head;
		const auto body_scale = is_ct ? scales.ct_body : scales.t_body;

		switch ( hitgroup )
		{
		case 1:
			damage *= headshot_multiplier * head_scale;
			break;
		case 2:
		case 4:
		case 5:
		case 8:
			damage *= body_scale;
			break;
		case 3:
			damage *= 1.25f * body_scale;
			break;
		case 6:
		case 7:
			damage *= 0.75f * body_scale;
			break;
		default:
			break;
		}

		const auto is_head = ( hitgroup == 1 );
		const auto is_armored = ( hitgroup >= 1 && hitgroup <= 5 ) || ( hitgroup == 8 );

		if ( armor <= 0 || !is_armored || ( is_head && !has_helmet ) )
		{
			damage = std::floor( damage );
			return;
		}

		constexpr auto armor_bonus{ 0.5f };
		const auto armor_ratio_scaled = armor_ratio * 0.5f;

		auto damage_to_health = damage * armor_ratio_scaled;
		auto damage_to_armor = ( damage - damage_to_health ) * armor_bonus;

		if ( damage_to_armor > static_cast< float >( armor ) )
		{
			damage_to_health = damage - ( static_cast< float >( armor ) / armor_bonus );
		}

		damage = std::floor( damage_to_health );
	}

}
