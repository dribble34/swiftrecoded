#include <core/settings.hpp>
#include <core/systems/systems.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* k_cham_material_names[ ]{
			"liquid", "metallic", "matte", "flat", "bloom", "outlines", "glow", "electric", "distortion", "hologram", "pearl",
			"liquid (iz)", "matte (iz)", "flat (iz)", "bloom (iz)", "outlines (iz)", "glow (iz)", "distortion (iz)", "hologram (iz)"
		};
		constexpr auto k_cham_material_count = static_cast< int >( settings::esp::cham_ids::count );

		inline static void draw_chams_layer( const char* label, const char* popup_id, settings::esp::chams_layer& layer )
		{
			xui::checkbox( label, layer.enabled );
			if ( xui::begin_popup( popup_id, 220.0f ) )
			{
				xui::combo( "material", layer.material.value, k_cham_material_names, k_cham_material_count );
				xui::color_picker( "color", layer.color );
				xui::end_popup( );
			}
		}

		inline static void draw_chams_config( const char* label, const char* id_suffix, settings::esp::chams_config& cfg )
		{
			xui::checkbox( label, cfg.enabled );

			char label_buf[ 64 ]{};
			char popup_id[ 64 ]{};

			std::snprintf( label_buf, sizeof( label_buf ), "visible layer##%s", id_suffix );
			std::snprintf( popup_id, sizeof( popup_id ), "##visible_%s", id_suffix );
			draw_chams_layer( label_buf, popup_id, cfg.visible );

			std::snprintf( label_buf, sizeof( label_buf ), "occluded layer##%s", id_suffix );
			std::snprintf( popup_id, sizeof( popup_id ), "##occluded_%s", id_suffix );
			draw_chams_layer( label_buf, popup_id, cfg.occluded );
		}

		// The preview uses MapPlayerPreviewPanel, so its own world->clip matrix is the
		// only reliable projection for drawing ESP on top of the captured preview.
		inline static void draw_model_preview( xdraw::draw_list& dl, float x, float y, float w, float h, const settings::esp::player::overlay& cfg )
		{
			auto& preview = systems::g_model_preview;
			const auto tex = static_cast< ID3D11ShaderResourceView* >( preview.texture( ) );
			if ( !tex )
			{
				dl.text( x + 8.0f, y + 8.0f, "preview unavailable", xdraw::color{ 255, 255, 255, 120 } );
				return;
			}

			const auto side = std::min( w, h );
			const auto img_x = x + ( w - side ) * 0.5f;
			const auto img_y = y + ( h - side ) * 0.5f;
			dl.image( img_x, img_y, side, side, tex );

			const auto pawn = preview.preview_pawn( );
			if ( !pawn ) return;

			const auto skeleton = systems::g_bones.get_skeleton( pawn );
			constexpr std::array<std::uint32_t, 21> bones_used{
				cstypes::bone_ids::pelvis, cstypes::bone_ids::spine_1, cstypes::bone_ids::spine_2,
				cstypes::bone_ids::spine_3, cstypes::bone_ids::spine_4, cstypes::bone_ids::neck,
				cstypes::bone_ids::head, cstypes::bone_ids::left_clavicle, cstypes::bone_ids::left_shoulder,
				cstypes::bone_ids::left_elbow, cstypes::bone_ids::left_hand, cstypes::bone_ids::right_clavicle,
				cstypes::bone_ids::right_shoulder, cstypes::bone_ids::right_elbow, cstypes::bone_ids::right_hand,
				cstypes::bone_ids::left_hip, cstypes::bone_ids::left_knee, cstypes::bone_ids::left_foot,
				cstypes::bone_ids::right_hip, cstypes::bone_ids::right_knee, cstypes::bone_ids::right_foot
			};

			// The composition texture is already rendered through the preview camera.
			// For the overlay, use the preview model's local bone bounds directly:
			// X -> screen X and Z -> screen Y. This keeps the skeleton stable even
			// when the internal Panorama camera matrix is unavailable or changes.
			const auto project = [ & ]( const math::vector3& p, float& sx, float& sy,
								const math::vector3& min_world, const math::vector3& max_world ) -> bool
			{
				const float world_w = max_world.x - min_world.x;
				const float world_h = max_world.z - min_world.z;
				if ( !std::isfinite( world_w ) || !std::isfinite( world_h ) || world_w < 0.001f || world_h < 0.001f )
					return false;

				const float nx = ( p.x - min_world.x ) / world_w;
				const float nz = ( p.z - min_world.z ) / world_h;
				if ( !std::isfinite( nx ) || !std::isfinite( nz ) )
					return false;

				// Leave a small margin around the model inside the preview texture.
				constexpr float margin = 0.08f;
				const float usable = 1.0f - margin * 2.0f;
				sx = img_x + ( margin + nx * usable ) * side;
				sy = img_y + ( 1.0f - ( margin + nz * usable ) ) * side;
				return true;
			};

			math::vector3 min_world{ std::numeric_limits<float>::max( ), std::numeric_limits<float>::max( ), std::numeric_limits<float>::max( ) };
			math::vector3 max_world{ std::numeric_limits<float>::lowest( ), std::numeric_limits<float>::lowest( ), std::numeric_limits<float>::lowest( ) };
			int world_count{};
			for ( const auto bone_id : bones_used )
			{
				const auto& pos = skeleton[ bone_id ].position;
				if ( !std::isfinite( pos.x ) || !std::isfinite( pos.y ) || !std::isfinite( pos.z ) )
					continue;
				min_world.x = std::min( min_world.x, pos.x );
				min_world.y = std::min( min_world.y, pos.y );
				min_world.z = std::min( min_world.z, pos.z );
				max_world.x = std::max( max_world.x, pos.x );
				max_world.y = std::max( max_world.y, pos.y );
				max_world.z = std::max( max_world.z, pos.z );
				++world_count;
			}
			if ( world_count < 10 ) return;

			std::array<std::pair<float, float>, bones_used.size( )> pts{};
			std::array<bool, bones_used.size( )> valid{};
			float min_x = std::numeric_limits<float>::max( );
			float max_x = std::numeric_limits<float>::lowest( );
			float min_y = std::numeric_limits<float>::max( );
			float max_y = std::numeric_limits<float>::lowest( );
			int count{};
			for ( std::size_t i = 0; i < bones_used.size( ); ++i )
			{
				float sx{}, sy{};
				if ( !project( skeleton[ bones_used[ i ] ].position, sx, sy, min_world, max_world ) ) continue;
				pts[ i ] = { sx, sy };
				valid[ i ] = true;
				min_x = std::min( min_x, sx ); max_x = std::max( max_x, sx );
				min_y = std::min( min_y, sy ); max_y = std::max( max_y, sy );
				++count;
			}
			if ( count < 10 || max_x <= min_x || max_y <= min_y ) return;

			const float pad_x = std::max( 2.0f, ( max_x - min_x ) * -1.0f );
			const float pad_y = std::max( 2.0f, ( max_y - min_y ) * -1.0f );
			const float box_x = min_x - pad_x;
			const float box_y = min_y - pad_y;
			const float box_w = ( max_x - min_x ) + pad_x * 2.0f;
			const float box_h = ( max_y - min_y ) + pad_y * 2.0f;
			const float body_x = box_x + box_w * 0.5f;

			const auto& box_col = cfg.m_box.visible_color.value;
			const auto& skel_col = cfg.m_skeleton.visible_color.value;

			if ( cfg.m_box.enabled.value )
			{
				if ( cfg.m_box.fill.value )
					dl.rect_filled( box_x + 1.0f, box_y + 1.0f, box_w - 2.0f, box_h - 2.0f, box_col.alpha( 20 ) );
				if ( cfg.m_box.style.value == settings::esp::player::overlay::box::style_type::full )
				{
					if ( cfg.m_box.outline.value )
						dl.rect( box_x - 1.0f, box_y - 1.0f, box_w + 2.0f, box_h + 2.0f, xdraw::color{ 0, 0, 0, 220 }, 1.0f );
					dl.rect( box_x, box_y, box_w, box_h, box_col, 1.0f );
				}
				else
				{
					const auto c = std::min( cfg.m_box.corner_length.value, std::min( box_w, box_h ) * 0.35f );
					dl.line( box_x, box_y, box_x + c, box_y, box_col, 1.0f );
					dl.line( box_x, box_y, box_x, box_y + c, box_col, 1.0f );
					dl.line( box_x + box_w - c, box_y, box_x + box_w, box_y, box_col, 1.0f );
					dl.line( box_x + box_w, box_y, box_x + box_w, box_y + c, box_col, 1.0f );
					dl.line( box_x, box_y + box_h - c, box_x, box_y + box_h, box_col, 1.0f );
					dl.line( box_x, box_y + box_h, box_x + c, box_y + box_h, box_col, 1.0f );
					dl.line( box_x + box_w - c, box_y + box_h, box_x + box_w, box_y + box_h, box_col, 1.0f );
					dl.line( box_x + box_w, box_y + box_h - c, box_x + box_w, box_y + box_h, box_col, 1.0f );
				}
			}

			if ( cfg.m_skeleton.enabled.value )
			{
				constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 17> links{
					std::pair{ cstypes::bone_ids::head, cstypes::bone_ids::neck },
					std::pair{ cstypes::bone_ids::neck, cstypes::bone_ids::spine_4 },
					std::pair{ cstypes::bone_ids::spine_4, cstypes::bone_ids::spine_3 },
					std::pair{ cstypes::bone_ids::spine_3, cstypes::bone_ids::spine_2 },
					std::pair{ cstypes::bone_ids::spine_2, cstypes::bone_ids::spine_1 },
					std::pair{ cstypes::bone_ids::spine_1, cstypes::bone_ids::pelvis },
					std::pair{ cstypes::bone_ids::left_clavicle, cstypes::bone_ids::left_shoulder },
					std::pair{ cstypes::bone_ids::left_shoulder, cstypes::bone_ids::left_elbow },
					std::pair{ cstypes::bone_ids::left_elbow, cstypes::bone_ids::left_hand },
					std::pair{ cstypes::bone_ids::right_clavicle, cstypes::bone_ids::right_shoulder },
					std::pair{ cstypes::bone_ids::right_shoulder, cstypes::bone_ids::right_elbow },
					std::pair{ cstypes::bone_ids::right_elbow, cstypes::bone_ids::right_hand },
					std::pair{ cstypes::bone_ids::left_hip, cstypes::bone_ids::left_knee },
					std::pair{ cstypes::bone_ids::left_knee, cstypes::bone_ids::left_foot },
					std::pair{ cstypes::bone_ids::right_hip, cstypes::bone_ids::right_knee },
					std::pair{ cstypes::bone_ids::right_knee, cstypes::bone_ids::right_foot },
					std::pair{ cstypes::bone_ids::right_hip, cstypes::bone_ids::pelvis }
				};
				const auto index_of = [ & ]( std::uint32_t id ) -> std::size_t {
					for ( std::size_t i = 0; i < bones_used.size( ); ++i ) if ( bones_used[ i ] == id ) return i;
					return bones_used.size( );
				};
				for ( const auto& [ a, b ] : links )
				{
					const auto ia = index_of( a ); const auto ib = index_of( b );
					if ( ia >= bones_used.size( ) || ib >= bones_used.size( ) || !valid[ ia ] || !valid[ ib ] ) continue;
					dl.line( pts[ ia ].first, pts[ ia ].second, pts[ ib ].first, pts[ ib ].second, skel_col, std::max( 0.5f, cfg.m_skeleton.thickness.value ) );
				}
				const auto left_hip = index_of( cstypes::bone_ids::left_hip );
				const auto pelvis = index_of( cstypes::bone_ids::pelvis );
				if ( left_hip < bones_used.size( ) && pelvis < bones_used.size( ) && valid[ left_hip ] && valid[ pelvis ] )
					dl.line( pts[ left_hip ].first, pts[ left_hip ].second, pts[ pelvis ].first, pts[ pelvis ].second, skel_col, std::max( 0.5f, cfg.m_skeleton.thickness.value ) );
			}

			const auto draw_bar = [ & ]( float bx, float by, float bw, float bh, float fraction, const auto& bar, bool vertical )
			{
				const auto bg = bar.background_color.value;
				const auto fg = fraction <= 0.5f ? bar.low_color.value : bar.full_color.value;
				dl.rect_filled( bx, by, bw, bh, bg.alpha( 180 ) );
				if ( vertical ) dl.rect_filled( bx, by + bh * ( 1.0f - fraction ), bw, bh * fraction, fg );
				else dl.rect_filled( bx, by, bw * fraction, bh, fg );
				if ( bar.outline_setting.value ) dl.rect( bx - 1.0f, by - 1.0f, bw + 2.0f, bh + 2.0f, bar.outline_color.value, 1.0f );
			};
			const auto bar_pos = []( auto v ) { return static_cast<int>( v.value ); };
			if ( cfg.m_health_bar.enabled.value )
			{
				switch ( bar_pos( cfg.m_health_bar.position ) )
				{
				case 0: draw_bar( box_x - 5.0f, box_y, 3.0f, box_h, 1.0f, cfg.m_health_bar, true ); break;
				case 1: draw_bar( box_x, box_y - 5.0f, box_w, 3.0f, 1.0f, cfg.m_health_bar, false ); break;
				default: draw_bar( box_x, box_y + box_h + 3.0f, box_w, 3.0f, 1.0f, cfg.m_health_bar, false ); break;
				}
			}
			if ( cfg.m_ammo_bar.enabled.value )
			{
				switch ( bar_pos( cfg.m_ammo_bar.position ) )
				{
				case 0: draw_bar( box_x - 10.0f, box_y, 3.0f, box_h, 0.70f, cfg.m_ammo_bar, true ); break;
				case 1: draw_bar( box_x, box_y - 10.0f, box_w, 3.0f, 0.70f, cfg.m_ammo_bar, false ); break;
				default: draw_bar( box_x, box_y + box_h + 8.0f, box_w, 3.0f, 0.70f, cfg.m_ammo_bar, false ); break;
				}
			}

			// Name and weapon are centered against the actual ESP box, not its left edge.
			if ( cfg.m_name.enabled.value )
			{
				constexpr std::string_view preview_name = "https://t.me/blgcy";
				const auto [ tw, th ] = xdraw::measure_text( preview_name );
				dl.text( body_x - tw * 0.5f, box_y - th - 4.0f, preview_name, cfg.m_name.color.value );
			}
			if ( cfg.m_weapon.enabled.value )
			{
				constexpr std::string_view weapon_name = "AWP";
				const auto [ tw, th ] = xdraw::measure_text( weapon_name );
				dl.text( body_x - tw * 0.5f, box_y + box_h + 5.0f, weapon_name, cfg.m_weapon.text_color.value );
			}
		}

	}

	void menu::draw_player( float /*group_w*/ ) const
	{
		auto& esp = settings::g_esp;
		auto& p = esp.m_player;

		const auto col_w = ( this->m_body_w - tokens::gap ) * 0.5f;
		const auto subtab = this->m_subtab;
		const auto has_overlay = ( subtab <= 1 );

		xui::layout::set_cursor( this->m_body_x - this->m_x, this->m_body_y - this->m_y );

		// left column: multiple separate boxes for different categories
		if ( has_overlay )
		{
			auto& ov = p.m_overlay[ subtab ];
			auto& chams = ( subtab == 0 ) ? p.m_chams.enemy : p.m_chams.team;
			auto& glow = ( subtab == 0 ) ? p.m_glow.enemy : p.m_glow.team;

			// Overlay elements box
			if ( xui::begin_child( "##player_esp", col_w ) )
			{
				xui::checkbox( "enable", ov.enabled );

				xui::checkbox( "box", ov.m_box.enabled );
				if ( xui::begin_popup( "##box_popup", 220.0f ) )
				{
					constexpr const char* box_styles[ ]{ "full", "cornered" };
					xui::combo( "style##box", ov.m_box.style.value, box_styles, 2 );

					xui::checkbox( "fill", ov.m_box.fill );
					xui::checkbox( "outline", ov.m_box.outline );
					xui::slider_float( "corner length", ov.m_box.corner_length, 2.0f, 20.0f, "%.0f" );
					xui::color_picker( "visible color##box", ov.m_box.visible_color );
					xui::color_picker( "occluded color##box", ov.m_box.occluded_color );
					xui::end_popup( );
				}

				xui::checkbox( "skeleton", ov.m_skeleton.enabled );
				if ( xui::begin_popup( "##skeleton_popup", 220.0f ) )
				{
					constexpr const char* skel_modes[ ]{ "normal", "backtrack" };
					xui::combo( "mode##skel", ov.m_skeleton.type.value, skel_modes, 2 );

					xui::slider_float( "thickness##skel", ov.m_skeleton.thickness, 0.5f, 4.0f, "%.1f" );
					xui::color_picker( "visible color##skel", ov.m_skeleton.visible_color );
					xui::color_picker( "occluded color##skel", ov.m_skeleton.occluded_color );
					xui::end_popup( );
				}

				xui::checkbox( "health bar", ov.m_health_bar.enabled );
				if ( xui::begin_popup( "##health_popup", 220.0f ) )
				{
					constexpr const char* bar_positions[ ]{ "left", "top", "bottom" };
					xui::combo( "position##hp", ov.m_health_bar.position.value, bar_positions, 3 );

					xui::checkbox( "outline##hp", ov.m_health_bar.outline_setting );
					xui::checkbox( "gradient##hp", ov.m_health_bar.gradient );
					xui::checkbox( "show value##hp", ov.m_health_bar.show_value );
					xui::checkbox( "glow##hp", ov.m_health_bar.glow );
					xui::color_picker( "full color##hp", ov.m_health_bar.full_color );
					xui::color_picker( "low color##hp", ov.m_health_bar.low_color );
					xui::color_picker( "background##hp", ov.m_health_bar.background_color );
					xui::color_picker( "outline color##hp", ov.m_health_bar.outline_color );
					xui::color_picker( "text color##hp", ov.m_health_bar.text_color );
					xui::color_picker( "glow color##hp", ov.m_health_bar.glow_color );
					xui::slider_float( "glow strength##hp", ov.m_health_bar.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::checkbox( "ammo bar", ov.m_ammo_bar.enabled );
				if ( xui::begin_popup( "##ammo_popup", 220.0f ) )
				{
					constexpr const char* bar_positions[ ]{ "left", "top", "bottom" };
					xui::combo( "position##ammo", ov.m_ammo_bar.position.value, bar_positions, 3 );

					xui::checkbox( "outline##ammo", ov.m_ammo_bar.outline_setting );
					xui::checkbox( "gradient##ammo", ov.m_ammo_bar.gradient );
					xui::checkbox( "show value##ammo", ov.m_ammo_bar.show_value );
					xui::checkbox( "glow##ammo", ov.m_ammo_bar.glow );
					xui::color_picker( "full color##ammo", ov.m_ammo_bar.full_color );
					xui::color_picker( "low color##ammo", ov.m_ammo_bar.low_color );
					xui::color_picker( "background##ammo", ov.m_ammo_bar.background_color );
					xui::color_picker( "outline color##ammo", ov.m_ammo_bar.outline_color );
					xui::color_picker( "text color##ammo", ov.m_ammo_bar.text_color );
					xui::color_picker( "glow color##ammo", ov.m_ammo_bar.glow_color );
					xui::slider_float( "glow strength##ammo", ov.m_ammo_bar.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::checkbox( "name", ov.m_name.enabled );
				if ( xui::begin_popup( "##name_popup", 220.0f ) )
				{
					xui::checkbox( "outline##name", ov.m_name.outline );
					xui::checkbox( "glow##name", ov.m_name.glow );
					xui::color_picker( "color##name", ov.m_name.color );
					xui::color_picker( "glow color##name", ov.m_name.glow_color );
					xui::slider_float( "glow strength##name", ov.m_name.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::checkbox( "weapon", ov.m_weapon.enabled );
				if ( xui::begin_popup( "##weapon_popup", 220.0f ) )
				{
					constexpr const char* display_types[ ]{ "text", "icon", "text + icon" };
					xui::combo( "display##wep", ov.m_weapon.display.value, display_types, 3 );

					xui::color_picker( "text color##wep", ov.m_weapon.text_color );
					xui::color_picker( "icon color##wep", ov.m_weapon.icon_color );
					xui::end_popup( );
				}

				xui::checkbox( "flags", ov.m_info_flags.enabled );
				if ( xui::begin_popup( "##flags_popup", 220.0f ) )
				{
					constexpr const char* flag_names[ ]{ "money", "armor", "kit", "scoped", "defusing", "flashed", "ping", "distance" };
					xui::multicombo( "flags##mc", ov.m_info_flags.flags, flag_names, settings::esp::player::overlay::info_flags::count );

					xui::color_picker( "money##flags", ov.m_info_flags.money_color );
					xui::color_picker( "armor##flags", ov.m_info_flags.armor_color );
					xui::color_picker( "kit##flags", ov.m_info_flags.kit_color );
					xui::color_picker( "scoped##flags", ov.m_info_flags.scoped_color );
					xui::color_picker( "defusing##flags", ov.m_info_flags.defusing_color );
					xui::color_picker( "flashed##flags", ov.m_info_flags.flashed_color );
					xui::color_picker( "distance##flags", ov.m_info_flags.distance_color );
					xui::end_popup( );
				}

				xui::checkbox( "oof arrows", ov.m_oof_arrow.enabled );
				if ( xui::begin_popup( "##oof_popup", 220.0f ) )
				{
					xui::checkbox( "glow##oof", ov.m_oof_arrow.glow );
					xui::slider_float( "width##oof", ov.m_oof_arrow.width, 4.0f, 40.0f, "%.0f" );
					xui::slider_float( "height##oof", ov.m_oof_arrow.height, 4.0f, 40.0f, "%.0f" );
					xui::slider_float( "radius x##oof", ov.m_oof_arrow.radius_x, 50.0f, 600.0f, "%.0f" );
					xui::slider_float( "radius y##oof", ov.m_oof_arrow.radius_y, 50.0f, 600.0f, "%.0f" );
					xui::slider_float( "glow strength##oof", ov.m_oof_arrow.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::color_picker( "visible color##oof", ov.m_oof_arrow.visible_color );
					xui::color_picker( "occluded color##oof", ov.m_oof_arrow.occluded_color );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			// Chams box
			if ( xui::begin_child( "##player_chams", col_w ) )
			{
				detail::draw_chams_config( "chams", "main", chams );
				xui::end_child( );
			}

			// Glow box
			if ( xui::begin_child( "##player_glow", col_w ) )
			{
				xui::checkbox( "glow", glow.enabled );
				if ( xui::begin_popup( "##glow_popup", 220.0f ) )
				{
					xui::color_picker( "color##glow", glow.color );
					xui::end_popup( );
				}

				xui::end_child( );
			}
		}
		else
		{
			// Local player tab - split into multiple boxes with dynamic sizing

			// Local chams box
			if ( xui::begin_child( "##local_chams", col_w ) )
			{
				detail::draw_chams_config( "chams", "local_main", p.m_chams.local );
				xui::end_child( );
			}

			// Opacity box
			if ( xui::begin_child( "##local_alpha", col_w ) )
			{
				xui::checkbox( "lower opacity", esp.m_local_alpha.enabled );
				if ( xui::begin_popup( "##local_alpha_popup", 220.0f ) )
				{
					xui::slider_float( "opacity", esp.m_local_alpha.opacity, 0.0f, 1.0f, "%.2f" );
					xui::checkbox( "only when scoped", esp.m_local_alpha.only_scoped );
					xui::end_popup( );
				}
				xui::end_child( );
			}

			// Glow box
			if ( xui::begin_child( "##local_glow", col_w ) )
			{
				xui::checkbox( "glow", p.m_glow.local.enabled );
				if ( xui::begin_popup( "##local_glow_popup", 220.0f ) )
				{
					xui::color_picker( "color##local_glow", p.m_glow.local.color );
					xui::end_popup( );
				}
				xui::end_child( );
			}

			// Weapon chams box
			if ( xui::begin_child( "##weapon_chams", col_w ) )
			{
				detail::draw_chams_config( "weapon chams", "vm_weapon", esp.m_viewmodel.weapon );
				xui::end_child( );
			}

			// Arms chams box
			if ( xui::begin_child( "##arms_chams", col_w ) )
			{
				detail::draw_chams_config( "arms chams", "vm_arms", esp.m_viewmodel.arms );
				xui::end_child( );
			}
		}

		// right column: model preview (full-height box, header on top, model below)
		xui::layout::set_cursor( this->m_body_x - this->m_x + col_w + tokens::gap, this->m_body_y - this->m_y );

		if ( xui::begin_child( "##player_preview", col_w, this->m_body_h, false ) )
		{
			xui::text( "model preview", tokens::col_text );

			constexpr auto k_preview_header_h{ 36.0f };

			auto& dl = xui::draw::current( );
			detail::draw_model_preview( dl, this->m_body_x + col_w + tokens::gap, this->m_body_y + k_preview_header_h, col_w, this->m_body_h - k_preview_header_h, p.m_overlay[ subtab ] );

			xui::end_child( );
		}
	}

} 
