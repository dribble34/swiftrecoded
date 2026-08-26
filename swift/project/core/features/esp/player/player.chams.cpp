#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
#include "../primitive_buffer.hpp"

namespace features::esp::player {

	bool chams::on_generate_primitives( std::uintptr_t owner_entity, std::uint32_t owner_hash, std::uintptr_t scene_object, std::uintptr_t primitive_buffer, void( __fastcall* original_fn )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t ), std::uintptr_t a1, std::uintptr_t scene_view )
	{
		const auto is_player = owner_hash == "C_CSPlayerPawn"_hash;
		const auto is_arms = owner_hash == "C_CS2HudModelArms"_hash;
		const auto is_weapon = owner_hash == "C_CS2HudModelWeapon"_hash;

		const auto is_local_attachment = [ & ]( std::uintptr_t view_pawn ) -> bool
			{
				const auto game_scene_node = memory::read<std::uintptr_t>( owner_entity + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( !game_scene_node )
				{
					return false;
				}

				const auto parent_node = memory::read<std::uintptr_t>( game_scene_node + SCHEMA( "CGameSceneNode", "m_pParent"_hash ) );
				if ( !parent_node )
				{
					return false;
				}

				const auto parent_owner = memory::read<std::uintptr_t>( parent_node + SCHEMA( "CGameSceneNode", "m_pOwner"_hash ) );
				return parent_owner == view_pawn;
			};

		const auto apply_config = [ & ]( const settings::esp::chams_config& cfg, std::uintptr_t target_scene_obj, bool force_original = false )
			{
				if ( cfg.occluded.enabled.value )
				{
					this->apply_layer( primitive_buffer, original_fn, a1, target_scene_obj, scene_view, cfg.occluded.color, cfg.occluded.material );
				}

				if ( cfg.visible.enabled.value )
				{
					this->apply_layer( primitive_buffer, original_fn, a1, target_scene_obj, scene_view, cfg.visible.color, cfg.visible.material );
				}

				if ( !cfg.visible.enabled.value && !cfg.occluded.enabled.value && force_original )
				{
					original_fn( a1, target_scene_obj, scene_view, primitive_buffer );
				}
			};

		if ( !is_player && !is_arms && !is_weapon )
		{
			if ( !settings::g_esp.m_viewmodel.weapon.enabled.value )
			{
				return false;
			}

			if ( !settings::g_misc.m_camera.thirdperson.value || !is_local_attachment( systems::g_local.get( ).view_pawn( ) ) )
			{
				return false;
			}

			apply_config( settings::g_esp.m_viewmodel.weapon, scene_object );
			return true;
		}

		if ( is_arms || is_weapon )
		{
			const auto& cfg = is_arms ? settings::g_esp.m_viewmodel.arms : settings::g_esp.m_viewmodel.weapon;
			if ( !cfg.enabled.value )
			{
				return false;
			}

			apply_config( cfg, scene_object );
			return true;
		}

		const auto local = systems::g_local.get( );
		const auto& chams_cfg = settings::g_esp.m_player.m_chams;

		const auto team = memory::read<int>( owner_entity + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto health = memory::read<int>( owner_entity + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );

		const auto is_other_team = local.is_this_other_team( team );
		const auto is_local = owner_entity == local.view_pawn( );
		const auto is_dead = health <= 0;

		const settings::esp::chams_config* target{ nullptr };

		if ( is_dead )
		{
			return false;
		}

		if ( is_local )
		{
			target = &chams_cfg.local;
		}
		else if ( is_other_team )
		{
			target = &chams_cfg.enemy;
		}
		else
		{
			target = &chams_cfg.team;
		}

		if ( !target || !target->enabled.value )
		{
			return false;
		}

		if ( !target->visible.enabled.value && !target->occluded.enabled.value )
		{
			return false;
		}

		{
			const auto flags = memory::safe_read<std::uint8_t>( scene_object + 0x78 );
			if ( flags ) {
				(void) memory::safe_write<std::uint8_t>(
					scene_object + 0x78,
					static_cast<std::uint8_t>( *flags & ~( 1u << 3 ) ) );
			}
		}

		if ( is_local )
		{
			if ( misc::g_other.is_alpha_changed( ) )
			{
				this->apply_clone( primitive_buffer, original_fn, a1, scene_object, scene_view, systems::materials::clone_type::translucent );

				if ( target->visible.enabled.value )
				{
					this->apply_overlay( primitive_buffer, original_fn, a1, scene_object, scene_view, target->visible.color, target->visible.material );
				}
			}
			else
			{
				apply_config( *target, scene_object );
			}

			return true;
		}

		apply_config( *target, scene_object );
		return true;
	}

	void chams::on_sort_primitives( std::uintptr_t entries, std::uint32_t count )
	{
		if ( !count || !entries || count > ( 1u << 20 ) )
		{
			return;
		}

		const auto overlay_mat_count = this->m_overlay_material_count.load( std::memory_order_acquire );
		if ( overlay_mat_count <= 0 )
		{
			return;
		}

		const auto total = static_cast< int >( count );
		if ( total <= 1 )
		{
			return;
		}

		std::vector<detail::mesh_primitive> sorted;
		sorted.reserve( total );

		for ( auto i = 0; i < total; ++i )
		{
			const auto primitive = memory::safe_read<detail::mesh_primitive>(
				entries + static_cast<std::size_t>( i ) * detail::primitive_size );
			if ( !primitive ) {
				return;
			}

			sorted.push_back( *primitive );
		}

		const auto overlay_begin = std::stable_partition(
			sorted.begin(), sorted.end(), [ this ]( const auto& primitive ) {
				return !this->is_overlay_material( primitive.material );
			} );
		const auto overlay_count = static_cast<int>(
			std::distance( overlay_begin, sorted.end() ) );

		if ( overlay_count <= 0 || overlay_count >= total )
		{
			return;
		}

		for ( auto i = 0; i < total; ++i )
		{
			if ( !memory::safe_write<detail::mesh_primitive>(
					entries + static_cast<std::size_t>( i ) * detail::primitive_size,
					sorted[ i ] ) ) {
				return;
			}
		}
	}

	void chams::apply_layer( std::uintptr_t primitive_buffer, void( __fastcall* original_fn )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t ), std::uintptr_t a1, std::uintptr_t scene_object, std::uintptr_t scene_view, const xdraw::color& color, settings::esp::cham_ids material_id )
	{
		const auto before = detail::read_primitive_buffer( primitive_buffer );
		const auto prev_count = before ? before->count() : -1;

		original_fn( a1, scene_object, scene_view, primitive_buffer );

		const auto after = detail::read_primitive_buffer( primitive_buffer );
		const auto new_count = after ? after->count() : -1;
		if ( !after || prev_count < 0 || prev_count >= new_count )
		{
			return;
		}

		const auto material = systems::materials::find( material_id );
		if ( !material )
		{
			return;
		}

		for ( auto i = prev_count; i < new_count; ++i )
		{
			detail::replace_primitive( after->at( i ), material, color );
		}
	}

	void chams::apply_overlay( std::uintptr_t primitive_buffer, void( __fastcall* original_fn )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t ), std::uintptr_t a1, std::uintptr_t scene_object, std::uintptr_t scene_view, const xdraw::color& color, settings::esp::cham_ids material_id )
	{
		const auto material = systems::materials::find( material_id );
		if ( !material )
		{
			return;
		}

		const auto before = detail::read_primitive_buffer( primitive_buffer );
		const auto prev_count = before ? before->count() : -1;

		original_fn( a1, scene_object, scene_view, primitive_buffer );

		const auto after = detail::read_primitive_buffer( primitive_buffer );
		const auto new_count = after ? after->count() : -1;
		if ( !after || prev_count < 0 || prev_count >= new_count )
		{
			return;
		}

		for ( auto i = prev_count; i < new_count; ++i )
		{
			detail::replace_primitive( after->at( i ), material, color );
		}

		this->add_overlay_material( material );
	}

	void chams::apply_clone( std::uintptr_t primitive_buffer, void( __fastcall* original_fn )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t ), std::uintptr_t a1, std::uintptr_t scene_object, std::uintptr_t scene_view, systems::materials::clone_type type )
	{
		const auto before = detail::read_primitive_buffer( primitive_buffer );
		const auto prev_count = before ? before->count() : -1;

		original_fn( a1, scene_object, scene_view, primitive_buffer );

		const auto after = detail::read_primitive_buffer( primitive_buffer );
		const auto new_count = after ? after->count() : -1;
		if ( !after || prev_count < 0 || prev_count >= new_count )
		{
			return;
		}

		for ( auto i = prev_count; i < new_count; ++i )
		{
			const auto primitive = after->at( i );
			const auto orig_mat = memory::safe_read<std::uintptr_t>(
				primitive + detail::primitive_material_offset );

			if ( !orig_mat || !*orig_mat )
			{
				continue;
			}

			const auto clone = systems::materials::get_or_create_clone( *orig_mat, type );
			if ( !clone )
			{
				continue;
			}

			(void) memory::safe_write<std::uintptr_t>(
				primitive + detail::primitive_material_offset, clone );
			(void) memory::safe_write<std::uintptr_t>(
				primitive + detail::primitive_material_copy_offset, clone );
		}
	}

	bool chams::is_overlay_material( std::uintptr_t mat ) const
	{
		const auto count = this->m_overlay_material_count.load( std::memory_order_acquire );

		for ( auto i = 0; i < count; ++i )
		{
			if ( this->m_overlay_materials[ i ].load( std::memory_order_relaxed ) == mat )
			{
				return true;
			}
		}

		return false;
	}

	void chams::add_overlay_material( std::uintptr_t mat )
	{
		const auto count = this->m_overlay_material_count.load( std::memory_order_acquire );

		for ( auto i = 0; i < count; ++i )
		{
			if ( this->m_overlay_materials[ i ].load( std::memory_order_relaxed ) == mat )
			{
				return;
			}
		}

		const auto idx = this->m_overlay_material_count.fetch_add( 1, std::memory_order_acq_rel );

		if ( idx < k_max_overlay_materials )
		{
			this->m_overlay_materials[ idx ].store( mat, std::memory_order_release );
		}
		else
		{
			this->m_overlay_material_count.fetch_sub( 1, std::memory_order_release );
		}
	}

} 
