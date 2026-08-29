#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <external/xdraw/xdraw.hpp>

namespace rendering {

	class context
	{
	public:
		bool initialize( IDXGISwapChain* swap_chain );
		void shutdown( );
		void on_present( IDXGISwapChain* swap_chain );
		void on_resize_buffers( );
		void on_resize_buffers_post( IDXGISwapChain* swap_chain );

		[[nodiscard]] HWND get_window( ) const { return this->m_window; }
		[[nodiscard]] ID3D11DeviceContext* get_context( ) const { return this->m_context; }
		[[nodiscard]] bool is_initialized( ) const { return this->m_initialized; }
		[[nodiscard]] bool ui_assets_ready( ) const { return this->m_ui_assets_ready; }

	private:
		void create_rtv( IDXGISwapChain* swap_chain );
		void setup_zdraw( HWND window );
		bool try_bind_ui_assets( );

		ID3D11Device* m_device{ nullptr };
		ID3D11DeviceContext* m_context{ nullptr };
		ID3D11RenderTargetView* m_rtv{ nullptr };
		HWND m_window{ nullptr };
		bool m_initialized{ false };
		bool m_ui_assets_ready{ false };
	};

    class menu
    {
    public:
        void draw( );
        void shutdown( ) const;		void toggle( ) { this->m_open = !this->m_open; }
		[[nodiscard]] bool is_open( ) const { return this->m_open; }
		void apply_saved_cursor( );
		[[nodiscard]] int get_subtab( ) const { return this->m_subtab; }
		[[nodiscard]] bool is_dark( ) const { return this->m_dark_mode; }

        enum class tab : int
        {
            ragebot, legitbot, player, world, skins, misc, config, count
        };

	private:
        void draw_top_bar( float x, float y, float w, float h );
        void draw_subtab_bar( float content_x, float bar_y, float w );
        void draw_bottom_bar( float x, float y, float w, float h );
        void apply_theme( ) const;
        void sync_theme_style( ) const;
        void draw_profile_settings( );

        void draw_ragebot( float group_w ) const;
        void draw_legitbot( float group_w ) const;
        void draw_player( float group_w ) const;
        void draw_world( float group_w ) const;
        void draw_skins( float group_w ) const;
        void draw_misc( float group_w ) const;		void draw_config( float group_w );

        bool m_open{ true };
        bool m_last_open{ true };
        float m_open_anim{ 1.0f };
        std::uint8_t m_saved_relative_mouse{};
		bool m_has_saved_cursor{};
		int m_saved_cursor_x{};
		int m_saved_cursor_y{};

        float m_x{ 100.0f };
        float m_y{ 100.0f };
        float m_w{ 805.0f };
        float m_h{ 644.0f };
        float m_body_x{};
        float m_body_y{};
        float m_body_w{};
        float m_body_h{};

        int m_tab{};
        int m_subtab{};
        int m_last_tab{};
        float m_tab_slide{ 1.0f };
        float m_tab_dir{ 1.0f };
        int m_subtab_pill_tab{ -1 };
        float m_subtab_pill_x{ -1.0f };
		bool m_visuals_open{ false };
		
        bool m_profile_settings_open{ false };
        int m_language_setting{ 0 }; 
        int m_menu_scale{ 100 }; 
        bool m_dark_mode{ true };
        float m_dpi_scale{};

        static constexpr auto k_max_subtabs{ 6 };

        struct subtab_info
        {
            const char* names[ k_max_subtabs ]{};
            int count{};
        };

        static constexpr subtab_info k_subtab_defs[ static_cast< int >( tab::count ) ]
        {
            { { "pistol", "smg", "rifle", "shotgun", "sniper", "lmg" }, 6 },
            { { "pistol", "smg", "rifle", "shotgun", "sniper", "lmg" }, 6 },
            { { "enemies", "allies", "local" },                         3 },
            { { "scene" },                                           1 },
            { { "guns", "knives", "gloves", "agents" },                 4 },
            { { "main", "camera", "hud" },                                  3 },
            { { "general" },                                            1 }
        };
    };

	class widgets
	{
	public:
		void draw( );

		static inline std::string s_map_name{};

	private:
		void watermark( xdraw::draw_list& draw_list );
		void keybinds( xdraw::draw_list& draw_list );
		void indicators( xdraw::draw_list& draw_list );
		void crosshair_indicators( xdraw::draw_list& draw_list );
	};

	class fonts
	{
	public:
		enum class size : std::uint8_t
		{
			petite,
			normal,
			big,
			medium,
			xlarge,
			title,
			count
		};

		struct family_t
		{
			std::array<xdraw::font*, static_cast< std::size_t >( size::count )> sizes{ };

			xdraw::font* operator[]( size size ) const { return this->sizes[ static_cast< std::size_t >( size ) ]; }
			xdraw::font*& operator[]( size size ) { return this->sizes[ static_cast< std::size_t >( size ) ]; }
		};

		void initialize( );

		family_t sfpro_bold{};
		xdraw::font* fa_solid{ };
		xdraw::font* fa_solid_small{ };

	private:
		void load_family( family_t& family, std::span<const std::byte> data, const std::array<float, static_cast< std::size_t >( size::count )>& sizes );
	};

	inline context g_context{};
	inline menu g_menu{};
	inline widgets g_widgets{};
	inline fonts g_fonts{};

} 
