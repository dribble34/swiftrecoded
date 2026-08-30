#include <core/settings.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names_legit[ ]{ "head", "chest", "stomach", "arms", "legs" };

	} 

	void menu::draw_legitbot( float /*group_w*/ ) const
	{
		auto& s = settings::g_combat;
		auto& lb = s.m_legitbot;
		auto& wg = lb.groups[ this->m_subtab ];

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = this->m_body_x;
		const auto body_y = this->m_body_y;
		const auto content_w = this->m_body_w;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		const auto col_h = ( this->m_body_h - tokens::gap ) * 0.5f;

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( xui::begin_child( "##legitbot_left_col", col_w, this->m_body_h, true ) )
		{
			xui::checkbox( "aimbot", wg.aimbot );

			xui::slider_float( "fov", wg.fov, 0.5f, 30.0f, "%.1f°" );
			xui::slider_int( "smooth", wg.smooth, 0, 100, "%d" );
			xui::multicombo( "hitboxes", wg.hitboxes, detail::hitbox_names_legit, 5 );

			xui::checkbox( "humanization simple", wg.humanization_simple );
			xui::checkbox( "humanization advanced", wg.humanization_advanced );
			if ( wg.humanization_advanced.value )
			{
				xui::slider_int( "overshoot chance", wg.overshoot_chance, 0, 100, "%d%%" );
				xui::slider_float( "overshoot amount", wg.overshoot_amount, 0.1f, 3.0f, "%.1f" );
			}

			xui::checkbox( "multipoint", wg.multipoint );
			if ( wg.multipoint.value )
			{
				xui::slider_float( "multipoint scale", wg.multipoint_scale, 10.0f, 90.0f, "%.0f%%" );
			}

			xui::checkbox( "draw fov", wg.visualize_fov );

			if ( xui::begin_popup( "##fov_color_popup", 220.0f ) )
			{
				xui::color_picker( "color##fov", wg.fov_color );
				xui::end_popup( );
			}

			xui::layout::spacing( 10.0f );

			xui::checkbox( "rcs", wg.rcs );
			if ( xui::begin_popup( "##rcs_popup", 220.0f ) )
			{
				xui::slider_int( "min##rcs", wg.rcs_min, 50, 150, "%d%%" );
				xui::slider_int( "max##rcs", wg.rcs_max, 50, 150, "%d%%" );
				xui::end_popup( );
			}

			xui::checkbox( "standalone rcs", wg.standalone_rcs );
			if ( xui::begin_popup( "##srcs_popup", 220.0f ) )
			{
				xui::slider_int( "strength##srcs", wg.standalone_rcs_strength, 0, 100, "%d%%" );
				xui::slider_int( "min##srcs", wg.standalone_rcs_min, 50, 150, "%d%%" );
				xui::slider_int( "max##srcs", wg.standalone_rcs_max, 50, 150, "%d%%" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		xui::layout::set_cursor( right_x - wx, body_y - wy );

		if ( xui::begin_child( "##legitbot_right_col", col_w, this->m_body_h, true ) )
		{
			xui::checkbox( "triggerbot", wg.triggerbot );
			xui::slider_int( "delay", wg.trigger_delay, 0, 250, "%d ms" );
			xui::slider_int( "delay random", wg.trigger_delay_random, 0, 100, "%d ms" );
			xui::slider_int( "reaction", wg.trigger_reaction, 0, 200, "%d ms" );
			xui::slider_int( "hitchance", wg.trigger_hitchance, 0, 100, "%d%%" );
			
			xui::checkbox( "seed prediction", wg.give_me_your_seed );

			xui::layout::spacing( 10.0f );

			xui::checkbox( "autowall", wg.autowall );
			xui::slider_int( "min damage##aw", wg.min_damage, 1, 125, "%d" );
			xui::checkbox( "min damage override", wg.min_damage_override );
			if ( wg.min_damage_override.value )
			{
				xui::slider_int( "override value", wg.min_damage_override_value, 1, 125, "%d" );
			}

			xui::checkbox( "flash check", wg.flash_check );
			xui::checkbox( "air check", wg.air_check );
			xui::checkbox( "smoke check", wg.smoke_check );
			xui::checkbox( "autostop", wg.autostop );

			xui::end_child( );
		}
	}

} 