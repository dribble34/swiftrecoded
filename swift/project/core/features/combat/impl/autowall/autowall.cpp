#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/tls/dynamic_tls.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

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

	} // namespace detail

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

	shared::penetration::run_context shared::penetration::prepare_target( std::uintptr_t target_pawn, lagcomp::record* record ) const
	{
		run_context ctx{};
		this->prepare_target( target_pawn, record, ctx );
		return ctx;
	}

	void shared::penetration::prepare_target( std::uintptr_t target_pawn, lagcomp::record* record, run_context& out ) const
	{
		auto& ctx = out;
		ctx = {};
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
			.ct_head = CONVAR ("mp_damage_scale_ct_head")->get<float>( ),
			.t_head = CONVAR ("mp_damage_scale_t_head")->get<float>( ),
			.ct_body = CONVAR ("mp_damage_scale_ct_body")->get<float>( ),
			.t_body = CONVAR ("mp_damage_scale_t_body")->get<float>( )
		};

		ctx.armor_ratio = this->m_weapon_data.armor_ratio;
		ctx.headshot_multiplier = this->m_weapon_data.headshot_multiplier;
	}

	bool shared::penetration::run( const math::vector3& start, const math::vector3& end, const run_context& ctx, std::uintptr_t local_pawn, int local_team, result& out ) const
	{
		if ( this->m_weapon_data.damage <= 0.0f )
		{
			return false;
		}

		const auto direction = ( end - start ).normalized( );
		const auto trace_delta = direction * this->m_weapon_data.range;

		auto filter = systems::g_tracing.make_filter( local_pawn, 0x1c300b, 3, 15 );

		static tls::dynamic_tls<systems::tracing::trace_data> trace_storage_slot{};
		auto& trace_storage = trace_storage_slot.get( );
		trace_storage = {};
		auto* trace = &trace_storage;
		trace->array_pointer = &trace->elements;
		trace->hit_array_pointer = &trace->hit_elements;

		g_shared.m_current_autowall_record.get( ) = ctx.record;
		g_shared.m_autowalling.get( ) = true;

		systems::g_tracing.setup_trace( trace, start, trace_delta, filter, 4, true );

		g_shared.m_autowalling.get( ) = false;
		g_shared.m_current_autowall_record.get( ) = nullptr;

		const auto num_hits = trace->num_hits;
		const auto hit_array = reinterpret_cast< std::uintptr_t >( trace->hit_array_pointer );

		if ( num_hits <= 0 )
		{
			out = {};
			return false;
		}

		const auto surface_array = reinterpret_cast< std::uintptr_t >( trace->array_pointer );

		memory::call<void> (PATTERN (patterns::trace_bullet), trace, this->m_weapon_data.damage, this->m_weapon_data.penetration, this->m_weapon_data.range_modifier, 4, local_team, static_cast<std::uintptr_t>(0));

		auto actual_hitbox{ -1 };
		auto closest_hitbox_fraction{ 1.0f };
		auto closest_hitbox_projection{ 1.0f };
		auto fallback_hitbox{ -1 };
		if ( ctx.record )
		{
			const auto ray_direction = direction.normalized( );
			for ( const auto& hitbox : ctx.hitboxes )
			{
				if ( hitbox.bone < 0 || hitbox.bone >= ctx.record->bone_count )
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
					intersects = g_shared.ray_vs_capsule( start, trace_delta, capsule_start, capsule_end, hitbox.radius, fraction );
				}
				else
				{
					auto inverse = bone.rotation;
					inverse.x = -inverse.x;
					inverse.y = -inverse.y;
					inverse.z = -inverse.z;

					const auto local_origin = inverse.rotate_vector( start - bone.position );
					const auto local_delta = inverse.rotate_vector( trace_delta );
					auto entry{ 0.0f };
					auto exit{ 1.0f };

					const auto intersect_axis = [ & ]( float origin, float delta, float minimum, float maximum )
						{
							if ( std::fabsf( delta ) < 1.0e-8f )
							{
								return origin >= minimum && origin <= maximum;
							}

							auto first = ( minimum - origin ) / delta;
							auto second = ( maximum - origin ) / delta;
							if ( first > second ) std::swap( first, second );
							entry = std::max( entry, first );
							exit = std::min( exit, second );
							return entry <= exit;
						};

					intersects = intersect_axis( local_origin.x, local_delta.x, hitbox.mins.x, hitbox.maxs.x ) &&
						intersect_axis( local_origin.y, local_delta.y, hitbox.mins.y, hitbox.maxs.y ) &&
						intersect_axis( local_origin.z, local_delta.z, hitbox.mins.z, hitbox.maxs.z );
					fraction = entry;
				}

				if ( intersects && fraction < closest_hitbox_fraction )
				{
					closest_hitbox_fraction = fraction;
					actual_hitbox = hitbox.index;
				}

				const auto center = bone.rotation.rotate_vector( ( hitbox.mins + hitbox.maxs ) * 0.5f ) + bone.position;
				const auto projection = ( center - start ).dot( ray_direction ) / trace_delta.length( );
				if ( projection >= 0.0f && projection < closest_hitbox_projection )
				{
					closest_hitbox_projection = projection;
					fallback_hitbox = hitbox.index;
				}
			}
		}

		if ( actual_hitbox < 0 && fallback_hitbox >= 0 )
		{
			actual_hitbox = fallback_hitbox;
		}

		auto penetrated{ false };

		for ( auto i = 0; i < num_hits; ++i )
		{
			auto hit = reinterpret_cast< detail::bullet_trace_record* >( hit_array + i * sizeof( detail::bullet_trace_record ) );
			const auto damage = *reinterpret_cast< float* >( reinterpret_cast< std::uintptr_t >( hit ) + 8 );

			if ( damage <= 0.0f )
			{
				break;
			}

			if ( ( hit->can_penetrate & 1 ) != 0 )
			{
				penetrated = true;

				if ( *reinterpret_cast< float* >( reinterpret_cast< std::uintptr_t >( hit ) + 4 ) == 1.0f )
				{
					break;
				}

				continue;
			}

			const auto trace_holder = surface_array + sizeof( systems::tracing::trace_array_element ) * ( hit->enter_contact_ix & 0x7fff );
			const auto hit_handle = memory::read<std::uint32_t>( trace_holder + 0x2c );
			const auto hit_entity = systems::g_entities.lookup( hit_handle );

			if ( !hit_entity || hit_entity != ctx.target_pawn )
			{
				continue;
			}

			int hitbox_to_use = actual_hitbox;
			if ( hitbox_to_use < 0 )
			{
				hitbox_to_use = fallback_hitbox;
			}
			if ( hitbox_to_use < 0 )
			{
				continue;
			}

			out.hitbox = hitbox_to_use;
			out.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( actual_hitbox );
			out.penetrated = penetrated;
			out.damage = damage;

			this->scale_damage( out.hitgroup, ctx.target_armor, ctx.has_helmet, ctx.target_team, ctx.armor_ratio, ctx.headshot_multiplier, ctx.scales, out.damage );

			return true;
		}

		out = {};
		return false;
	}

	bool shared::penetration::can( const math::vector3& start, const math::vector3& direction, float& out_damage, const systems::local::snapshot& local ) const
	{
		out_damage = 0.0f;

		if ( this->m_weapon_data.damage <= 0.0f || this->m_weapon_data.penetration <= 0.0f )
		{
			return false;
		}

		const auto local_team = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto trace_delta = direction * this->m_weapon_data.range;

		auto filter = systems::g_tracing.make_filter( local.pawn, 0x1c300b, 3, 15 );
		static tls::dynamic_tls<systems::tracing::trace_data> trace_storage_slot{};
		auto& trace_storage = trace_storage_slot.get( );
		trace_storage = {};
		auto* trace = &trace_storage;
		trace->array_pointer = &trace->elements;
		trace->hit_array_pointer = &trace->hit_elements;

		systems::g_tracing.setup_trace( trace, start, trace_delta, filter, 4, true );

		const auto num_hits = trace->num_hits;

		if ( num_hits <= 0 )
		{
			return false;
		}

		const auto hit_array = reinterpret_cast< std::uintptr_t >( trace->hit_array_pointer );

		memory::call<void> (PATTERN (patterns::trace_bullet), trace, this->m_weapon_data.damage, this->m_weapon_data.penetration, this->m_weapon_data.range_modifier, 4, local_team, static_cast<std::uintptr_t>(0));

		for ( auto i = 0; i < num_hits; ++i )
		{
			auto hit = reinterpret_cast< detail::bullet_trace_record* >( hit_array + i * sizeof( detail::bullet_trace_record ) );
			const auto damage = hit->damage_applied;

			if ( damage <= 0.0f )
			{
				break;
			}

			if ( ( hit->can_penetrate & 1 ) != 0 )
			{
				// Pure "can this shot get through the wall" check: don't
				// treat "this was the last recorded surface" as blocked --
				// that's just as true for a bare wall with nothing behind
				// it as it is for one that fully absorbed the shot, and the
				// two need to be told apart by can_penetrate, not by
				// whether anything further was in the trace.
				out_damage = damage;
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
			.ct_head = CONVAR ("mp_damage_scale_ct_head")->get<float> (),
			.t_head = CONVAR ("mp_damage_scale_t_head")->get<float> (),
			.ct_body = CONVAR ("mp_damage_scale_ct_body")->get<float> (),
			.t_body = CONVAR ("mp_damage_scale_t_body")->get<float> ()
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

} // namespace features::combat
