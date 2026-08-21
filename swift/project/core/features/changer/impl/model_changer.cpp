#include <pch/pch.hpp>
#include "../changer.hpp"
#include <protection/patterns.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>

namespace features::changer {

	bool model_changer::ensure_initialized( )
	{
		if ( m_initialized )
		{
			return true;
		}

		logging::console::print( xs( "[model_changer] Initializing signatures..." ) );

		// Get ResourceSystem interface properly via CreateInterface
		m_resource_system = memory::get_module_interface( "resourcesystem.dll:ResourceSystem013" );
		if ( !m_resource_system )
		{
			logging::console::print( xs( "[model_changer] Failed to get ResourceSystem013 interface" ) );
			return false;
		}

		m_precache_fn = reinterpret_cast< void* >( memory::resolve_pattern( "resourcesystem.dll:405355574881EC80000000488B01498BE8488BFA" ) );
		m_set_model_fn = reinterpret_cast< void* >( memory::resolve_pattern( "client.dll:40534883EC?488BD94C8BC2488B0D????????488D5424" ) );
		m_cbuffer_insert_fn = reinterpret_cast< void* >( memory::get_module_export( "tier0.dll:?Insert@CBufferString@@QEAAPEBDHPEBDH_N@Z" ) );

		if ( m_resource_system && m_precache_fn && m_set_model_fn && m_cbuffer_insert_fn )
		{
			m_initialized = true;
			logging::console::print( xs( "[model_changer] Initialization succeeded" ) );
		}
		else
		{
			logging::console::print( xs( "[model_changer] Initialization FAILED | resource_system={:p} precache_fn={:p} set_model_fn={:p} cbuffer_insert_fn={:p}" ),
				m_resource_system, m_precache_fn, m_set_model_fn, m_cbuffer_insert_fn );
		}

		return m_initialized;
	}

	void model_changer::on_frame_stage_notify( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) || !local.is_alive )
		{
			return;
		}

		if ( !ensure_initialized( ) )
		{
			return;
		}

		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );

		for ( const auto& p : players )
		{
			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}

			const auto pawn_handle = memory::read< std::uint32_t >( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( !pawn || pawn == local.pawn )
			{
				continue;
			}

			const auto team = memory::read< int >( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( team != 2 && team != 3 )
			{
				continue;
			}

			apply_model( pawn, team );
		}
	}

	bool model_changer::precache( const std::string& path )
	{
		return precache_model( path );
	}

	bool model_changer::precache_model( const std::string& path )
	{
		if ( !ensure_initialized( ) || !m_resource_system || !m_precache_fn || !m_cbuffer_insert_fn )
		{
			logging::console::print( xs( "[model_changer] Precache failed - not initialized" ) );
			return false;
		}

		struct CBufferString
		{
			int m_nLength{ 0 };
			int m_nAllocatedSize{ static_cast< int >( 0x80000000 | 0x40000000 | 8 ) };
			union
			{
				char* m_pString;
				char m_szString[ 8 ];
			};

			const char* ( __fastcall* fnInsert )( void*, int, const char*, int, bool );

			const char* Insert( int index, const char* str, int length, bool b1 )
			{
				return fnInsert( this, index, str, length, b1 );
			}
		};

		using fnPrecache_t = void( __fastcall* )( std::uintptr_t, CBufferString*, const char* );
		auto precache = reinterpret_cast< fnPrecache_t >( m_precache_fn );

		CBufferString names{};
		names.fnInsert = reinterpret_cast< decltype( CBufferString::fnInsert ) >( m_cbuffer_insert_fn );

		names.Insert( 0, path.c_str( ), -1, false );

		precache( m_resource_system, &names, "" );

		logging::console::print( xs( "[model_changer] Precached model: {}" ), path );

		return true;
	}

	bool model_changer::set_model( std::uintptr_t pawn, const std::string& path )
	{
		if ( !m_set_model_fn )
		{
			logging::console::print( xs( "[model_changer] SetModel failed - not initialized" ) );
			return false;
		}

		using fnSetModel_t = void( __fastcall* )( std::uintptr_t, const char* );
		auto set_model = reinterpret_cast< fnSetModel_t >( m_set_model_fn );

		set_model( pawn, path.c_str( ) );

		logging::console::print( xs( "[model_changer] SetModel on pawn {:x}: {}" ), pawn, path );
		return true;
	}

	void model_changer::apply_model( std::uintptr_t pawn, int team )
	{
		auto& models_cfg = settings::g_changer.models;
		const auto ct_source = static_cast< settings::changer::model_changer_field::model_source >( models_cfg.ct_source.value );
		const auto t_source = static_cast< settings::changer::model_changer_field::model_source >( models_cfg.t_source.value );
		const auto& ct_custom = models_cfg.ct_custom.value;
		const auto& t_custom = models_cfg.t_custom.value;

		std::string model_path;
		if ( team == 3 )
		{
			if ( ct_source == settings::changer::model_changer_field::model_source::disabled )
				return;
			else if ( ct_source == settings::changer::model_changer_field::model_source::custom )
				model_path = ct_custom;
			else if ( ct_source == settings::changer::model_changer_field::model_source::ct )
				model_path = "models/player/custom_player/ctm/ctm_gsg9.vmdl";
			else if ( ct_source == settings::changer::model_changer_field::model_source::t )
				model_path = "models/player/custom_player/tm/tm_phoenix.vmdl";
		}
		else if ( team == 2 )
		{
			if ( t_source == settings::changer::model_changer_field::model_source::disabled )
				return;
			else if ( t_source == settings::changer::model_changer_field::model_source::custom )
				model_path = t_custom;
			else if ( t_source == settings::changer::model_changer_field::model_source::ct )
				model_path = "models/player/custom_player/ctm/ctm_gsg9.vmdl";
			else if ( t_source == settings::changer::model_changer_field::model_source::t )
				model_path = "models/player/custom_player/tm/tm_phoenix.vmdl";
		}

		if ( model_path.empty( ) )
		{
			return;
		}

		auto& models = ( team == 3 ) ? m_ct_models : m_t_models;
		auto it = models.find( pawn );

		if ( it == models.end( ) )
		{
			const auto game_scene_node = memory::read< std::uintptr_t >( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( game_scene_node )
			{
				const auto model_state = game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash );
				const auto model_handle = memory::read< std::uintptr_t >( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
				if ( model_handle )
				{
					const auto model_name = memory::read< std::uintptr_t >( model_handle + 0x10 );
					if ( model_name )
					{
						m_original_models[ pawn ] = memory::read_string( model_name, 256 );
					}
				}
			}

			if ( !precache_model( model_path ) )
			{
				return;
			}

			models[ pawn ] = { model_path, true };
		}

		if ( it == models.end( ) )
		{
			it = models.find( pawn );
		}

		if ( it != models.end( ) && it->second.precached )
		{
			set_model( pawn, it->second.path );
		}
	}

	void model_changer::restore_model( std::uintptr_t pawn, int team )
	{
		auto it = m_original_models.find( pawn );
		if ( it != m_original_models.end( ) )
		{
			set_model( pawn, it->second );
			m_original_models.erase( it );
		}

		auto& models = ( team == 3 ) ? m_ct_models : m_t_models;
		models.erase( pawn );
	}

	void model_changer::reset_cache( )
	{
		m_ct_models.clear( );
		m_t_models.clear( );
		m_original_models.clear( );
	}

} // namespace features::changer