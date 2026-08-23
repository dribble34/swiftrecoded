#include <pch/pch.hpp>

#include "changer.hpp"

#include <utilities/fnv1a.hpp>
#include <utilities/game_path.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>

namespace {

	struct cbuffer_string
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

	using fn_precache_t = void( __fastcall* )( std::uintptr_t, cbuffer_string*, const char* );
	using fn_set_model_t = void( __fastcall* )( std::uintptr_t, const char* );

	std::uintptr_t g_resource_system{};
	void* g_precache_fn{};
	void* g_set_model_fn{};
	void* g_cbuffer_insert_fn{};
	bool g_initialized{};

	bool ensure_initialized( )
	{
		if ( g_initialized )
		{
			return true;
		}

		logging::console::print( xs( "[ModelChanger] Initializing signatures..." ) );

		// Get ResourceSystem interface properly via CreateInterface
		g_resource_system = memory::get_module_interface( "resourcesystem.dll:ResourceSystem013" );
		if ( !g_resource_system )
		{
			logging::console::print( xs( "[ModelChanger] Failed to get ResourceSystem013 interface" ) );
			return false;
		}

		// Precache function pattern from project's patterns.cpp
		g_precache_fn = reinterpret_cast< void* >(
			memory::resolve_pattern( "resourcesystem.dll:405355574881EC80000000488B01498BE8488BFA" ) );

		// SetModel pattern from cs2-sdk.com (client.dll RVA 0x922120)
		g_set_model_fn = reinterpret_cast< void* >(
			memory::resolve_pattern( "client.dll:40534883EC?488BD94C8BC2488B0D????????488D5424" ) );

		// CBufferString::Insert export
		g_cbuffer_insert_fn = reinterpret_cast< void* >(
			memory::resolve_pattern( "tier0.dll:?Insert@CBufferString@@QEAAPEBDHPEBDH_N@Z" ) );

		g_initialized = g_resource_system && g_precache_fn && g_set_model_fn && g_cbuffer_insert_fn;

		logging::console::print( xs( "[ModelChanger] Initialization {} | resource_system={:p} precache_fn={:p} set_model_fn={:p} cbuffer_insert_fn={:p}" ),
			g_initialized ? "succeeded" : "FAILED", g_resource_system, g_precache_fn, g_set_model_fn, g_cbuffer_insert_fn );

		if ( !g_initialized )
		{
			if ( !g_precache_fn ) logging::console::print( xs( "[ModelChanger] Missing precache_fn" ) );
			if ( !g_set_model_fn ) logging::console::print( xs( "[ModelChanger] Missing set_model_fn" ) );
			if ( !g_cbuffer_insert_fn ) logging::console::print( xs( "[ModelChanger] Missing cbuffer_insert_fn" ) );
		}

		return g_initialized;
	}

	bool precache_model( const std::string& path )
	{
		if ( !ensure_initialized( ) )
		{
			logging::console::print( xs( "[ModelChanger] Precache failed - not initialized" ) );
			return false;
		}

		cbuffer_string names{};
		names.fnInsert = reinterpret_cast< decltype( cbuffer_string::fnInsert ) >( g_cbuffer_insert_fn );
		names.Insert( 0, path.c_str( ), -1, false );

		auto precache = reinterpret_cast< fn_precache_t >( g_precache_fn );
		precache( g_resource_system, &names, "" );

		logging::console::print( xs( "[ModelChanger] Precached model: {}" ), path );

		return true;
	}

	bool set_model( std::uintptr_t pawn, const std::string& path )
	{
		if ( !ensure_initialized( ) || !g_set_model_fn )
		{
			logging::console::print( xs( "[ModelChanger] SetModel failed - not initialized" ) );
			return false;
		}

		auto set = reinterpret_cast< fn_set_model_t >( g_set_model_fn );
		set( pawn, path.c_str( ) );

		logging::console::print( xs( "[ModelChanger] SetModel on pawn {:x}: {}" ), pawn, path );

		return true;
	}

} // namespace

void CModelChanger::UpdateWeaponModels( )
{
	std::vector<Model_t> result;

	const auto dir = game_path::csgo_directory( );
	if ( !dir )
	{
		logging::console::print( xs( "[ModelChanger] UpdateWeaponModels: csgo_directory not found" ) );
		vecWeaponModels = std::move( result );
		return;
	}

	std::error_code error;
	auto models_base = *dir;
	if ( !std::filesystem::is_directory( models_base / "weapons", error ) )
	{
		error.clear( );
		models_base = models_base / "csgo";
	}

	const auto models_dir = models_base / "weapons";
	if ( !std::filesystem::exists( models_dir, error ) )
	{
		logging::console::print( xs( "[ModelChanger] Weapons dir not found: {}" ), models_dir.string( ) );
		vecWeaponModels = std::move( result );
		return;
	}

	for ( const auto& entry : std::filesystem::recursive_directory_iterator( models_dir, error ) )
	{
		if ( error || !entry.is_regular_file( error ) )
		{
			error.clear( );
			continue;
		}

		const auto file_name = entry.path( ).filename( ).string( );
		if ( file_name.size( ) < 6 )
		{
			continue;
		}

		if ( file_name.substr( file_name.size( ) - 7 ) != ".vmdl_c" )
		{
			continue;
		}

		auto model_path = entry.path( ).string( );
		model_path = model_path.substr( 0, model_path.size( ) - 2 );
		std::replace( model_path.begin( ), model_path.end( ), '\\', '/' );

		const auto pos = model_path.find( "weapons/" );
		if ( pos != std::string::npos )
		{
			model_path = model_path.substr( pos );
		}

		Model_t model{};
		model.strModelName = file_name.substr( 0, file_name.size( ) - 8 );
		model.strModelPath = model_path;
		result.push_back( std::move( model ) );
	}

	logging::console::print( xs( "[ModelChanger] Found {} weapon models" ), result.size( ) );
	vecWeaponModels = std::move( result );
}

void CModelChanger::UpdatePlayerModels( )
{
	std::vector<Model_t> result;

	const auto dir = game_path::csgo_directory( );
	if ( !dir )
	{
		logging::console::print( xs( "[ModelChanger] UpdatePlayerModels: csgo_directory not found" ) );
		vecPlayerModels = std::move( result );
		return;
	}

	std::error_code error;
	auto models_base = *dir;
	if ( !std::filesystem::is_directory( models_base / "characters" / "models", error ) )
	{
		error.clear( );
		models_base = models_base / "csgo";
	}

	const auto models_dir = models_base / "characters" / "models";
	if ( !std::filesystem::exists( models_dir, error ) )
	{
		logging::console::print( xs( "[ModelChanger] Player models dir not found: {}" ), models_dir.string( ) );
		vecPlayerModels = std::move( result );
		return;
	}

	for ( const auto& entry : std::filesystem::recursive_directory_iterator( models_dir, error ) )
	{
		if ( error || !entry.is_regular_file( error ) )
		{
			error.clear( );
			continue;
		}

		if ( entry.path( ).extension( ).string( ) != ".vmdl_c" )
		{
			continue;
		}

		const auto file_name = entry.path( ).filename( ).string( );
		if ( file_name.find( "arm" ) != std::string::npos )
		{
			continue;
		}

		auto model_path = entry.path( ).string( );
		model_path = model_path.substr( 0, model_path.size( ) - 2 );
		std::replace( model_path.begin( ), model_path.end( ), '\\', '/' );

		const auto pos = model_path.find( "characters/models/" );
		if ( pos != std::string::npos )
		{
			model_path = model_path.substr( pos );
		}

		Model_t model{};
		model.strModelName = file_name.substr( 0, file_name.size( ) - 8 );
		model.strModelPath = model_path;
		result.push_back( std::move( model ) );
	}

	logging::console::print( xs( "[ModelChanger] Found {} player models" ), result.size( ) );
	vecPlayerModels = std::move( result );
}

void CModelChanger::UpdateCustomAgentModels( )
{
	std::vector<Model_t> result;

	const auto dir = game_path::csgo_directory( );
	if ( !dir )
	{
		logging::console::print( xs( "[ModelChanger] UpdateCustomAgentModels: csgo_directory not found" ) );
		vecCustomAgentModels = std::move( result );
		return;
	}

	std::error_code error;
	const auto models_dir = *dir / "characters" / "models";
	if ( !std::filesystem::is_directory( models_dir, error ) )
	{
		logging::console::print( xs( "[ModelChanger] models dir not found: {}" ), models_dir.string( ) );
		vecCustomAgentModels = std::move( result );
		return;
	}

	for ( const auto& entry : std::filesystem::recursive_directory_iterator( models_dir, error ) )
	{
		if ( error || !entry.is_regular_file( error ) )
		{
			error.clear( );
			continue;
		}

		if ( entry.path( ).extension( ).string( ) != ".vmdl_c" )
		{
			continue;
		}

		auto model_path = entry.path( ).string( );
		model_path = model_path.substr( 0, model_path.size( ) - 2 );
		std::replace( model_path.begin( ), model_path.end( ), '\\', '/' );

		const auto pos = model_path.find( "characters/models/" );
		if ( pos == std::string::npos )
		{
			continue;
		}

		Model_t model{};
		model.strModelName = entry.path( ).stem( ).string( );
		model.strModelPath = model_path.substr( pos );
		result.push_back( std::move( model ) );
	}

	logging::console::print( xs( "[ModelChanger] Found {} custom agent models" ), result.size( ) );
	vecCustomAgentModels = std::move( result );
}

bool CModelChanger::SetPlayerModel( )
{
	const auto local = systems::g_local.get( );
	if ( !local.is_valid( ) || !local.is_alive )
	{
		return false;
	}

	if ( !bPlayerModelChanger )
	{
		return false;
	}

	if ( nSelectedPlayerModel == ~1U )
	{
		return false;
	}

	if ( vecPlayerModels.size( ) <= nSelectedPlayerModel )
	{
		return false;
	}

	const auto& model = vecPlayerModels[ nSelectedPlayerModel ];
	if ( model.strModelPath.empty( ) )
	{
		return false;
	}

	const auto model_hash = fnv1a::runtime_hash( model.strModelPath.c_str( ) );
	if ( uLastPlayerModelHash == model_hash )
	{
		return true;
	}

	if ( !precache_model( model.strModelPath ) )
	{
		return false;
	}

	uLastPlayerModelHash = model_hash;
	set_model( local.pawn, model.strModelPath );

	return true;
}