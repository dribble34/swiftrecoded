#include <pch/pch.hpp>
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

	} // namespace detail

	void overlay::on_render( xdraw::draw_list& draw_list )
	{
		this->add_spectators( draw_list );
		this->add_bomb( draw_list );
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

		// Check if bomb is planted
		bool bomb_planted = planted_c4 && global_vars;
		
		float time_remaining = 0.0f;
		int bomb_site = 0;
		bool is_exploding = false;
		bool bomb_defused = false;

		if ( bomb_planted )
		{
			const auto current_time = memory::read<float>( global_vars + 0x30 );
			const auto blow_time = memory::read<float>( planted_c4 + SCHEMA( "C_PlantedC4", "m_flC4Blow"_hash ) );
			const auto has_exploded = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bHasExploded"_hash ) );
			bomb_defused = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bBombDefused"_hash ) );

			if ( bomb_defused || ( has_exploded && ( blow_time - current_time ) < -2.0f ) )
			{
				bomb_planted = false;
			}
			else
			{
				time_remaining = blow_time - current_time;
				is_exploding = has_exploded || time_remaining <= 0.0f;
				bomb_site = memory::read<int>( planted_c4 + SCHEMA( "C_PlantedC4", "m_nBombSite"_hash ) );
			}
		}

		const auto [screen_w, screen_h] = xdraw::viewport_size( );

		constexpr auto r{ 10.0f };
		constexpr auto pad_x{ 12.0f };
		constexpr auto pad_y{ 10.0f };
		constexpr auto c4_icon_w{ 80.0f };
		constexpr auto c4_icon_h{ 50.0f };
		constexpr auto text_spacing{ 12.0f };

		// Dragging state
		static float drag_offset_x{ 0.0f };
		static float drag_offset_y{ 0.0f };
		static bool is_dragging{ false };
		static float widget_x{ static_cast<float>( screen_w ) * 0.5f - 120.0f }; // Center initially
		static float widget_y{ 200.0f };

		const auto view_pawn = local.view_pawn( );
		const auto health = view_pawn ? memory::read<int>( view_pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) ) : 100;

		const auto site_label = bomb_planted ? ( bomb_site == 0 ? "A" : "B" ) : "-";
		
		char timer_buf[ 32 ]{};
		if ( !bomb_planted )
		{
			std::snprintf( timer_buf, sizeof( timer_buf ), "%s - --.-s", site_label );
		}
		else if ( is_exploding )
		{
			std::snprintf( timer_buf, sizeof( timer_buf ), "%s - 0.0s", site_label );
		}
		else
		{
			std::snprintf( timer_buf, sizeof( timer_buf ), "%s - %.1fs", site_label, time_remaining );
		}

		char health_buf[ 16 ]{};
		std::snprintf( health_buf, sizeof( health_buf ), "%d hp", health );

		// Measure text
		const auto [timer_tw, timer_th] = xdraw::measure_text( timer_buf );
		const auto [health_tw, health_th] = xdraw::measure_text( health_buf );
		
		const auto text_w = std::max( timer_tw, health_tw );
		const auto total_w = pad_x + c4_icon_w + text_spacing + text_w + pad_x;
		const auto total_h = pad_y + c4_icon_h + pad_y;

		// Handle dragging
		const auto& input = xui::ctx( ).input;
		const auto widget_rect = xui::rect{ widget_x, widget_y, total_w, total_h };
		
		if ( input.in_rect( widget_rect ) && input.mouse_clicked && !is_dragging )
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
				
				// Clamp to screen bounds
				widget_x = std::max( 0.0f, std::min( widget_x, static_cast<float>( screen_w ) - total_w ) );
				widget_y = std::max( 0.0f, std::min( widget_y, static_cast<float>( screen_h ) - total_h ) );
			}
			else
			{
				is_dragging = false;
			}
		}

		// Draw container
		draw_list.rect_filled_blurred( widget_x, widget_y, total_w, total_h, xdraw::corner_radius{ r } );
		draw_list.rect_filled( widget_x, widget_y, total_w, total_h, xdraw::color{ 17, 17, 17, 230 }, xdraw::corner_radius{ r } );

		// Draw C4 bomb icon (realistic CS:GO C4 design)
		const auto icon_x = widget_x + pad_x;
		const auto icon_y = widget_y + pad_y;
		
		// Background for C4 bomb (beige/tan color like plastic explosive)
		draw_list.rect_filled( icon_x, icon_y, c4_icon_w, c4_icon_h, xdraw::color{ 200, 185, 170, 255 }, xdraw::corner_radius{ 3.0f } );
		
		// Draw large circular button on the LEFT side
		const auto big_button_x = icon_x + 12.0f;
		const auto big_button_y = icon_y + c4_icon_h * 0.5f;
		const auto big_button_radius = 9.0f;
		draw_list.circle_filled( big_button_x, big_button_y, big_button_radius, xdraw::color{ 60, 55, 50, 255 }, 32 );
		draw_list.circle_filled( big_button_x, big_button_y, big_button_radius - 1.5f, xdraw::color{ 80, 75, 70, 255 }, 32 );
		
		// Draw screen (large dark rectangle) on the RIGHT UPPER part
		const auto screen_x = icon_x + 32.0f;
		const auto screen_y = icon_y + 5.0f;
		const auto bomb_screen_w = 44.0f;
		const auto bomb_screen_h = 16.0f;
		draw_list.rect_filled( screen_x, screen_y, bomb_screen_w, bomb_screen_h, xdraw::color{ 25, 28, 22, 255 }, xdraw::corner_radius{ 1.0f } );
		
		// Draw keypad buttons BELOW the screen (4 columns x 2 rows = 8 buttons)
		const auto keypad_start_x = screen_x + 2.0f;
		const auto keypad_start_y = screen_y + bomb_screen_h + 3.0f;
		const auto button_w = 8.5f;
		const auto button_h = 4.0f;
		const auto button_gap_x = 2.0f;
		const auto button_gap_y = 2.5f;
		
		for ( int row = 0; row < 2; ++row )
		{
			for ( int col = 0; col < 4; ++col )
			{
				const auto btn_x = keypad_start_x + col * ( button_w + button_gap_x );
				const auto btn_y = keypad_start_y + row * ( button_h + button_gap_y );
				draw_list.rect_filled( btn_x, btn_y, button_w, button_h, xdraw::color{ 45, 42, 38, 255 }, xdraw::corner_radius{ 0.8f } );
			}
		}

		// Draw timer and health info on right
		const auto text_x = widget_x + pad_x + c4_icon_w + text_spacing;
		const auto text_y_top = widget_y + pad_y + ( c4_icon_h - timer_th - health_th - 4.0f ) * 0.5f;
		
		draw_list.text( text_x, text_y_top, timer_buf, xdraw::color{ 200, 200, 200, 255 } );
		draw_list.text( text_x, text_y_top + timer_th + 4.0f, health_buf, tokens::col_accent );
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

		constexpr auto margin{ 10.0f };
		constexpr auto row_spacing{ 4.0f };
		constexpr auto row_h{ 28.0f };
		constexpr auto header_h{ 38.0f };
		constexpr auto r{ 10.0f };
		constexpr auto pad_x{ 12.0f };
		constexpr auto pad_y{ 8.0f };
		constexpr auto underline_h{ 1.0f };
		constexpr auto avatar_size{ 20.0f };
		constexpr auto min_w{ 200.0f };

		// Dragging state
		static float drag_offset_x{ 0.0f };
		static float drag_offset_y{ 0.0f };
		static bool is_dragging{ false };
		static float widget_x{ static_cast<float>( screen_w ) - 220.0f }; // Initial X position (right side)
		static float widget_y{ 200.0f }; // Initial Y position

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
			std::ranges::transform( name, name.begin( ), [ ]( unsigned char c ) { return std::tolower( c ); } );

			auto& e = entries[ count++ ];
			strncpy_s( e.name, name.c_str( ), sizeof( e.name ) - 1 );

			e.name[ sizeof( e.name ) - 1 ] = '\0';
			e.steam_id = memory::read<std::uintptr_t>( player.ptr + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
		}

		// Always show spectator widget even if no spectators
		static detail::avatar_cache avatars{};

		// Calculate content height and total height
		const auto content_h = static_cast< float >( count ) * row_h + ( count > 1 ? ( count - 1 ) * row_spacing : 0.0f );
		const auto total_h = header_h + pad_y + content_h + pad_y;

		// Calculate max width
		float max_w = min_w;
		xdraw::push_font( rendering::g_fonts.hurme_black[ rendering::fonts::size::normal ] );
		const auto [header_tw, header_th] = xdraw::measure_text( "spectators" );
		xdraw::pop_font( );
		
		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			const auto [nw, nh] = xdraw::measure_text( e.name );
			const auto row_w = pad_x + avatar_size + pad_x + nw + pad_x;
			if ( row_w > max_w ) max_w = row_w;
		}
		
		// Handle dragging
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
				
				// Clamp to screen bounds
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

		// Draw container background
		draw_list.rect_filled_blurred( x, base_y, w, total_h, xdraw::corner_radius{ r } );
		draw_list.rect_filled( x, base_y, w, total_h, xdraw::color{ 17, 17, 17, 230 }, xdraw::corner_radius{ r } );

		// Draw header with hurme_black font (centered)
		xdraw::push_font( rendering::g_fonts.hurme_black[ rendering::fonts::size::normal ] );
		const auto header_text_x = x + ( w - header_tw ) * 0.5f;
		draw_list.text( header_text_x, base_y + ( header_h - header_th ) * 0.5f, "spectators", tokens::col_accent );
		xdraw::pop_font( );
		
		// Draw gradient underline below header
		const auto line_y = base_y + header_h - underline_h;
		const auto transparent = tokens::col_accent.alpha( 0 );
		const auto bright = tokens::col_accent.alpha( 200 );
		
		// Center part (20% width)
		draw_list.rect_filled_gradient( 
			x + w * 0.4f, line_y, w * 0.2f, underline_h, 
			bright, bright, bright, bright
		);
		
		// Left fade gradient (40% width)
		draw_list.rect_filled_gradient( 
			x, line_y, w * 0.4f, underline_h, 
			transparent, bright, bright, transparent
		);
		
		// Right fade gradient (40% width)
		draw_list.rect_filled_gradient( 
			x + w * 0.6f, line_y, w * 0.4f, underline_h, 
			bright, transparent, transparent, bright
		);

		// Draw entries
		float current_y = base_y + header_h + pad_y;
		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			const auto [nw, nh] = xdraw::measure_text( e.name );
			const auto avatar_tex = avatars.get( e.steam_id );
			
			// Draw avatar or fallback on left
			if ( avatar_tex )
			{
				draw_list.image( x + pad_x, current_y + ( row_h - avatar_size ) * 0.5f, avatar_size, avatar_size, avatar_tex, xdraw::corner_radius{ 4.0f } );
			}
			else
			{
				// Draw colored circle background
				const auto circle_x = x + pad_x + avatar_size * 0.5f;
				const auto circle_y = current_y + row_h * 0.5f;
				draw_list.circle_filled( circle_x, circle_y, avatar_size * 0.5f, tokens::col_accent.alpha( 120 ), 32 );
				
				// Draw first letter of name as initials
				if ( e.name[ 0 ] != '\0' )
				{
					char initial[ 2 ] = { static_cast<char>( std::toupper( e.name[ 0 ] ) ), '\0' };
					xdraw::push_font( rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );
					const auto [iw, ih] = xdraw::measure_text( initial );
					draw_list.text( circle_x - iw * 0.5f, circle_y - ih * 0.5f, initial, tokens::col_accent );
					xdraw::pop_font( );
				}
			}
			
			// Draw name on right
			draw_list.text( x + pad_x + avatar_size + pad_x, current_y + ( row_h - nh ) * 0.5f, e.name, xdraw::color{ 200, 200, 200, 255 } );

			current_y += row_h + row_spacing;
		}
	}

} // namespace features::esp::other
