#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names[ ]{ "head", "chest", "stomach", "arms", "legs", "feet" };
		constexpr const char* prefer_items[ ]{ "head", "damage", "reliable" };
		constexpr const char* pitch_items[ ]{ "none", "down", "up", "custom" };
		constexpr const char* yaw_items[ ]{ "backwards", "forward", "custom" };
		constexpr const char* autostop_items[ ]{ "early", "in air" };
		//constexpr const char* delay_shot_items[ ]{ "none", "always", "on peak", "on unduck" };
		constexpr const char* cham_materials[ ]{ "liquid", "metallic", "matte", "flat", "bloom", "outlines", "glow", "electric", "distortion", "hologram", "pearl",
			"liquid ignorez", "matte ignorez", "flat ignorez", "bloom ignorez", "outlines ignorez", "glow ignorez", "distortion ignorez", "hologram ignorez" };
		constexpr auto k_cham_material_count{ static_cast< int >( std::size( cham_materials ) ) };

	} // namespace detail

	void menu::draw_ragebot( float group_w ) const
	{
		auto& s = settings::g_combat;
		auto& rb = s.m_ragebot;
		auto& qp = s.m_quickpeek;
		auto& dp = s.m_duckpeek;
		auto& zb = s.m_zeusbot;
		auto& kb = s.m_knifebot;
		auto& autos = s.m_autos;
		auto& lg = s.m_lagcomp;

		auto& wg = rb.groups[ this->m_subtab ];

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap;
		const auto body_y = wy + tokens::header_bar_h + tokens::gap * 2.0f + tokens::subtab_bar_h;
		const auto content_w = this->m_w - tokens::gap * 2.0f;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( xui::begin_child( "##ragebot_aimbot", col_w ) )
		{
			xui::checkbox( "enabled", rb.enabled );
			xui::checkbox( "silent", wg.silent );
			xui::checkbox( "nospread", wg.no_spread );

			xui::checkbox("forceshot", wg.forceshot);
			if (xui::begin_popup("##forceshot_popup", 220.0f))
			{
				xui::slider_int("in-air hitchance", wg.forceshot_inair_hitchance, 0, 100, "%d%%");
				xui::slider_int("grounded hitchance", wg.forceshot_grounded_hitchance, 0, 100, "%d%%");
				xui::end_popup();
			}

			xui::checkbox("autostop", wg.autostop);
			if (xui::begin_popup("##autostop_popup", 220.0f))
			{
				xui::multicombo("mode", wg.autostop_mode.values, detail::autostop_items, 2);
				xui::checkbox("crouch to stop", wg.crouch_to_stop);
				xui::end_popup();
			}

			xui::slider_float( "fov", wg.max_fov, 1.0f, 180.0f, "%.0f°" );
			xui::slider_int( "hitchance", wg.hitchance, 0, 100, "%d%%" );
			xui::slider_int( "in-air hit chance", wg.inair_hitchance, 0, 100, "%d%%" );
			xui::checkbox( "ignore hitchance if accurate", wg.ignore_hitchance_if_accurate );
			xui::slider_int( "mindamage", wg.min_damage, 1, 101, wg.min_damage.value >= 101 ? "hp+1" : "%d" );
			//xui::slider_int( "max backtrack", s.m_lagcomp.max_backtrack_ticks, 1, 10, "%d tick(s)" );

			xui::checkbox( "hitchance override", wg.hitchance_override );
			if ( xui::begin_popup( "##hitchance_popup", 220.0f ) )
			{
				xui::slider_int( "value##hc", wg.hitchance_override_value, 0, 100, "%d%%" );
				xui::end_popup( );
			}

			xui::checkbox( "mindamage override", wg.min_damage_override );
			if ( xui::begin_popup( "##mindamage_popup", 220.0f ) )
			{
				xui::slider_int( "value##md", wg.min_damage_override_value, 1, 101, wg.min_damage_override_value.value >= 101 ? "hp+1" : "%d" );
				xui::end_popup( );
			}

			xui::checkbox( "refine shot", wg.refine_shot );

			//xui::combo( "delay shot", wg.delay_shot.value, detail::delay_shot_items, 4 );
			//if ( wg.delay_shot.value != settings::combat::ragebot::delay_shot_mode::none )
			//{
			//	xui::slider_int( "delay ticks", wg.delay_ticks, 1, 20, "%d tick(s)" );
			//}

			xui::end_child( );
		}

		const auto extras_h = std::max( 150.0f, xui::layout::avail( ).second - xui::ctx( ).style.item_spacing_y - tokens::gap );
if ( xui::begin_child( "##ragebot_extras", col_w, extras_h, true ) )
		{
			xui::checkbox( "bodyaim", wg.body_aim );
			xui::checkbox( "dynamic point scale", wg.dynamic_pointscale );
			xui::checkbox( "debug multipoints", wg.debug_multipoints );
			xui::slider_float( "pointscale", wg.pointscale, 0.0f, 100.0f, "%.0f%%" );
			xui::multicombo( "hitboxes", wg.hitboxes, detail::hitbox_names, 6 );
			xui::combo( "prefer", wg.prefer.value, detail::prefer_items, 3 );

			xui::end_child( );
		}

		xui::layout::set_cursor( right_x - wx, body_y - wy );

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

		if ( xui::begin_child( "##ragebot_peek", col_w ) )
		{
			xui::checkbox( "quick peek assist", qp.enabled );
			if ( xui::begin_popup( "##qp_colors", 220.0f ) )
			{
				xui::color_picker( "base color##qp", qp.color );
				xui::color_picker( "retracting color##qp", qp.retrack_color );
				xui::end_popup( );
			}

			xui::checkbox( "duck peek assist", dp.enabled );

			xui::end_child( );
		}
	}

	void menu::draw_antiaim( float group_w ) const
	{
		auto& s = settings::g_combat;
		auto& aa = s.m_antiaim;
		auto& qp = s.m_quickpeek;
		auto& dp = s.m_duckpeek;

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap;
		const auto body_y = wy + tokens::header_bar_h + tokens::gap * 2.0f + tokens::subtab_bar_h;
		const auto content_w = this->m_w - tokens::gap * 2.0f;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( xui::begin_child( "##antiaim_main", col_w ) )
		{
			xui::checkbox( "anti aim", aa.enabled );

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

			xui::checkbox( "yaw jitter", aa.yaw_jitter );
			if ( aa.yaw_jitter )
			{
				xui::slider_float( "yaw jitter amount", aa.yaw_jitter_amount.value, 0.0f, 90.0f, "%.0f°" );
			}

			xui::checkbox( "pitch jitter", aa.pitch_jitter );
			if ( aa.pitch_jitter )
			{
				xui::slider_float( "pitch jitter amount", aa.pitch_jitter_amount.value, 0.0f, 90.0f, "%.0f°" );
			}

			xui::checkbox( "yaw from view", aa.use_view_yaw );
			xui::checkbox( "compensate roll", aa.auto_yaw_adjust );
			xui::checkbox( "force left", aa.manual_left );
			xui::checkbox( "force right", aa.manual_right );
			xui::checkbox( "hide onshot", aa.hide_shots );
			xui::checkbox( "avoid backstab", aa.avoid_backstab );

			xui::checkbox( "direction indicator", aa.direction_indicator );
			if ( xui::begin_popup( "##aa_indicator", 220.0f ) )
			{
				xui::color_picker( "color##aa_ind", aa.direction_indicator_color );
				xui::checkbox( "glow##aa_ind", aa.direction_indicator_glow );
				xui::slider_float( "glow strength##aa_ind", aa.direction_indicator_glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		xui::layout::set_cursor( right_x - wx, body_y - wy );

		if ( xui::begin_child( "##antiaim_peek", col_w ) )
		{
			xui::checkbox( "quick peek assist", qp.enabled );
			if ( xui::begin_popup( "##qp_colors", 220.0f ) )
			{
				xui::color_picker( "base color##qp", qp.color );
				xui::color_picker( "retracting color##qp", qp.retrack_color );
				xui::end_popup( );
			}

			xui::checkbox( "duck peek assist", dp.enabled );

			xui::end_child( );
		}
	}

} // namespace rendering
