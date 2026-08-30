#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

#include <unordered_set>

namespace features::combat {

	bool shared::lagcomp::record::setup( std::uintptr_t pawn_ptr )
	{
		this->pawn = pawn_ptr;
		this->game_scene_node = memory::read<std::uintptr_t>( pawn_ptr + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );

		if ( !this->game_scene_node )
		{
			return false;
		}

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return false;
		}

		this->bone_count = memory::read<int>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x8c );
		if ( this->bone_count <= 0 )
		{
			return false;
		}
		this->bone_count = std::min( this->bone_count, 128 );

		const auto abs_origin = memory::read<math::vector3>( this->game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto abs_rotation = memory::read<math::vector3>( this->game_scene_node + SCHEMA( "CGameSceneNode", "m_angAbsRotation"_hash ) );
		if ( !std::isfinite( abs_origin.x ) || !std::isfinite( abs_origin.y ) || !std::isfinite( abs_origin.z ) )
		{
			return false;
		}

		this->origin = abs_origin;
		this->rotation = abs_rotation;

		this->simulation_time = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flSimulationTime"_hash ) );

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		if ( !global_vars )
		{
			return false;
		}

		const auto backup_current_time = memory::read<float>( global_vars + 0x30 );
		const auto backup_tick_count = memory::read<int>( global_vars + 0x44 );
		memory::write<float>( global_vars + 0x30, this->simulation_time );
		memory::write<int>( global_vars + 0x44, cstypes::time_to_ticks( this->simulation_time ) );

		memory::call<void>( PATTERN( patterns::game_scene_node_set_mesh_group ), this->game_scene_node, 0xfffff );
		memory::call<void>( PATTERN( patterns::game_scene_node_set_skeleton ), this->game_scene_node, 0x100 );

		memory::write<int>( global_vars + 0x44, backup_tick_count );
		memory::write<float>( global_vars + 0x30, backup_current_time );

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return false;
		}

		std::memcpy( this->bones, reinterpret_cast< void* >( this->bone_cache ), sizeof( systems::bones::data ) * this->bone_count );

		this->tick = cstypes::time_to_ticks( this->simulation_time );
		this->valid = true;

		return true;
	}

	bool shared::lagcomp::record::is_valid( ) const
	{
		if ( !this->valid )
		{
			return false;
		}

		const auto local = systems::g_local.get( );
		const auto local_pawn = local.pawn;
		const auto net_channel = memory::call<std::uintptr_t>( PATTERN( patterns::get_net_channel ), 0, 0 );
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );

		if ( !local_pawn || !net_channel || !global_vars )
		{
			return false;
		}

		const auto max_unlag = [ ]
		{
			const auto server_limit = CONVAR( "sv_maxunlag" )->get<float>( );
			const auto player_limit = CONVAR( "sv_maxunlag_player" )->get<float>( );
			return player_limit > 0.0f ? std::min( server_limit, player_limit ) : server_limit;
		}( );

		const auto current_time = memory::read<float>( global_vars + 0x30 );

		// vfunc 10 is the engine's latency accessor (float, seconds). m_iPing on
		// the controller is the same value in ms; take the larger of the two.
		const auto channel_latency = memory::call_vfunc<float>( net_channel, 10, 0 );

		const auto reported_ping = local.controller
			? memory::read<int>( local.controller + SCHEMA( "CCSPlayerController", "m_iPing"_hash ) )
			: 0;

		const auto ping_latency = reported_ping > 0
			? static_cast< float >( reported_ping ) / 1000.0f
			: 0.0f;

		const auto measured = std::max( std::isfinite( channel_latency ) ? channel_latency : 0.0f, ping_latency );

		if ( !std::isfinite( max_unlag ) || !std::isfinite( current_time ) || !std::isfinite( measured ) )
		{
			return false;
		}

		// m_iPing is smoothed and refreshed periodically, so it can sit below
		// the real latency. underestimating costs the shot, overestimating only
		// costs range -- so hold the peak and decay it slowly.
		// only needs to cover sub-tick drift, not spikes -- the peak-hold does
		// those. at 15ms the budget sat on top of the common 0.1562 record age
		// and rejected a quarter of them.
		constexpr auto jitter_margin{ 0.005f };
		constexpr auto decay_per_second{ 0.5f };

		static float held_latency{};
		static float held_time{};

		if ( measured > held_latency || current_time < held_time )
		{
			held_latency = measured;
		}
		else
		{
			const auto elapsed = current_time - held_time;
			if ( elapsed > 0.0f && elapsed < 1.0f )
			{
				held_latency -= ( held_latency - measured ) * std::min( elapsed * decay_per_second, 1.0f );
			}
			else if ( elapsed >= 1.0f )
			{
				held_latency = measured;
			}
		}

		held_time = current_time;

		const auto latency = std::max( held_latency, 0.0f ) + jitter_margin;

		const auto budget = max_unlag - latency;
		const auto correct = std::max( measured, 0.0f );

		return budget > 0.0f
			&& this->simulation_time >= current_time - budget
			&& this->simulation_time <= current_time + correct;
	}

	void shared::lagcomp::record::apply( )
	{
		if ( !this->valid || this->is_applied || !this->game_scene_node )
		{
			return;
		}

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return;
		}

		const auto size = sizeof( systems::bones::data ) * this->bone_count;
		std::memcpy( this->bones_backup, reinterpret_cast< void* >( this->bone_cache ), size );
		std::memcpy( reinterpret_cast< void* >( this->bone_cache ), this->bones, size );

		this->is_applied = true;
	}

	void shared::lagcomp::record::restore( )
	{
		if ( !this->valid || !this->is_applied || !this->bone_cache )
		{
			return;
		}

		const auto size = sizeof( systems::bones::data ) * this->bone_count;
		std::memcpy( reinterpret_cast< void* >( this->bone_cache ), this->bones_backup, size );

		this->is_applied = false;
	}

	void shared::lagcomp::run( )
	{
		std::unique_lock records_lock( this->m_records_mtx );

		const auto local = systems::g_local.get( );
		if ( !local.is_alive )
		{
			this->m_records.clear( );
			return;
		}

		std::unordered_set<std::uintptr_t> active{};

		for ( const auto& p : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}

			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( !pawn || pawn == local.pawn )
			{
				continue;
			}

			const auto team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			active.insert( pawn );
		}

		std::erase_if( this->m_records, [ & ]( const auto& pair ) { return !active.contains( pair.first ); } );

		struct pending_record
		{
			std::uintptr_t pawn{};
			int simulation_tick{};
		};

		std::vector<pending_record> pending;
		pending.reserve( active.size( ) );

		for ( const auto& pawn : active )
		{
			const auto health = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
			{
				this->m_records.erase( pawn );
				continue;
			}

			auto& records = this->m_records[ pawn ];
			const auto simulation_time = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flSimulationTime"_hash ) );
			const auto simulation_tick = cstypes::time_to_ticks( simulation_time );

			// sim time went backwards — teleport/respawn, history is garbage
			if ( !records.empty( ) && simulation_tick < records.front( ).tick - 1 )
			{
				records.clear( );
			}

			if ( records.empty( ) || simulation_tick > records.front( ).tick )
			{
				pending.push_back( { pawn, simulation_tick } );
			}

			while ( !records.empty( ) && !records.back( ).is_valid( ) )
			{
				records.pop_back( );
			}

			while ( records.size( ) > rage::k_max_lagcomp_records )
			{
				records.pop_back( );
			}
		}

		if ( pending.empty( ) )
		{
			return;
		}

		for ( auto& p : pending )
		{
			record rec{};

			if ( rec.setup( p.pawn ) )
			{
				this->m_records[ p.pawn ].emplace_front( std::move( rec ) );
			}
		}

		for ( auto& [pawn, records] : this->m_records )
		{
			for ( auto& rec : records )
			{
				rec.was_valid = rec.is_valid( );
			}
		}
	}

	shared::lagcomp::record* shared::lagcomp::get_oldest_valid( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return nullptr;
		}

		const auto max_ticks = std::clamp( settings::g_combat.m_lagcomp.max_backtrack_ticks.value, 1, static_cast< int >( rage::k_max_lagcomp_records ) );
		const auto controller = systems::g_local.get( ).controller;
		const auto current_tick = controller ? memory::read<int>( controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) ) : 0;

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( !rit->is_valid( ) )
			{
				continue;
			}

			// same freshness rule as get_valid_records — stale records miss
			if ( current_tick && ( current_tick - rit->tick ) > max_ticks )
			{
				return nullptr;
			}

			return &( *rit );
		}

		return nullptr;
	}

	shared::lagcomp::record* shared::lagcomp::get_oldest_was_valid( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return nullptr;
		}

		const auto max_ticks = std::clamp( settings::g_combat.m_lagcomp.max_backtrack_ticks.value, 1, static_cast< int >( rage::k_max_lagcomp_records ) );
		const auto controller = systems::g_local.get( ).controller;
		const auto current_tick = controller ? memory::read<int>( controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) ) : 0;

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( !rit->was_valid )
			{
				continue;
			}

			if ( current_tick && ( current_tick - rit->tick ) > max_ticks )
			{
				return nullptr;
			}

			return &( *rit );
		}

		return nullptr;
	}

	std::optional<shared::lagcomp::visual_record> shared::lagcomp::get_oldest_was_valid_visual( std::uintptr_t pawn ) const
	{
		std::shared_lock records_lock( this->m_records_mtx );

		const auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) )
		{
			return std::nullopt;
		}

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( !rit->was_valid )
			{
				continue;
			}

			visual_record out{};
			out.origin = rit->origin;
			for ( auto i = 0; i < 27; ++i )
			{
				out.bones[ i ] = rit->bones[ i ];
			}

			return out;
		}

		return std::nullopt;
	}

	std::vector<shared::lagcomp::record*> shared::lagcomp::get_valid_records( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		std::vector<record*> result;

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) )
		{
			return result;
		}

		result.reserve( it->second.size( ) );

		for ( auto& rec : it->second )
		{
			if ( rec.is_valid( ) )
			{
				result.push_back( &rec );
			}
		}

		if ( result.empty( ) )
		{
			return result;
		}

		const auto max_ticks = std::clamp( settings::g_combat.m_lagcomp.max_backtrack_ticks.value, 1, static_cast< int >( rage::k_max_lagcomp_records ) );
		const auto newest_tick = result.front( )->tick;

		// also gate against the live tick — a server hitch can leave the whole
		// deque stale, firing at those is a guaranteed miss
		const auto controller = systems::g_local.get( ).controller;
		const auto current_tick = controller ? memory::read<int>( controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) ) : newest_tick;

		result.erase(
			std::remove_if( result.begin( ), result.end( ), [ newest_tick, current_tick, max_ticks ]( const record* rec )
				{
					return ( newest_tick - rec->tick ) > max_ticks
						|| ( current_tick - rec->tick ) > max_ticks;
				} ),
			result.end( )
		);

		return result;
	}

	std::array<systems::bones::data, 27> shared::lagcomp::get_skeleton( const record& record ) const
	{
		std::array<systems::bones::data, 27> skeleton;

		if ( record.valid )
		{
			for ( auto i = 0; i < 27; ++i )
			{
				skeleton[ i ] = record.bones[ i ];
			}
		}

		return skeleton;
	}

}

