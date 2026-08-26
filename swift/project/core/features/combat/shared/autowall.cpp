#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

#include <mutex>

namespace features::combat {

	namespace detail {

		struct bullet_trace_record
		{
			float enter_fraction;
			float exit_fraction;
			float damage_applied;
			int team_at_contact;
			std::uint16_t enter_contact_ix;
			std::uint16_t exit_contact_ix;
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
			constexpr auto k_max_penetrations{ 8 };
			constexpr auto k_trace_range{ 8192.0f };

			const auto trace_delta = direction * k_trace_range;

			thread_local systems::tracing::trace_data trace_storage{};
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
			const math::vector3& maxs )
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

			return axis( local_origin.x, local_delta.x, mins.x, maxs.x ) &&
				axis( local_origin.y, local_delta.y, mins.y, maxs.y ) &&
				axis( local_origin.z, local_delta.z, mins.z, maxs.z );
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

	struct hitbox_intersection
	{
		int index{ -1 };
		float fraction{ 1.0f };
		float center_projection{ 1.0f };
		bool valid{ false };
	};

	hitbox_intersection intersections[ 19 ]{};
	auto intersection_count{ 0 };

	if ( ctx.record )
	{
		for ( const auto& hitbox : ctx.hitboxes )
		{
			if ( hitbox.bone < 0 || hitbox.bone >= ctx.record->bone_count || hitbox.index < 0 || hitbox.index >= 19 )
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
				intersects = detail::intersect_capsule( g_shared, start, direction * 8192.0f, capsule_start, capsule_end, hitbox.radius, fraction );
			}
			else
			{
				intersects = detail::intersect_box( bone.rotation, bone.position, start, direction * 8192.0f, hitbox.mins, hitbox.maxs );
			}

			if ( intersects )
			{
				const auto center = bone.rotation.rotate_vector( ( hitbox.mins + hitbox.maxs ) * 0.5f ) + bone.position;
				const auto center_delta = center - start;
				const auto projection = center_delta.dot( direction ) / 8192.0f;

				intersections[ intersection_count ] = hitbox_intersection
				{
					.index = hitbox.index,
					.fraction = fraction,
					.center_projection = projection,
					.valid = true
				};
				++intersection_count;
			}
		}
	}

	auto penetrated{ false };

	for ( auto i = 0; i < path.num_hits; ++i )
	{
		const auto& hit = path.records[ i ];

		if ( hit.damage_applied <= 0.0f )
		{
			break;
		}

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

		auto best_hitbox{ -1 };
		auto best_match_diff{ 1.0f };

		const auto trace_fraction = hit.enter_fraction;

		for ( auto j = 0; j < intersection_count; ++j )
		{
			const auto& inter = intersections[ j ];
			if ( !inter.valid )
			{
				continue;
			}

			const auto diff = std::fabsf( inter.fraction - trace_fraction );
			if ( diff < best_match_diff )
			{
				best_match_diff = diff;
				best_hitbox = inter.index;
			}
		}

		if ( best_hitbox < 0 )
		{
			for ( auto j = 0; j < intersection_count; ++j )
			{
				const auto& inter = intersections[ j ];
				if ( !inter.valid )
				{
					continue;
				}

				const auto diff = std::fabsf( inter.center_projection - trace_fraction );
				if ( diff < best_match_diff )
				{
					best_match_diff = diff;
					best_hitbox = inter.index;
				}
			}
		}

		if ( best_hitbox < 0 )
		{
			continue;
		}

		constexpr auto max_fraction_diff{ 0.15f };
		if ( best_match_diff > max_fraction_diff )
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
			if ( hit.exit_fraction == 1.0f )
			{
				break;
			}

			if ( hit.damage_applied < 0.03f )
			{
				return false;
			}

			out_damage = hit.damage_applied;
			return true;
		}
	}

	return false;
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
