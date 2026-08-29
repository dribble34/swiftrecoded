#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/steam/steam.hpp>
#include <core/rendering/rendering.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

namespace features::esp::other {

	namespace detail {

		struct avatar_cache
		{
			struct entry
			{
				Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture{};
				bool attempted{};
			};

			std::unordered_map<std::uintptr_t, entry> m_entries{};

			[[nodiscard]] ID3D11ShaderResourceView* get( std::uintptr_t steam_id )
			{
				auto it = this->m_entries.find( steam_id );
				if ( it != this->m_entries.end( ) )
				{
					return it->second.texture.Get( );
				}

				auto& e = this->m_entries[ steam_id ];
				e.attempted = true;

				const auto image_handle = steam::friends::get_medium_friend_avatar( steam_id );
				if ( image_handle <= 0 )
				{
					return nullptr;
				}

				std::uint32_t w{}, h{};
				if ( !steam::utils::get_image_size( image_handle, &w, &h ) || !w || !h )
				{
					return nullptr;
				}

				std::vector<std::uint8_t> rgba( w * h * 4 );
				if ( !steam::utils::get_image_rgba( image_handle, rgba.data( ), static_cast< int >( rgba.size( ) ) ) )
				{
					return nullptr;
				}

				e.texture = xdraw::create_srv_from_rgba( rgba.data( ), static_cast< int >( w ), static_cast< int >( h ) );
				return e.texture.Get( );
			}

			void clear( )
			{
				this->m_entries.clear( );
			}
		};

		constexpr unsigned char spectator_icon[ 1030 ]
		{
			0x3C, 0x73, 0x76, 0x67, 0x20, 0x77, 0x69, 0x64, 0x74, 0x68, 0x3D, 0x22,
			0x31, 0x32, 0x22, 0x20, 0x68, 0x65, 0x69, 0x67, 0x68, 0x74, 0x3D, 0x22,
			0x31, 0x32, 0x22, 0x20, 0x76, 0x69, 0x65, 0x77, 0x42, 0x6F, 0x78, 0x3D,
			0x22, 0x30, 0x20, 0x30, 0x20, 0x31, 0x32, 0x20, 0x31, 0x32, 0x22, 0x20,
			0x66, 0x69, 0x6C, 0x6C, 0x3D, 0x22, 0x6E, 0x6F, 0x6E, 0x65, 0x22, 0x20,
			0x78, 0x6D, 0x6C, 0x6E, 0x73, 0x3D, 0x22, 0x68, 0x74, 0x74, 0x70, 0x3A,
			0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x77, 0x33, 0x2E, 0x6F, 0x72, 0x67,
			0x2F, 0x32, 0x30, 0x30, 0x30, 0x2F, 0x73, 0x76, 0x67, 0x22, 0x3E, 0x0D,
			0x0A, 0x3C, 0x67, 0x20, 0x63, 0x6C, 0x69, 0x70, 0x2D, 0x70, 0x61, 0x74,
			0x68, 0x3D, 0x22, 0x75, 0x72, 0x6C, 0x28, 0x23, 0x63, 0x6C, 0x69, 0x70,
			0x30, 0x5F, 0x31, 0x35, 0x35, 0x5F, 0x32, 0x32, 0x33, 0x29, 0x22, 0x3E,
			0x0D, 0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D,
			0x35, 0x20, 0x36, 0x43, 0x35, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32,
			0x32, 0x20, 0x35, 0x2E, 0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x36, 0x2E,
			0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38,
			0x39, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31, 0x31, 0x43, 0x35, 0x2E,
			0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x36, 0x2E, 0x38, 0x39, 0x34, 0x36,
			0x34, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20, 0x37, 0x20,
			0x36, 0x20, 0x37, 0x43, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20,
			0x37, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x36, 0x2E,
			0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31,
			0x31, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31, 0x31, 0x43, 0x36, 0x2E,
			0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x39, 0x35,
			0x37, 0x20, 0x37, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20,
			0x37, 0x20, 0x36, 0x43, 0x37, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37,
			0x38, 0x20, 0x36, 0x2E, 0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x35, 0x2E,
			0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31,
			0x31, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38, 0x39, 0x43, 0x36, 0x2E,
			0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x35, 0x2E, 0x31, 0x30, 0x35, 0x33,
			0x36, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20, 0x35, 0x20,
			0x36, 0x20, 0x35, 0x43, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20,
			0x35, 0x20, 0x35, 0x2E, 0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x35, 0x2E,
			0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38,
			0x39, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38, 0x39, 0x43, 0x35, 0x2E,
			0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x35, 0x2E, 0x34, 0x38, 0x30, 0x34,
			0x33, 0x20, 0x35, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20,
			0x35, 0x20, 0x36, 0x5A, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65,
			0x3D, 0x22, 0x23, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73,
			0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61,
			0x70, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74,
			0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69,
			0x6E, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D,
			0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D, 0x37,
			0x2E, 0x35, 0x31, 0x35, 0x20, 0x38, 0x2E, 0x37, 0x33, 0x39, 0x43, 0x37,
			0x2E, 0x30, 0x32, 0x39, 0x32, 0x34, 0x20, 0x38, 0x2E, 0x39, 0x31, 0x34,
			0x32, 0x36, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x36, 0x34, 0x31, 0x20, 0x39,
			0x2E, 0x30, 0x30, 0x32, 0x36, 0x31, 0x20, 0x36, 0x20, 0x39, 0x43, 0x34,
			0x2E, 0x32, 0x20, 0x39, 0x20, 0x32, 0x2E, 0x37, 0x20, 0x38, 0x20, 0x31,
			0x2E, 0x35, 0x20, 0x36, 0x43, 0x32, 0x2E, 0x37, 0x20, 0x34, 0x20, 0x34,
			0x2E, 0x32, 0x20, 0x33, 0x20, 0x36, 0x20, 0x33, 0x43, 0x37, 0x2E, 0x38,
			0x20, 0x33, 0x20, 0x39, 0x2E, 0x33, 0x20, 0x34, 0x20, 0x31, 0x30, 0x2E,
			0x35, 0x20, 0x36, 0x43, 0x31, 0x30, 0x2E, 0x34, 0x35, 0x37, 0x38, 0x20,
			0x36, 0x2E, 0x30, 0x37, 0x30, 0x33, 0x35, 0x20, 0x31, 0x30, 0x2E, 0x34,
			0x31, 0x34, 0x38, 0x20, 0x36, 0x2E, 0x31, 0x34, 0x30, 0x31, 0x39, 0x20,
			0x31, 0x30, 0x2E, 0x33, 0x37, 0x31, 0x20, 0x36, 0x2E, 0x32, 0x30, 0x39,
			0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x3D, 0x22, 0x23,
			0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F,
			0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61, 0x70, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
			0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69, 0x6E, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x70,
			0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D, 0x39, 0x2E, 0x35, 0x20,
			0x38, 0x56, 0x39, 0x2E, 0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
			0x65, 0x3D, 0x22, 0x23, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20,
			0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63,
			0x61, 0x70, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73,
			0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F,
			0x69, 0x6E, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E,
			0x0D, 0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D,
			0x39, 0x2E, 0x35, 0x20, 0x31, 0x31, 0x56, 0x31, 0x31, 0x2E, 0x30, 0x30,
			0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x3D, 0x22, 0x23,
			0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F,
			0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61, 0x70, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
			0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69, 0x6E, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F,
			0x67, 0x3E, 0x0D, 0x0A, 0x3C, 0x64, 0x65, 0x66, 0x73, 0x3E, 0x0D, 0x0A,
			0x3C, 0x63, 0x6C, 0x69, 0x70, 0x50, 0x61, 0x74, 0x68, 0x20, 0x69, 0x64,
			0x3D, 0x22, 0x63, 0x6C, 0x69, 0x70, 0x30, 0x5F, 0x31, 0x35, 0x35, 0x5F,
			0x32, 0x32, 0x33, 0x22, 0x3E, 0x0D, 0x0A, 0x3C, 0x72, 0x65, 0x63, 0x74,
			0x20, 0x77, 0x69, 0x64, 0x74, 0x68, 0x3D, 0x22, 0x31, 0x32, 0x22, 0x20,
			0x68, 0x65, 0x69, 0x67, 0x68, 0x74, 0x3D, 0x22, 0x31, 0x32, 0x22, 0x20,
			0x66, 0x69, 0x6C, 0x6C, 0x3D, 0x22, 0x77, 0x68, 0x69, 0x74, 0x65, 0x22,
			0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F, 0x63, 0x6C, 0x69, 0x70, 0x50, 0x61,
			0x74, 0x68, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F, 0x64, 0x65, 0x66, 0x73, 0x3E,
			0x0D, 0x0A, 0x3C, 0x2F, 0x73, 0x76, 0x67, 0x3E, 0x0D, 0x0A
		};

	} 

	void overlay::on_render( xdraw::draw_list& draw_list )
	{
		this->add_spectators( draw_list );
		this->add_bomb( draw_list );
		this->add_direction_indicator( draw_list );
	}

	void overlay::add_bomb( xdraw::draw_list& draw_list )
	{
		if ( !settings::g_esp.m_other.bomb_timer )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) || !systems::g_entities.exists( local.view_controller( ) ) )
		{
			return;
		}

		const auto planted_c4 = memory::read<std::uintptr_t>( addresses::globals::planted_c4 );
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );

		if ( !planted_c4 || !global_vars )
		{
			return;
		}

		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto blow_time = memory::read<float>( planted_c4 + SCHEMA( "C_PlantedC4", "m_flC4Blow"_hash ) );
		const auto has_exploded = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bHasExploded"_hash ) );
		const auto bomb_defused = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bBombDefused"_hash ) );

		if ( bomb_defused )
		{
			return;
		}

		const auto time_remaining = blow_time - current_time;
		const auto is_exploding = has_exploded || time_remaining <= 0.0f;

		if ( is_exploding && time_remaining < -2.0f )
		{
			return;
		}

		const auto bomb_site = memory::read<int>( planted_c4 + SCHEMA( "C_PlantedC4", "m_nBombSite"_hash ) );
		const auto being_defused = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bBeingDefused"_hash ) );
		const auto timer_length = memory::read<float>( planted_c4 + SCHEMA( "C_PlantedC4", "m_flTimerLength"_hash ) );

		const auto calculate_bomb_damage = [ & ]( ) -> float
			{
				const auto view_pawn = local.view_pawn( );
				if ( !view_pawn )
				{
					return 0.0f;
				}

				const auto c4_scene_node = memory::read<std::uintptr_t>( planted_c4 + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				const auto pawn_scene_node = memory::read<std::uintptr_t>( view_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );

				if ( !c4_scene_node || !pawn_scene_node )
				{
					return 0.0f;
				}

				const auto c4_origin = memory::read<math::vector3>( c4_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
				const auto pawn_origin = memory::read<math::vector3>( pawn_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );

				const auto distance = ( c4_origin - pawn_origin ).length( );

				// m_flBombRadius на C_PlantedC4 хранит урон бомбы текущей карты (напр. 700 на Dust2),
				// а радиус поражения = урон * 3.5 (напр. 2450). Считываем реальные значения вместо хардкода.
				constexpr auto fallback_damage{ 650.0f };
				const auto bomb_damage = memory::safe_read<float>( planted_c4 + SCHEMA( "C_PlantedC4", "m_flBombRadius"_hash ) ).value_or( fallback_damage );
				const auto effective_damage = bomb_damage > 1.0f ? bomb_damage : fallback_damage;
				const auto bomb_radius = effective_damage * 3.5f;

				const auto sigma = bomb_radius / 3.0f;
				auto damage = effective_damage * std::exp( -( distance * distance ) / ( 2.0f * sigma * sigma ) );

				const auto armor = memory::read<int>( view_pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );

				if ( armor > 0 )
				{
					constexpr auto armor_ratio = 0.5f;
					constexpr auto armor_bonus = 0.5f;

					auto armor_absorbed = damage * armor_ratio;
					auto armor_cost = ( damage - armor_absorbed ) * armor_bonus;

					if ( armor_cost > static_cast< float >( armor ) )
					{
						armor_cost = static_cast< float >( armor ) * ( 1.0f / armor_bonus );
						armor_absorbed = damage - armor_cost;
					}

					damage = armor_absorbed;
				}

				return std::floor( damage );
			}( );

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto& s = xui::ctx( ).style;

		constexpr auto h{ 24.0f };
		constexpr auto top_offset{ 175.0f };
		constexpr auto r{ 8.0f };
		constexpr auto inner_r{ 6.0f };
		constexpr auto inner_pad{ 2.0f };
		constexpr auto text_pad_x{ 8.0f };
		constexpr auto text_nudge{ 0.5f };
		constexpr auto section_spacing{ 2.0f };

		const auto inner_h = h - inner_pad * 2.0f;

		auto timer_color = [ & ]( ) -> xdraw::color
			{
				if ( is_exploding )
				{
					return { 255, 100, 100, 255 };
				}

				const auto frac = timer_length > 0.0f ? time_remaining / timer_length : 1.0f;

				if ( frac > 0.5f )
				{
					return s.accent;
				}
				else if ( frac > 0.2f )
				{
					const auto t = ( frac - 0.2f ) / 0.3f;

					return
					{
						static_cast< std::uint8_t >( 255 ),
						static_cast< std::uint8_t >( 200 + static_cast< int >( ( s.accent.g - 200 ) * t ) ),
						static_cast< std::uint8_t >( 140 + static_cast< int >( ( s.accent.b - 140 ) * t ) ),
						255
					};
				}
				else
				{
					const auto t = frac / 0.2f;

					return
					{
						255,
						static_cast< std::uint8_t >( 120 + static_cast< int >( 80 * t ) ),
						static_cast< std::uint8_t >( 100 + static_cast< int >( 40 * t ) ),
						255
					};
				}
			}( );

		const auto site_label = bomb_site == 0 ? "A plant" : "B plant";
		const auto [site_tw, site_th] = xdraw::measure_text( site_label );
		const auto site_pill_w = site_tw + text_pad_x * 2.0f;

		const auto damage = static_cast< int >( calculate_bomb_damage );
		const auto view_pawn = local.view_pawn( );
		const auto health = view_pawn ? memory::read<int>( view_pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) ) : 0;
		const auto will_kill = health <= damage;

		char health_buf[ 16 ]{};
		std::snprintf( health_buf, sizeof( health_buf ), "%+d", -damage );

		const auto [health_vw, health_vh] = xdraw::measure_text( health_buf );
		const auto [health_uw, health_uh] = xdraw::measure_text( " health" );
		const auto health_pill_w = health_vw + health_uw + text_pad_x * 2.0f;
		const auto health_col = will_kill ? xdraw::color{ 255, 120, 120, 255 } : xdraw::color{ 160, 210, 140, 255 };

		char timer_buf[ 16 ]{};
		const char* timer_unit{};

		if ( is_exploding )
		{
			strncpy_s( timer_buf, sizeof( timer_buf ), "0.0s", _TRUNCATE );
			timer_unit = " exploding";
		}
		else
		{
			std::snprintf( timer_buf, sizeof( timer_buf ), "%.1fs", time_remaining );
			timer_unit = being_defused ? " defusing" : " explosion";
		}

		const auto [timer_vw, timer_vh] = xdraw::measure_text( timer_buf );
		const auto [timer_uw, timer_uh] = xdraw::measure_text( timer_unit );
		const auto timer_pill_w = timer_vw + timer_uw + text_pad_x * 2.0f;

		const auto total_w = inner_pad + site_pill_w + section_spacing + health_pill_w + section_spacing + timer_pill_w + inner_pad;
		const auto x = ( static_cast< float >( screen_w ) - total_w ) * 0.5f;
		const auto y = top_offset;

		xdraw::backdrop( x, y, total_w, h, xdraw::corner_radius{ r }, xdraw::color{ 255, 255, 255, 225 } );
		draw_list.rect_filled( x, y, total_w, h, s.window_bg, xdraw::corner_radius{ r } );
		draw_list.rect( x, y, total_w, h, xdraw::color{ 255, 255, 255, 32 }, xdraw::corner_radius{ r } );

		auto cx = x + inner_pad;

		draw_list.rect_filled( cx, y + inner_pad, site_pill_w, inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );
		draw_list.text( cx + text_pad_x, y + ( h - site_th ) * 0.5f + text_nudge, site_label, s.accent );
		cx += site_pill_w + section_spacing;

		const auto health_unit_col = xdraw::color{ health_col.r, health_col.g, health_col.b, 120 };
		draw_list.rect_filled( cx, y + inner_pad, health_pill_w, inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );
		draw_list.text( cx + text_pad_x, y + ( h - health_vh ) * 0.5f + text_nudge, health_buf, health_col );
		draw_list.text( cx + text_pad_x + health_vw, y + ( h - health_uh ) * 0.5f + text_nudge, " health", health_unit_col );
		cx += health_pill_w + section_spacing;

		const auto timer_unit_col = xdraw::color{ timer_color.r, timer_color.g, timer_color.b, 120 };
		draw_list.rect_filled( cx, y + inner_pad, timer_pill_w, inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );
		draw_list.text( cx + text_pad_x, y + ( h - timer_vh ) * 0.5f + text_nudge, timer_buf, timer_color );
		draw_list.text( cx + text_pad_x + timer_vw, y + ( h - timer_uh ) * 0.5f + text_nudge, timer_unit, timer_unit_col );
	}

	void overlay::add_spectators( xdraw::draw_list& draw_list )
	{
		if ( !settings::g_esp.m_other.spectator_list )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) || !systems::g_entities.exists( local.view_controller( ) ) )
		{
			return;
		}

		const auto game_rules = memory::read<std::uintptr_t>( addresses::globals::game_rules );
		if ( !game_rules || memory::read<int>( game_rules + SCHEMA( "C_CSGameRules", "m_gamePhase"_hash ) ) >= 4 )
		{
			return;
		}

		const auto local_controller = local.controller;
		const auto view_controller = local.view_controller( );
		const auto view_pawn = local.view_pawn( );
		if ( !view_pawn )
		{
			return;
		}

		const auto [screen_w, screen_h] = xdraw::viewport_size( );

		constexpr auto row_spacing{ 4.0f };
		constexpr auto row_h{ 28.0f };
		constexpr auto header_h{ 38.0f };
		constexpr auto r{ 10.0f };
		constexpr auto pad_x{ 12.0f };
		constexpr auto pad_y{ 8.0f };
		constexpr auto underline_h{ 1.0f };
		constexpr auto avatar_size{ 20.0f };
		constexpr auto min_w{ 200.0f };

		
		static float drag_offset_x{ 0.0f };
		static float drag_offset_y{ 0.0f };
		static bool is_dragging{ false };
		static float widget_x{ static_cast<float>( screen_w ) - 220.0f }; 
		static float widget_y{ 200.0f }; 

		struct spectator_entry
		{
			char name[ 128 ];
			std::uintptr_t steam_id;
		};

		spectator_entry entries[ 32 ]{};
		auto count{ 0 };

		for ( const auto& player : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			if ( player.ptr == view_controller || player.ptr == local_controller || count >= 32 )
			{
				continue;
			}

			if ( memory::read<bool>( player.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto obs_pawn_handle = memory::read<std::uint32_t>( player.ptr + SCHEMA( "CCSPlayerController", "m_hObserverPawn"_hash ) );
			if ( !obs_pawn_handle || obs_pawn_handle == 0xffffffff )
			{
				continue;
			}

			const auto obs_pawn = systems::g_entities.lookup( obs_pawn_handle );
			if ( !obs_pawn )
			{
				continue;
			}

			const auto observer_services = memory::safe_read<std::uintptr_t>( obs_pawn + SCHEMA( "C_BasePlayerPawn", "m_pObserverServices"_hash ) ).value_or( 0 );
			if ( !observer_services || ( observer_services >> 48 ) != 0 )
			{
				continue;
			}

			const auto observer_target_handle = memory::safe_read<std::uint32_t>( observer_services + SCHEMA( "CPlayer_ObserverServices", "m_hObserverTarget"_hash ) ).value_or( 0 );
			if ( !observer_target_handle )
			{
				continue;
			}

			const auto observer_target = systems::g_entities.lookup( observer_target_handle );
			if ( observer_target != view_pawn )
			{
				continue;
			}

			const auto name_ptr = memory::read<std::uintptr_t>( player.ptr + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) );
			if ( !name_ptr )
			{
				continue;
			}

			auto name = memory::read_string( name_ptr, 127 );
			std::ranges::transform( name, name.begin( ), [ ]( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

			auto& e = entries[ count++ ];
			strncpy_s( e.name, name.c_str( ), sizeof( e.name ) - 1 );

			e.name[ sizeof( e.name ) - 1 ] = '\0';
			e.steam_id = memory::read<std::uintptr_t>( player.ptr + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
		}

		
		static detail::avatar_cache avatars{};

		
		const auto content_h = static_cast< float >( count ) * row_h + ( count > 1 ? ( count - 1 ) * row_spacing : 0.0f );
		const auto total_h = header_h + pad_y + content_h + pad_y;

		
		float max_w = min_w;
		xdraw::push_font( rendering::g_fonts.sfpro_bold[ rendering::fonts::size::normal ] );
		const auto [header_tw, header_th] = xdraw::measure_text( "spectators" );
		xdraw::pop_font( );
		
		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			const auto [nw, nh] = xdraw::measure_text( e.name );
			const auto row_w = pad_x + avatar_size + pad_x + nw + pad_x;
			if ( row_w > max_w ) max_w = row_w;
		}
		
		
		const auto& input = xui::ctx( ).input;
		const auto header_rect = xui::rect{ widget_x, widget_y, max_w, header_h };
		
		if ( input.in_rect( header_rect ) && input.mouse_clicked && !is_dragging )
		{
			is_dragging = true;
			drag_offset_x = input.mouse_x - widget_x;
			drag_offset_y = input.mouse_y - widget_y;
		}
		
		if ( is_dragging )
		{
			if ( input.mouse_down )
			{
				widget_x = input.mouse_x - drag_offset_x;
				widget_y = input.mouse_y - drag_offset_y;
				
				
				widget_x = std::max( 0.0f, std::min( widget_x, static_cast<float>( screen_w ) - max_w ) );
				widget_y = std::max( 0.0f, std::min( widget_y, static_cast<float>( screen_h ) - total_h ) );
			}
			else
			{
				is_dragging = false;
			}
		}

				const auto x = widget_x;
		const auto base_y = widget_y;
		const auto w = max_w;

		const auto dark = rendering::g_menu.is_dark( );

		const auto backdrop_col = dark ? tokens::col_dark : xdraw::color{ 245, 246, 252, 210 };
		const auto fill_col = dark ? tokens::col_card.alpha( 110 ) : xdraw::color{ 255, 255, 255, 120 };
		const auto border_col = dark ? tokens::col_border.alpha( 35 ) : xdraw::color{ 24, 26, 38, 40 };

		xdraw::backdrop( x, base_y, w, total_h, xdraw::corner_radius{ r }, backdrop_col );
		draw_list.rect_filled( x, base_y, w, total_h, fill_col, xdraw::corner_radius{ r } );
		draw_list.rect( x, base_y, w, total_h, border_col, xdraw::corner_radius{ r } );

		
		xdraw::push_font( rendering::g_fonts.sfpro_bold[ rendering::fonts::size::normal ] );
		const auto header_text_x = x + ( w - header_tw ) * 0.5f;
		draw_list.text( header_text_x, base_y + ( header_h - header_th ) * 0.5f, "spectators", tokens::col_accent );
		xdraw::pop_font( );
		
		
		const auto line_y = base_y + header_h - underline_h;
		const auto transparent = tokens::col_accent.alpha( 0 );
		const auto bright = tokens::col_accent.alpha( 200 );
		
		
		draw_list.rect_filled_gradient( 
			x + w * 0.4f, line_y, w * 0.2f, underline_h, 
			bright, bright, bright, bright
		);
		
		
		draw_list.rect_filled_gradient( 
			x, line_y, w * 0.4f, underline_h, 
			transparent, bright, bright, transparent
		);
		
		
		draw_list.rect_filled_gradient( 
			x + w * 0.6f, line_y, w * 0.4f, underline_h, 
			bright, transparent, transparent, bright
		);

		
		float current_y = base_y + header_h + pad_y;
		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			const auto [nw, nh] = xdraw::measure_text( e.name );
			const auto avatar_tex = avatars.get( e.steam_id );
			
			
			if ( avatar_tex )
			{
				draw_list.image( x + pad_x, current_y + ( row_h - avatar_size ) * 0.5f, avatar_size, avatar_size, avatar_tex, xdraw::corner_radius{ 4.0f } );
			}
			else
			{
				
				const auto circle_x = x + pad_x + avatar_size * 0.5f;
				const auto circle_y = current_y + row_h * 0.5f;
				draw_list.circle_filled( circle_x, circle_y, avatar_size * 0.5f, tokens::col_accent.alpha( 120 ), 32 );
				
				
				if ( e.name[ 0 ] != '\0' )
				{
					char initial[ 2 ] = { static_cast<char>( std::toupper( e.name[ 0 ] ) ), '\0' };
					xdraw::push_font( rendering::g_fonts.sfpro_bold[ rendering::fonts::size::petite ] );
					const auto [iw, ih] = xdraw::measure_text( initial );
					draw_list.text( circle_x - iw * 0.5f, circle_y - ih * 0.5f, initial, tokens::col_accent );
					xdraw::pop_font( );
				}
			}
			
			
			draw_list.text( x + pad_x + avatar_size + pad_x, current_y + ( row_h - nh ) * 0.5f, e.name, tokens::col_text );

			current_y += row_h + row_spacing;
		}
	}

	void overlay::add_direction_indicator( xdraw::draw_list& draw_list )
	{
		const auto& aa = settings::g_combat.m_antiaim;
		if ( !aa.enabled.value || !aa.direction_indicator.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) || !local.is_alive )
		{
			return;
		}

		// Calculate target angle based on current antiaim state or mouse override
		const auto view_angles = systems::g_input.get_view_angles( );
		float target_yaw = view_angles.y;

		if ( aa.mouse_override.value || aa.mouse_override_has_set )
		{
			target_yaw = aa.mouse_override_yaw.value;
		}
		else if ( aa.manual_left.value )
		{
			target_yaw = view_angles.y - 90.0f;
		}
		else if ( aa.manual_right.value )
		{
			target_yaw = view_angles.y + 90.0f;
		}
		else if ( aa.yaw.value == settings::combat::antiaim::yaw_mode::backwards )
		{
			target_yaw = view_angles.y + 180.0f;
		}
		else if ( aa.yaw.value == settings::combat::antiaim::yaw_mode::custom )
		{
			target_yaw = view_angles.y - aa.custom_yaw.value;
		}

		// Relative angle from view
		float rel_yaw = math::helpers::normalize_yaw( target_yaw - view_angles.y );

		// Smooth slide animation (interpolating current indicator position towards target_yaw)
		static float current_anim_yaw = rel_yaw;
		static float last_change_time = 0.0f;
		static float prev_rel_yaw = rel_yaw;

		const float dt = xdraw::delta_time( );
		const float current_clock = static_cast< float >( GetTickCount64( ) ) * 0.001f;

		if ( std::fabsf( math::helpers::normalize_yaw( rel_yaw - prev_rel_yaw ) ) > 1.0f )
		{
			last_change_time = current_clock;
			prev_rel_yaw = rel_yaw;
		}

		// Smooth lerp (slide movement)
		float diff = math::helpers::normalize_yaw( rel_yaw - current_anim_yaw );
		const float lerp_speed = aa.mouse_override.value ? 60.0f : 30.0f;
		current_anim_yaw = math::helpers::normalize_yaw( current_anim_yaw + diff * std::min( lerp_speed * dt, 1.0f ) );

		// Fade Out timer calculation
		static float fade_alpha = 1.0f;
		if ( aa.direction_indicator_fade.value )
		{
			const float delay = aa.direction_indicator_fade_delay.value;
			const float idle_time = current_clock - last_change_time;

			if ( aa.mouse_override.value )
			{
				fade_alpha = 1.0f; // Always active while holding mouse override
				last_change_time = current_clock;
			}
			else if ( idle_time > delay )
			{
				fade_alpha -= dt * 2.0f;
				if ( fade_alpha < 0.0f ) fade_alpha = 0.0f;
			}
			else
			{
				fade_alpha += dt * 5.0f;
				if ( fade_alpha > 1.0f ) fade_alpha = 1.0f;
			}
		}
		else
		{
			fade_alpha = 1.0f;
		}

		if ( fade_alpha <= 0.0f )
		{
			return;
		}

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const float center_x = static_cast< float >( screen_w ) * 0.5f;
		const float center_y = static_cast< float >( screen_h ) * 0.5f;

		const auto base_col = aa.direction_indicator_color.value;
		const std::uint8_t alpha = static_cast< std::uint8_t >( static_cast< float >( base_col.a ) * fade_alpha );
		if ( alpha <= 0 )
		{
			return;
		}

		const auto col = xdraw::color{ base_col.r, base_col.g, base_col.b, alpha };
		const auto col_inactive = xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 50.0f * fade_alpha ) };

		const float distance = aa.direction_indicator_distance.value;
		const float radius = aa.direction_indicator_width.value; // Radius / Width
		const float thickness = aa.direction_indicator_height.value; // Thickness / Height

		if ( aa.direction_indicator_style.value == 1 )
		{
			// Mode 1: Half Circle (demi cercle) at crosshair
			// Grand demi-cercle (Background)
			const auto bg_base_col = aa.direction_indicator_arc_color.value;
			const auto bg_alpha = static_cast< std::uint8_t >( static_cast< float >( bg_base_col.a ) * fade_alpha );
			const auto bg_col = xdraw::color{ bg_base_col.r, bg_base_col.g, bg_base_col.b, bg_alpha };

			// Semi-circle background facing downwards (angles from -pi/2 to pi/2, or bottom half)
			constexpr int segments = 32;
			// Compute ring arc boundaries (ensure thickness does not collapse inner radius to zero)
			const float effective_thick = std::min( thickness, radius * 0.75f );
			const float inner_r = std::max( 2.0f, radius - effective_thick * 0.5f );
			const float outer_r = radius + effective_thick * 0.5f;

			// Draw full background semi-circle arc (spanning 180° around crosshair)
			for ( int i = 0; i < segments; ++i )
			{
				const float a1 = ( std::numbers::pi_v<float> * 0.0f ) + ( std::numbers::pi_v<float> * ( static_cast<float>( i ) / segments ) );
				const float a2 = ( std::numbers::pi_v<float> * 0.0f ) + ( std::numbers::pi_v<float> * ( static_cast<float>( i + 1 ) / segments ) );

				const float p1_x = center_x + std::cosf( a1 ) * inner_r;
				const float p1_y = center_y + std::sinf( a1 ) * inner_r;
				const float p2_x = center_x + std::cosf( a2 ) * inner_r;
				const float p2_y = center_y + std::sinf( a2 ) * inner_r;
				const float p3_x = center_x + std::cosf( a2 ) * outer_r;
				const float p3_y = center_y + std::sinf( a2 ) * outer_r;
				const float p4_x = center_x + std::cosf( a1 ) * outer_r;
				const float p4_y = center_y + std::sinf( a1 ) * outer_r;

				draw_list.triangle_filled( p1_x, p1_y, p2_x, p2_y, p3_x, p3_y, bg_col );
				draw_list.triangle_filled( p1_x, p1_y, p3_x, p3_y, p4_x, p4_y, bg_col );
			}

			// Active 1/3 segment (spanning 60° = 1/3 of 180°) sliding to left (150°), center (90°), or right (30°)
			// rel_yaw: -90° (left), 90° (right), 180° / -180° / 0° (center/backwards)
			float active_center_angle = std::numbers::pi_v<float> * 0.5f; // Center (90°)
			if ( rel_yaw < -45.0f && rel_yaw > -135.0f )
			{
				active_center_angle = std::numbers::pi_v<float> * 0.833f; // Left (150°)
			}
			else if ( rel_yaw > 45.0f && rel_yaw < 135.0f )
			{
				active_center_angle = std::numbers::pi_v<float> * 0.167f; // Right (30°)
			}

			static float anim_active_angle = active_center_angle;
			const float angle_diff = active_center_angle - anim_active_angle;
			anim_active_angle += angle_diff * std::min( 20.0f * dt, 1.0f ); // Smooth slide animation

			const float active_span = std::numbers::pi_v<float> / 3.0f; // 1/3 of semicircle = 60°
			const float active_start = anim_active_angle - active_span * 0.5f;
			const float active_end = anim_active_angle + active_span * 0.5f;

			constexpr int active_segments = 16;
			for ( int i = 0; i < active_segments; ++i )
			{
				const float a1 = active_start + ( active_end - active_start ) * ( static_cast<float>( i ) / active_segments );
				const float a2 = active_start + ( active_end - active_start ) * ( static_cast<float>( i + 1 ) / active_segments );

				const float p1_x = center_x + std::cosf( a1 ) * inner_r;
				const float p1_y = center_y + std::sinf( a1 ) * inner_r;
				const float p2_x = center_x + std::cosf( a2 ) * inner_r;
				const float p2_y = center_y + std::sinf( a2 ) * inner_r;
				const float p3_x = center_x + std::cosf( a2 ) * outer_r;
				const float p3_y = center_y + std::sinf( a2 ) * outer_r;
				const float p4_x = center_x + std::cosf( a1 ) * outer_r;
				const float p4_y = center_y + std::sinf( a1 ) * outer_r;

				draw_list.triangle_filled( p1_x, p1_y, p2_x, p2_y, p3_x, p3_y, col );
				draw_list.triangle_filled( p1_x, p1_y, p3_x, p3_y, p4_x, p4_y, col );
			}

			if ( aa.direction_indicator_glow.value )
			{
				auto& glow = xdraw::get_glow( );
				const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( alpha ) * aa.direction_indicator_glow_strength.value );
				const auto glow_col = xdraw::color{ base_col.r, base_col.g, base_col.b, glow_a };

				for ( int i = 0; i < active_segments; ++i )
				{
					const float a1 = active_start + ( active_end - active_start ) * ( static_cast<float>( i ) / active_segments );
					const float a2 = active_start + ( active_end - active_start ) * ( static_cast<float>( i + 1 ) / active_segments );

					const float p1_x = center_x + std::cosf( a1 ) * inner_r;
					const float p1_y = center_y + std::sinf( a1 ) * inner_r;
					const float p2_x = center_x + std::cosf( a2 ) * inner_r;
					const float p2_y = center_y + std::sinf( a2 ) * inner_r;
					const float p3_x = center_x + std::cosf( a2 ) * outer_r;
					const float p3_y = center_y + std::sinf( a2 ) * outer_r;
					const float p4_x = center_x + std::cosf( a1 ) * outer_r;
					const float p4_y = center_y + std::sinf( a1 ) * outer_r;

					glow.triangle_filled( p1_x, p1_y, p2_x, p2_y, p3_x, p3_y, glow_col );
					glow.triangle_filled( p1_x, p1_y, p3_x, p3_y, p4_x, p4_y, glow_col );
				}
			}
		}
		else
		{
			// Mode 0: Arrows style (2D screen overlay arrows, no ground visualizer)
			const float arrow_w = aa.direction_indicator_width.value;
			const float arrow_h = aa.direction_indicator_height.value;

			auto draw_single_arrow = [ & ]( float angle_deg, xdraw::color arrow_col )
			{
				const float rad = ( angle_deg - 90.0f ) * ( std::numbers::pi_v<float> / 180.0f );
				const float perp_rad = angle_deg * ( std::numbers::pi_v<float> / 180.0f );

				const float tip_x = center_x + std::cosf( rad ) * ( distance + arrow_h );
				const float tip_y = center_y + std::sinf( rad ) * ( distance + arrow_h );

				const float base_center_x = center_x + std::cosf( rad ) * distance;
				const float base_center_y = center_y + std::sinf( rad ) * distance;

				const float base1_x = base_center_x + std::cosf( perp_rad ) * ( arrow_w * 0.5f );
				const float base1_y = base_center_y + std::sinf( perp_rad ) * ( arrow_w * 0.5f );

				const float base2_x = base_center_x - std::cosf( perp_rad ) * ( arrow_w * 0.5f );
				const float base2_y = base_center_y - std::sinf( perp_rad ) * ( arrow_w * 0.5f );

				draw_list.triangle_filled( tip_x, tip_y, base1_x, base1_y, base2_x, base2_y, arrow_col );
				const float outline_pts[ ]{ tip_x, tip_y, base1_x, base1_y, base2_x, base2_y };
				draw_list.polyline( outline_pts, xdraw::color{ 0, 0, 0, arrow_col.a }, true, 1.2f );
			};

			// Draw 4 static directional arrows
			draw_single_arrow( -90.0f, col_inactive ); // Left
			draw_single_arrow( 90.0f,  col_inactive ); // Right
			draw_single_arrow( 180.0f, col_inactive ); // Back
			draw_single_arrow( 0.0f,   col_inactive ); // Front

			// Draw active sliding arrow at current_anim_yaw
			draw_single_arrow( current_anim_yaw, col );

			if ( aa.direction_indicator_glow.value )
			{
				auto& glow = xdraw::get_glow( );
				const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( alpha ) * aa.direction_indicator_glow_strength.value );
				const auto glow_col = xdraw::color{ base_col.r, base_col.g, base_col.b, glow_a };

				const float rad = ( current_anim_yaw - 90.0f ) * ( std::numbers::pi_v<float> / 180.0f );
				const float perp_rad = current_anim_yaw * ( std::numbers::pi_v<float> / 180.0f );

				const float tip_x = center_x + std::cosf( rad ) * ( distance + arrow_h );
				const float tip_y = center_y + std::sinf( rad ) * ( distance + arrow_h );

				const float base_center_x = center_x + std::cosf( rad ) * distance;
				const float base_center_y = center_y + std::sinf( rad ) * distance;

				const float base1_x = base_center_x + std::cosf( perp_rad ) * ( arrow_w * 0.5f );
				const float base1_y = base_center_y + std::sinf( perp_rad ) * ( arrow_w * 0.5f );

				const float base2_x = base_center_x - std::cosf( perp_rad ) * ( arrow_w * 0.5f );
				const float base2_y = base_center_y - std::sinf( perp_rad ) * ( arrow_w * 0.5f );

				glow.triangle_filled( tip_x, tip_y, base1_x, base1_y, base2_x, base2_y, glow_col );
			}
		}
	}

} 
