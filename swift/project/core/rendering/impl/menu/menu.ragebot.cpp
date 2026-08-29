#include <cstdio>
#include <core/settings.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names[ ]{ "head", "chest", "stomach", "arms", "legs", "feet" };
		constexpr const char* pitch_items[ ]{ "none", "down", "up", "custom" };
		constexpr const char* yaw_items[ ]{ "backwards", "forward", "custom" };

	} 

	void menu::draw_ragebot( float /*group_w*/ ) const
	{
		auto& s = settings::g_combat;
		auto& rb = s.m_ragebot;
		auto& aa = s.m_antiaim;
		auto& zb = s.m_zeusbot;
		auto& kb = s.m_knifebot;
		auto& autos = s.m_autos;

		auto& wg = rb.groups[ this->m_subtab ];

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = this->m_body_x;
		const auto body_y = this->m_body_y;
		const auto content_w = this->m_body_w;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( xui::begin_child( "##ragebot_aimbot", col_w ) )
		{
			xui::checkbox( "enabled", rb.enabled );
			xui::checkbox( "silent", wg.silent );
			xui::checkbox( "nospread", wg.no_spread );
			xui::checkbox( "autostop", wg.autostop );
			if ( xui::begin_popup( "##autostop_popup", 220.0f ) )
			{
				xui::checkbox( "early", wg.autostop_early );
				xui::end_popup( );
			}
			if ( this->m_subtab == 4 )
			{
				xui::checkbox( "autoscope", autos.scope );
			}
			xui::slider_float( "fov", wg.max_fov, 1.0f, 180.0f, "%.0f°" );
			xui::slider_int( "hitchance", wg.hitchance, 0, 100, "%d%%" );
			static char min_dmg_buf[32];
			if ( wg.min_damage == settings::combat::ragebot::k_lethal_min_damage )
				std::snprintf( min_dmg_buf, sizeof( min_dmg_buf ), "FL" );
			else if ( wg.min_damage > 100 && wg.min_damage < settings::combat::ragebot::k_lethal_min_damage )
				std::snprintf( min_dmg_buf, sizeof( min_dmg_buf ), "HP + %d", static_cast<int>( wg.min_damage ) - 100 );
			else
				std::snprintf( min_dmg_buf, sizeof( min_dmg_buf ), "%d", static_cast<int>( wg.min_damage ) );
			xui::slider_int( "mindamage", wg.min_damage, 5, settings::combat::ragebot::k_lethal_min_damage, min_dmg_buf );
			/*xui::slider_int( "max backtrack", s.m_lagcomp.max_backtrack_ticks, 1, 16, "%d tick(s)" );*/

			xui::checkbox( "extrapolation", s.m_lagcomp.extrapolation );
			if ( xui::begin_popup( "##extrapolation_popup", 220.0f ) )
			{
				xui::slider_int( "ticks##extrap", s.m_lagcomp.max_extrapolate_ticks, 1, 8, "%d tick(s)" );
				xui::end_popup( );
			}

			xui::checkbox( "hitchance override", wg.hitchance_override );
			if ( xui::begin_popup( "##hitchance_popup", 220.0f ) )
			{
				xui::slider_int( "value##hc", wg.hitchance_override_value, 0, 100, "%d%%" );
				xui::end_popup( );
			}

			xui::checkbox( "mindamage override", wg.min_damage_override );
			if ( xui::begin_popup( "##mindamage_popup", 220.0f ) )
			{
				static char min_dmg_ovr_buf[32];
				if ( wg.min_damage_override_value > 100 )
					std::snprintf( min_dmg_ovr_buf, sizeof( min_dmg_ovr_buf ), "HP + %d", static_cast<int>( wg.min_damage_override_value ) - 100 );
				else
					std::snprintf( min_dmg_ovr_buf, sizeof( min_dmg_ovr_buf ), "%d", static_cast<int>( wg.min_damage_override_value ) );
				xui::slider_int( "value##md", wg.min_damage_override_value, 0, 130, min_dmg_ovr_buf );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		if ( xui::begin_child( "##ragebot_extras", col_w ) )
		{
			xui::checkbox( "force bodyaim", wg.body_aim );
			xui::checkbox( "force shot", wg.force_shot );
			xui::checkbox( "auto lineup", wg.auto_lineup );

		xui::slider_float( "pointscale", wg.pointscale, 0.0f, 100.0f, "%.0f%%" );
		xui::multicombo( "hitboxes", wg.hitboxes, detail::hitbox_names, 6 );

			xui::end_child( );
		}

		xui::layout::set_cursor( right_x - wx, body_y - wy );

		if ( xui::begin_child( "##ragebot_antiaim", col_w ) )
		{
			xui::checkbox( "anti aim", aa.enabled );
			xui::checkbox( "at target", aa.at_target );

			xui::combo( "pitch", aa.pitch.value, detail::pitch_items, 4 );
				if ( aa.pitch.value == settings::combat::antiaim::pitch_mode::custom )
				{
					xui::slider_float( "custom pitch", aa.custom_pitch.value, -90.0f, 90.0f, "%.0f°" );
				}

			xui::combo( "yaw", aa.yaw.value, detail::yaw_items, 3 );
			if ( aa.yaw.value == settings::combat::antiaim::yaw_mode::custom )
			{
				xui::slider_float( "custom yaw", aa.custom_yaw.value, -180.0f, 180.0f, "%.0f°" );
			}

			xui::checkbox( "yaw from view", aa.use_view_yaw );
			xui::checkbox( "compensate roll", aa.auto_yaw_adjust );
			xui::checkbox( "force left", aa.manual_left );
			xui::checkbox( "force right", aa.manual_right );
			xui::checkbox( "hide onshot", aa.hide_shots );
			
			xui::checkbox( "direction indicator", aa.direction_indicator );

			if ( xui::begin_popup( "##aa_indicator", 220.0f ) )
			{
				constexpr const char* ind_styles[]{ "arrows", "half circle" };
				xui::combo( "style##aa_ind", aa.direction_indicator_style, ind_styles, 2 );
				xui::color_picker( "active color##aa_ind", aa.direction_indicator_color );
				xui::color_picker( "bg color##aa_ind", aa.direction_indicator_arc_color );
				xui::slider_float( "distance##aa_ind", aa.direction_indicator_distance, 20.0f, 200.0f, "%.0fpx" );
				xui::slider_float( "width / radius##aa_ind", aa.direction_indicator_width, 4.0f, 60.0f, "%.0fpx" );
				xui::slider_float( "height / thickness##aa_ind", aa.direction_indicator_height, 4.0f, 40.0f, "%.0fpx" );
				xui::checkbox( "fade animation##aa_ind", aa.direction_indicator_fade );
				xui::checkbox( "glow##aa_ind", aa.direction_indicator_glow );
				xui::slider_float( "glow strength##aa_ind", aa.direction_indicator_glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		if ( xui::begin_child( "##ragebot_otherbots", col_w ) )
		{
			xui::checkbox( "auto revolver", autos.revolver );

			xui::checkbox( "zeusbot", zb.enabled );
			if ( xui::begin_popup( "##zb_settings", 220.0f ) )
			{
				xui::slider_float( "max fov##zb", zb.max_fov, 1, 180, "%.0f°" );
				xui::checkbox( "drop after##zb", zb.drop_after );
				xui::end_popup( );
			}

			xui::checkbox( "knifebot", kb.enabled );
			if ( xui::begin_popup( "##kb_settings", 220.0f ) )
			{
				xui::slider_float( "max fov##kb", kb.max_fov, 1, 180, "%.0f°" );
				xui::end_popup( );
			}

			xui::end_child( );
		}
	}

} 
