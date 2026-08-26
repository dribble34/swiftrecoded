#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "nlohmann/json.hpp"
#include "lz4/lz4.h"
#include "xdraw/xui/xui.hpp"

#include <filesystem>
#include <fstream>
#include <cstring>

namespace config {

	enum class field_type : std::uint8_t
	{
		setting,
		bool_val,
		int_val,
		uint8_val,
		float_val,
		color,
		float3,
		bool_array,
		string_val,
		custom
	};

	struct float3
	{
		float x{}, y{}, z{};
	};

	struct field
	{
		std::uint32_t key;
		field_type type;
		void* ptr;
		std::uint32_t count;
	}; 
	
	struct custom_field
	{
		virtual ~custom_field( ) = default;
		virtual nlohmann::json serialize( ) const = 0;
		virtual void deserialize( const nlohmann::json& j ) = 0;
	};

	namespace detail {

		struct config_registry
		{
			std::vector<field> fields{};
			nlohmann::json defaults{};
		};

		inline config_registry& get_registry( )
		{
			static config_registry r{};
			return r;
		}

		inline std::uint32_t make_key( std::string_view category, std::string_view name )
		{
			char buf[ 256 ]{};
			const auto cat_len = std::min( category.size( ), std::size_t{ 120 } );
			const auto name_len = std::min( name.size( ), std::size_t{ 120 } );

			std::memcpy( buf, category.data( ), cat_len );
			buf[ cat_len ] = '.';
			std::memcpy( buf + cat_len + 1, name.data( ), name_len );
			buf[ cat_len + 1 + name_len ] = '\0';

			return static_cast<std::uint32_t>(xui::fnv1a( buf ));
		}

		inline void register_field( field f )
		{
			get_registry( ).fields.push_back( f );
		}

		inline void unregister_ptr( void* ptr )
		{
			auto& v = get_registry( ).fields;
			v.erase( std::remove_if( v.begin( ), v.end( ), [ ptr ]( const field& f ) { return f.ptr == ptr; } ), v.end( ) );
		}

	} 

	template <typename T>
	struct val
	{
		T value{};

		val( ) = default;

		explicit val( T v ) : value{ v } {}

		val( T v, std::string_view category, std::string_view name ) : value{ v }
		{
			reg( category, name );
		}

		void reg( std::string_view category, std::string_view name )
		{
			constexpr auto ft = [ ]( )
				{
					if constexpr ( std::is_same_v<T, bool> )              return field_type::bool_val;
					else if constexpr ( std::is_same_v<T, int> )          return field_type::int_val;
					else if constexpr ( std::is_same_v<T, float> )        return field_type::float_val;
					else if constexpr ( std::is_same_v<T, std::uint8_t> ) return field_type::uint8_val;
					else static_assert( !sizeof( T ), "unsupported type for config::val" );
				}( );

			detail::register_field( { .key = detail::make_key( category, name ), .type = ft, .ptr = &this->value, .count = 1 } );
		}

		operator T& ( ) noexcept { return value; }
		operator const T& ( ) const noexcept { return value; }
		val& operator=( T v ) noexcept { value = v; return *this; }
	};

	struct col
	{
		xdraw::color value{};

		col( ) = default;

		explicit col( xdraw::color v ) : value{ v } {}

		col( xdraw::color v, std::string_view category, std::string_view name ) : value{ v }
		{
			reg( category, name );
		}

		void reg( std::string_view category, std::string_view name )
		{
			detail::register_field( { .key = detail::make_key( category, name ), .type = field_type::color, .ptr = &this->value, .count = 1 } );
		}

		operator xdraw::color& ( ) noexcept { return value; }
		operator const xdraw::color& ( ) const noexcept { return value; }
		col& operator=( const xdraw::color& v ) noexcept { value = v; return *this; }
	};

	struct vec3
	{
		float3 value{};

		vec3( ) = default;

		explicit vec3( float3 v ) : value{ v } {}

		vec3( float3 v, std::string_view category, std::string_view name ) : value{ v }
		{
			reg( category, name );
		}

		void reg( std::string_view category, std::string_view name )
		{
			detail::register_field( { .key = detail::make_key( category, name ), .type = field_type::float3, .ptr = &this->value, .count = 1 } );
		}

		operator float3& ( ) noexcept { return value; }
		operator const float3& ( ) const noexcept { return value; }
		vec3& operator=( const float3& v ) noexcept { value = v; return *this; }
	};

	template <typename E>
	struct enm
	{
		static_assert( std::is_enum_v<E>, "enm requires an enum type" );

		E value{};

		enm( ) = default;

		explicit enm( E v ) : value{ v } {}

		enm( E v, std::string_view category, std::string_view name ) : value{ v }
		{
			reg( category, name );
		}

		void reg( std::string_view category, std::string_view name )
		{
			constexpr auto ft = ( sizeof( E ) == 1 ) ? field_type::uint8_val : field_type::int_val;

			detail::register_field( { .key = detail::make_key( category, name ), .type = ft, .ptr = &this->value, .count = 1 } );
		}

		operator E& ( ) noexcept { return value; }
		operator const E& ( ) const noexcept { return value; }
		enm& operator=( E v ) noexcept { value = v; return *this; }
	};

	template <std::uint32_t N>
	struct bools
	{
		bool values[ N ]{};

		bools( ) = default;

		explicit bools( std::initializer_list<bool> init )
		{
			auto i{ 0u };

			for ( auto v : init )
			{
				if ( i >= N )
				{
					break;
				}

				values[ i++ ] = v;
			}
		}

		bools( std::initializer_list<bool> init, std::string_view category, std::string_view name )
		{
			auto i{ 0u };

			for ( auto v : init )
			{
				if ( i >= N )
				{
					break;
				}

				values[ i++ ] = v;
			}

			reg( category, name );
		}

		void reg( std::string_view category, std::string_view name )
		{
			detail::register_field( { .key = detail::make_key( category, name ), .type = field_type::bool_array, .ptr = this->values, .count = N } );
		}

		bool& operator[]( std::size_t i ) { return values[ i ]; }
		const bool& operator[]( std::size_t i ) const { return values[ i ]; }
		operator bool* ( ) noexcept { return values; }
		operator const bool* ( ) const noexcept { return values; }
	};

	struct str
	{
		std::string value{};

		str( ) = default;

		explicit str( std::string_view v ) : value{ v } {}

		str( std::string_view v, std::string_view category, std::string_view name ) : value{ v }
		{
			reg( category, name );
		}

		void reg( std::string_view category, std::string_view name )
		{
			detail::register_field( { .key = detail::make_key( category, name ), .type = field_type::string_val, .ptr = &this->value, .count = 1 } );
		}

		operator std::string& ( ) noexcept { return value; }
		operator const std::string& ( ) const noexcept { return value; }
		str& operator=( const std::string& v ) noexcept { value = v; return *this; }
		str& operator=( std::string_view v ) noexcept { value = v; return *this; }

		[[nodiscard]] const char* c_str( ) const noexcept { return value.c_str( ); }
		[[nodiscard]] bool empty( ) const noexcept { return value.empty( ); }
	};

	namespace serial {

		inline nlohmann::json bind_to_json( const xui::bind_info& b )
		{
			if ( b.key == 0 )
			{
				return nullptr;
			}

			return nlohmann::json{ { "k", b.key }, { "m", static_cast< int >( b.mode ) } };
		}

		inline void json_to_bind( const nlohmann::json& j, xui::bind_info& b )
		{
			if ( j.is_null( ) )
			{
				b.key = 0;
				b.mode = xui::bind_mode::toggle;
				return;
			}

			b.key = j.value( "k", 0 );
			b.mode = static_cast< xui::bind_mode >( j.value( "m", 0 ) );
		}

		inline nlohmann::json field_to_json( const field& f )
		{
			switch ( f.type )
			{
			case field_type::setting:
			{
				const auto s = static_cast< const xui::setting* >( f.ptr );
				return nlohmann::json{ { "v", s->value }, { "b", bind_to_json( s->bind ) } };
			}
			case field_type::bool_val:  return *static_cast< const bool* >( f.ptr );
			case field_type::int_val:   return *static_cast< const int* >( f.ptr );
			case field_type::uint8_val: return *static_cast< const std::uint8_t* >( f.ptr );
			case field_type::float_val: return *static_cast< const float* >( f.ptr );
			case field_type::color:
			{
				const auto& c = *static_cast< const xdraw::color* >( f.ptr );
				return nlohmann::json::array( { c.r, c.g, c.b, c.a } );
			}
			case field_type::float3:
			{
				const auto& v = *static_cast< const config::float3* >( f.ptr );
				return nlohmann::json::array( { v.x, v.y, v.z } );
			}
			case field_type::bool_array:
			{
				const auto arr = static_cast< const bool* >( f.ptr );
				auto j = nlohmann::json::array( );

				for ( std::uint32_t i = 0; i < f.count; ++i )
				{
					j.push_back( arr[ i ] );
				}

				return j;
			}
			case field_type::string_val: 
			{
				return *static_cast< const std::string* >( f.ptr );
			}
			case field_type::custom:
			{
				const auto c = static_cast< const custom_field* >( f.ptr );
				return c->serialize( );
			}
			}
			return nullptr;
		}

		inline void json_to_field( const nlohmann::json& j, field& f )
		{
			try
			{
				switch ( f.type )
				{
				case field_type::setting:
				{
					auto s = static_cast< xui::setting* >( f.ptr );

					if ( j.contains( "v" ) )
					{
						s->value = j[ "v" ].get< bool >( );
					}

					if ( j.contains( "b" ) )
					{
						json_to_bind( j[ "b" ], s->bind );
					}

					if ( s->bind.key != 0 && s->bind.mode == xui::bind_mode::toggle )
					{
						s->bind.active = s->value;
					}

					break;
				}
				case field_type::bool_val:  *static_cast< bool* >( f.ptr ) = j.get< bool >( ); break;
				case field_type::int_val:   *static_cast< int* >( f.ptr ) = j.get< int >( ); break;
				case field_type::uint8_val: *static_cast< std::uint8_t* >( f.ptr ) = j.get< std::uint8_t >( ); break;
				case field_type::float_val: *static_cast< float* >( f.ptr ) = j.get< float >( ); break;
				case field_type::color:
				{
					auto& c = *static_cast< xdraw::color* >( f.ptr );
					if ( j.is_array( ) && j.size( ) >= 4 )
					{
						c.r = j[ 0 ]; c.g = j[ 1 ]; c.b = j[ 2 ]; c.a = j[ 3 ];
					}

					break;
				}
				case field_type::float3:
				{
					auto& v = *static_cast< config::float3* >( f.ptr );
					if ( j.is_array( ) && j.size( ) >= 3 )
					{
						v.x = j[ 0 ]; v.y = j[ 1 ]; v.z = j[ 2 ];
					}

					break;
				}
				case field_type::bool_array:
				{
					auto arr = static_cast< bool* >( f.ptr );
					if ( j.is_array( ) )
					{
						for ( std::uint32_t i = 0; i < std::min< std::uint32_t >( static_cast< std::uint32_t >( j.size( ) ), f.count ); ++i )
						{
							arr[ i ] = j[ i ].get< bool >( );
						}
					}

					break;
				}
				case field_type::string_val:
				{
					if ( j.is_string( ) )
					{
						*static_cast< std::string* >( f.ptr ) = j.get<std::string>( );
					}
					break;
				}
				case field_type::custom:
				{
					auto c = static_cast< custom_field* >( f.ptr );
					c->deserialize( j );
					break;
				}
				}
			}
			catch ( ... ) {}
		}

	} 

	namespace compress {

		inline std::vector<std::uint8_t> deflate( const std::string& input )
		{
			const auto bound = LZ4_compressBound( static_cast< int >( input.size( ) ) );
			if ( bound <= 0 )
			{
				return {};
			}

			std::vector<std::uint8_t> buf( 4 + bound );

			const auto src_size = static_cast< std::uint32_t >( input.size( ) );
			std::memcpy( buf.data( ), &src_size, 4 );

			const auto compressed = LZ4_compress_default( input.data( ), reinterpret_cast< char* >( buf.data( ) + 4 ), static_cast< int >( input.size( ) ), bound );
			if ( compressed <= 0 )
			{
				return {};
			}

			buf.resize( 4 + static_cast< std::size_t >( compressed ) );
			return buf;
		}

		inline std::optional<std::string> inflate( const std::uint8_t* data, std::size_t len )
		{
			if ( len < 4 )
			{
				return std::nullopt;
			}

			std::uint32_t original_size{};
			std::memcpy( &original_size, data, 4 );

			if ( original_size > 64 * 1024 * 1024 )
			{
				return std::nullopt;
			}

			std::string out( original_size, '\0' );

			const auto result = LZ4_decompress_safe( reinterpret_cast< const char* >( data + 4 ), out.data( ), static_cast< int >( len - 4 ), static_cast< int >( original_size ) );
			if ( result < 0 )
			{
				return std::nullopt;
			}

			out.resize( static_cast< std::size_t >( result ) );
			return out;
		}

	} 

	inline constexpr int k_version{ 3 };

	inline void apply_blank_profile( )
	{
		auto& reg = detail::get_registry( );

		for ( auto& f : reg.fields )
		{
			switch ( f.type )
			{
			case field_type::setting:
			{
				auto* s = static_cast< xui::setting* >( f.ptr );
				s->value = false;
				s->bind.key = 0;
				s->bind.mode = xui::bind_mode::toggle;
				s->bind.active = false;
				break;
			}
			case field_type::bool_val:
				*static_cast< bool* >( f.ptr ) = false;
				break;
			case field_type::uint8_val:
				*static_cast< std::uint8_t* >( f.ptr ) = 0;
				break;
			case field_type::bool_array:
			{
				auto* arr = static_cast< bool* >( f.ptr );
				for ( std::uint32_t i = 0; i < f.count; ++i )
				{
					arr[ i ] = false;
				}
				break;
			}
			case field_type::custom:
				static_cast< custom_field* >( f.ptr )->deserialize( nlohmann::json::object( ) );
				break;
			default:
				break;
			}
		}
	}

	inline void initialize( )
	{
		for ( auto s : xui::binds::all( ) )
		{
			if ( !s || s->category.empty( ) || s->name.empty( ) )
			{
				continue;
			}

			detail::register_field( { .key = detail::make_key( s->category, s->name ), .type = field_type::setting, .ptr = s, .count = 1 } );
		}

		auto& reg = detail::get_registry( );

		apply_blank_profile( );

		auto fields_obj = nlohmann::json::object( );

		for ( const auto& f : reg.fields )
		{
			char key_str[ 12 ];
			std::snprintf( key_str, sizeof( key_str ), "%08x", f.key );
			fields_obj[ key_str ] = serial::field_to_json( f );
		}

		reg.defaults = std::move( fields_obj );
	}

	inline nlohmann::json to_json( )
	{
		auto& reg = detail::get_registry( );
		auto fields_obj = nlohmann::json::object( );

		for ( const auto& f : reg.fields )
		{
			char key_str[ 12 ];
			std::snprintf( key_str, sizeof( key_str ), "%08x", f.key );
			fields_obj[ key_str ] = serial::field_to_json( f );
		}

		return nlohmann::json{ { "version", k_version }, { "fields", std::move( fields_obj ) } };
	}

	inline bool from_json( const nlohmann::json& root )
	{
		if ( !root.contains( "version" ) || !root.contains( "fields" ) )
		{
			return false;
		}

		// strict: foreign/old formats never load
		if ( !root[ "version" ].is_number_integer( ) || root[ "version" ].get<int>( ) != k_version )
		{
			return false;
		}

		const auto& fields_obj = root[ "fields" ];
		if ( !fields_obj.is_object( ) )
		{
			return false;
		}

		auto& reg = detail::get_registry( );

		std::unordered_map<std::uint32_t, field*> lookup;
		lookup.reserve( reg.fields.size( ) );

		for ( auto& f : reg.fields )
		{
			lookup[ f.key ] = &f;
		}

		for ( auto it = fields_obj.begin( ); it != fields_obj.end( ); ++it )
		{
			auto found = lookup.find( static_cast< std::uint32_t >( std::strtoul( it.key( ).c_str( ), nullptr, 16 ) ) );
			if ( found == lookup.end( ) )
			{
				continue;
			}

			serial::json_to_field( it.value( ), *found->second );
		}
		return true;
	}

	namespace registry {

		inline constexpr wchar_t k_root_dir[ ]{ L"C:\\swift" };
		inline constexpr wchar_t k_file_ext[ ]{ L".cfg" };

		// envelope so only configs written by this build family load
		inline constexpr std::uint8_t k_magic[ 8 ]{ 'S', 'W', 'I', 'F', 'T', 'C', 'F', 'G' };
		inline constexpr std::uint32_t k_format_id{ 0x53574633 };

		inline std::vector<std::uint8_t> wrap_payload( const std::vector<std::uint8_t>& compressed )
		{
			std::vector<std::uint8_t> out;
			out.reserve( compressed.size( ) + 12 );

			out.insert( out.end(), std::begin( k_magic ), std::end( k_magic ) );

			const auto id = k_format_id;
			const auto* id_bytes = reinterpret_cast< const std::uint8_t* >( &id );
			out.insert( out.end( ), id_bytes, id_bytes + sizeof( id ) );

			out.insert( out.end( ), compressed.begin( ), compressed.end( ) );
			return out;
		}

		inline bool unwrap_payload( const std::vector<std::uint8_t>& file_data, std::vector<std::uint8_t>& compressed )
		{
			if ( file_data.size( ) <= sizeof( k_magic ) + sizeof( std::uint32_t ) )
			{
				return false;
			}

			if ( std::memcmp( file_data.data( ), k_magic, sizeof( k_magic ) ) != 0 )
			{
				return false;
			}

			std::uint32_t id{};
			std::memcpy( &id, file_data.data( ) + sizeof( k_magic ), sizeof( id ) );
			if ( id != k_format_id )
			{
				return false;
			}

			compressed.assign( file_data.begin( ) + sizeof( k_magic ) + sizeof( id ), file_data.end( ) );
			return true;
		}

		inline std::filesystem::path directory( )
		{
			return std::filesystem::path( k_root_dir );
		}

		inline std::filesystem::path sanitize_name( std::wstring_view name )
		{
			std::wstring cleaned;
			cleaned.reserve( name.size( ) );

			for ( const auto ch : name )
			{
				const auto invalid = ch < 32
					|| ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*'
					|| ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|';

				cleaned.push_back( invalid ? L'_' : ch );
			}

			while ( !cleaned.empty( ) && ( cleaned.back( ) == L' ' || cleaned.back( ) == L'.' ) )
			{
				cleaned.pop_back( );
			}

			if ( cleaned.empty( ) )
			{
				cleaned = L"config";
			}

			return std::filesystem::path( k_root_dir ) / ( cleaned + k_file_ext );
		}

		inline bool save( std::wstring_view name )
		{
			std::error_code ec;
			std::filesystem::create_directories( k_root_dir, ec );
			if ( ec )
			{
				return false;
			}

			const auto json_str = to_json( ).dump( -1 );
			const auto compressed = compress::deflate( json_str );
			if ( compressed.empty( ) )
			{
				return false;
			}

			const auto payload = wrap_payload( compressed );

			std::ofstream file( sanitize_name( name ), std::ios::binary );
			if ( !file.is_open( ) )
			{
				return false;
			}

			file.write( reinterpret_cast< const char* >( payload.data( ) ), static_cast< std::streamsize >( payload.size( ) ) );
			file.close( );
			return file.good( );
		}

		inline bool load( std::wstring_view name )
		{
			std::ifstream file( sanitize_name( name ), std::ios::binary );
			if ( !file.is_open( ) )
			{
				return false;
			}

			std::vector<std::uint8_t> buf( ( std::istreambuf_iterator< char >( file ) ), std::istreambuf_iterator< char >( ) );

			std::vector<std::uint8_t> compressed;
			if ( !unwrap_payload( buf, compressed ) )
			{
				return false;
			}

			const auto json_str = compress::inflate( compressed.data( ), compressed.size( ) );
			if ( !json_str )
			{
				return false;
			}

			try
			{
				const auto j = nlohmann::json::parse( *json_str, nullptr, false );
				if ( j.is_discarded( ) )
				{
					return false;
				}

				return from_json( j );
			}
			catch ( ... ) { return false; }
		}

		inline std::vector<std::wstring> list( )
		{
			std::vector<std::wstring> names;

			std::error_code ec;
			std::filesystem::directory_iterator it( k_root_dir, std::filesystem::directory_options::skip_permission_denied, ec );
			if ( ec )
			{
				return names;
			}

			const auto end = std::filesystem::directory_iterator{ };

			for ( ; it != end; it.increment( ec ) )
			{
				if ( ec )
				{
					break;
				}

				const auto& entry = *it;
				if ( !entry.is_regular_file( ec ) )
				{
					continue;
				}

				const auto& path = entry.path( );
				if ( path.extension( ).wstring( ) != k_file_ext )
				{
					continue;
				}

				names.emplace_back( path.stem( ).wstring( ) );
			}

			std::sort( names.begin( ), names.end( ) );
			return names;
		}

	} 

} 
