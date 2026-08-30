#include <external/xorstr.hpp>

#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/rendering/rendering.hpp>
#include <core/features/features.hpp>

namespace systems
{

    // keep these as arrays: xs() sizes the xor buffer from sizeof(), so a
    // pointer would truncate the script to 8 bytes
    static constexpr const char k_preview_script[ ] = R"PANORAMA(
(function () {
    var root = $.GetContextPanel();
    if (!root)
        return;

    var old = root.FindChildTraverse('ExampleModelPreviewRoot');
    if (old) {
        try { old.DeleteAsync(0.0); } catch (e) {}
    }

    var container = $.CreatePanel('Panel', root, 'ExampleModelPreviewRoot', {
        hittest: false,
        style: 'width: 512px; height: 512px; opacity: 0.01; brightness: 0.0; wash-color: #00000000; position: 0px 0px 0px; z-index: -99999;'
    });

    if (!container)
        return;

    var sequence = 't_main_menu_rifle_awp_lookat';
    var model = 'agents/models/tm_professional/tm_professional_varj.vmdl';
    var weaponDefIndex = 40; // SSG08

    var panel = $.CreatePanel('MapPlayerPreviewPanel', container, 'ExampleModelPreview', {
        map: 'ui/buy_menu',
        camera: 'cam_loadoutmenu_ct',
        'require-composition-layer': true,
        'composition-layer-texture-name': 'example_model_preview',
        playermodel: model,
        playername: 'vanity_character',
        animgraphcharactermode: 'main-menu',
        pose_sequence: sequence,
        player: true,
        mouse_rotate: true,
        mouse_rotate_yaw: true,
        sync_spawn_addons: true,
        'transparent-background': true,
        'pin-fov': 'vertical',
        csm_split_plane0_distance_override: '120.0',
        hittest: true,
        style: 'width: 100%; height: 100%;'
    });

    if (!panel)
        return;

    try {
        if (panel.EquipPlayerWithItem && typeof BigInt === 'function') {
            var itemId = BigInt('0xF000000000000000') | BigInt(weaponDefIndex);
            panel.EquipPlayerWithItem(itemId);
        }
    } catch (e) {}

    var replay = function () {
        try {
            if (!panel)
                return;

            if (panel.IsValid && !panel.IsValid())
                return;

            // restart the clip periodically; readiness (SetReadyForDisplay) is
            // owned by the host so the 3D scene only renders while the menu is open
            if (panel.PlaySequence)
                panel.PlaySequence(sequence);

            $.Schedule(8.0, replay);
        } catch (e) {}
    };

    var keepAliveFlip = false;

    var keepAlive = function () {
        try {
            if (!panel)
                return;

            if (panel.IsValid && !panel.IsValid())
                return;

            // keep the composition layer render-active without forcing
            // readiness and without restarting the animation
            keepAliveFlip = !keepAliveFlip;
            panel.style.opacity = keepAliveFlip ? '1.0' : '0.999';

            $.Schedule(0.5, keepAlive);
        } catch (e) {}
    };

    $.Schedule(0.20, replay);
    $.Schedule(0.5, keepAlive);
})();
)PANORAMA";

    static constexpr const char k_ready_script[ ] = R"PANORAMA(
(function () {
    var root = $.GetContextPanel();
    var p = root ? root.FindChildTraverse('ExampleModelPreview') : null;
    if (!p)
        return;

    if (p.SetReadyForDisplay)
        p.SetReadyForDisplay(true);
})();
)PANORAMA";

    static constexpr const char k_kick_script[ ] = R"PANORAMA(
(function () {
    var root = $.GetContextPanel();
    var p = root ? root.FindChildTraverse('ExampleModelPreview') : null;
    if (!p)
        return;

    if (p.SetReadyForDisplay)
        p.SetReadyForDisplay(true);

    if (p.PlaySequence)
        p.PlaySequence('t_main_menu_rifle_awp_lookat');
})();
)PANORAMA";

    static constexpr const char k_pause_script[ ] = R"PANORAMA(
(function () {
    var root = $.GetContextPanel();
    var p = root ? root.FindChildTraverse('ExampleModelPreview') : null;
    if (p && p.SetReadyForDisplay)
        p.SetReadyForDisplay(false);
})();
)PANORAMA";

    bool model_preview::initialize( )
    {
        m_initialized = true;
        m_current_texture = nullptr;
        m_preview_pawn = 0;
        m_panel = 0;
        m_script_injected = false;
        m_ui_engine = 0;
        m_script_panel = 0;
        m_init_throttle = 0;
        m_panel_walk_throttle = 0;
        m_state_assert_throttle = 0;
        m_menu_was_open = true;
        std::fill( std::begin( m_world_to_clip ), std::end( m_world_to_clip ), 0.0f );
        
        logging::console::print( "[model_preview] initialized" );
        
        return true;
    }

	std::uintptr_t model_preview::find_script_panel( ) const
	{
		const auto try_root = [ ]( std::uintptr_t global ) -> std::uintptr_t
			{
				if ( !global )
				{
					return 0;
				}

				const auto root = memory::safe_read<std::uintptr_t>( global ).value_or( 0 );
				if ( !root )
				{
					return 0;
				}

				const auto panel = memory::safe_read<std::uintptr_t>( root + 0x8 ).value_or( 0 );
				const auto vtable = panel ? memory::safe_read<std::uintptr_t>( panel ).value_or( 0 ) : 0;
				if ( vtable && memory::safe_read<std::uintptr_t>( vtable ).value_or( 0 ) )
				{
					return panel;
				}

				return 0;
			};

		const auto hud = try_root( addresses::globals::hud );
		const auto main_menu = try_root( addresses::globals::main_menu_panel );

		// The main-menu root can remain valid after a level is loaded, so
		// choosing the first valid root is not enough.  s_map_name is populated
		// by level_initialization and cleared by level_shutdown, which gives us
		// an unambiguous menu-vs-match state here.
		const bool in_match = !rendering::g_widgets.s_map_name.empty( );

		if ( in_match )
		{
			if ( hud )
			{
				logging::console::print( "[model_preview] in match, using HUD (0x{:X})", hud );
				return hud;
			}

			return 0;
		}

		if ( main_menu )
		{
			logging::console::print( "[model_preview] in main menu, using main_menu_panel (0x{:X})", main_menu );
			return main_menu;
		}

		return 0;
	}

	bool model_preview::create_panel( )
	{
		if ( m_script_injected )
		{
			return true;
		}

		if ( !addresses::globals::panorama )
		{
			return false;
		}

		const auto script_panel = find_script_panel( );
		if ( !script_panel )
		{
			return false;
		}

		// panorama ui engine -> ui engine (vfunc 13), then run script on the
		// active root (HUD in a match, main menu otherwise) panel (vfunc 77)
		const auto ui_engine = memory::call_vfunc<std::uintptr_t>( addresses::globals::panorama, 13 );
		if ( !ui_engine )
		{
			return false;
		}

		m_ui_engine = ui_engine;
		m_script_panel = script_panel;

		// keep the xor_string object alive across the call; crypt_get() points
		// into the object's storage
		auto script = xorstr( k_preview_script );
		memory::call_vfunc<void>(
			m_ui_engine,
			77,
			reinterpret_cast<void*>( m_script_panel ),
			static_cast<const char*>( script.crypt_get( ) ),
			"",
			static_cast<std::uint64_t>( 1 ) );

		m_script_injected = true;
		logging::console::print( "[model_preview] preview panel script injected" );
		return true;
	}

	void model_preview::find_preview_panel( )
	{
		// the panel created by the script is named "ExampleModelPreview"; walk the ui
		// engine's panel array once and cache it (avoid walking every frame)
		if ( !m_panel )
		{
			// while the panel is not created yet, don't rescan the whole array every
			// frame - retry a few times per second instead
			if ( ( ++m_panel_walk_throttle % 120 ) != 1 )
			{
				return;
			}

			const auto engine = m_ui_engine;
			if ( !engine )
			{
				return;
			}

			const auto panels = memory::safe_read<std::uintptr_t>( engine + 0x228 ).value_or( 0 );
			const auto count = memory::safe_read<int>( engine + 0x230 ).value_or( 0 );
			if ( !panels || count <= 0 || count > 4096 )
			{
				return;
			}

			for ( auto i = 0; i < count; ++i )
			{
				const auto entry = panels + static_cast< std::size_t >( i ) * 0x20;
				const auto panel_ptr = memory::safe_read<std::uintptr_t>( entry + 0x10 ).value_or( 0 );
				if ( !panel_ptr )
				{
					continue;
				}

				const auto name_ptr = memory::safe_read<std::uintptr_t>( panel_ptr + 0x10 ).value_or( 0 );
				if ( !name_ptr )
				{
					continue;
				}

				char name[ 64 ]{};
				for ( auto k = 0; k < 63; ++k )
				{
					const auto ch = memory::safe_read<char>( name_ptr + k ).value_or( 0 );
					name[ k ] = ch;
					if ( ch == '\0' )
					{
						break;
					}
				}
				name[ 63 ] = '\0';

				if ( std::strstr( name, "ExampleModelPreview" ) )
				{
					m_panel = panel_ptr;
					break;
				}
			}

			if ( !m_panel )
			{
				return;
			}
		}

		// validate the cached panel is still alive and refresh the camera matrix
		const auto vtable = memory::safe_read<std::uintptr_t>( m_panel ).value_or( 0 );
		if ( !vtable || !memory::safe_read<std::uintptr_t>( vtable ).value_or( 0 ) )
		{
			m_panel = 0;
			return;
		}

		for ( auto i = 0; i < 16; ++i )
		{
			m_world_to_clip[ i ] = memory::safe_read<float>( m_panel + 0x2F8 + static_cast< std::size_t >( i ) * 4 ).value_or( 0.0f );
		}
	}

    void model_preview::update( )
    {
        if ( !m_initialized )
        {
            return;
        }

        static int update_count = 0;
        ++update_count;
        
        if ( update_count == 1 )
        {
            logging::console::print( "[model_preview] update() first call" );
        }

        if ( !m_script_injected )
        {
            ++m_init_throttle;
            
            if ( m_init_throttle > 10 && m_init_throttle % 60 != 0 )
            {
                return;
            }

            if ( m_init_throttle == 1 )
            {
                logging::console::print( "[model_preview] attempting panel creation" );
            }

            if ( !create_panel( ) )
            {
                return;
            }
        }

        find_preview_panel( );

        const auto menu_open = rendering::g_menu.is_open( );
        if ( menu_open != m_menu_was_open )
        {
            m_menu_was_open = menu_open;
            set_ready_for_display( menu_open, true );
        }

        if ( ( ++m_state_assert_throttle % 60 ) == 0 )
        {
            set_ready_for_display( menu_open, false );
        }
    }	void model_preview::set_ready_for_display( bool ready, bool kick )
	{
		if ( !m_ui_engine || !m_script_panel )
		{
			return;
		}

		// keep all xor_string objects alive across the call; crypt_get() points
		// into the object's storage
		auto kick_obj = xorstr( k_kick_script );
		auto ready_obj = xorstr( k_ready_script );
		auto pause_obj = xorstr( k_pause_script );

		const auto* script = ready
			? ( kick ? kick_obj.crypt_get( ) : ready_obj.crypt_get( ) )
			: pause_obj.crypt_get( );

		memory::call_vfunc<void>(
			m_ui_engine,
			77,
			reinterpret_cast<void*>( m_script_panel ),
			static_cast<const char*>( script ),
			"",
			static_cast<std::uint64_t>( 1 ) );
	}	void model_preview::clear_texture( )
	{
		if ( m_current_texture )
		{
			logging::console::print( "[model_preview] clearing texture (was 0x{:X})", reinterpret_cast<std::uintptr_t>( m_current_texture ) );
		}
		
		if ( const auto tex = static_cast< ID3D11ShaderResourceView* >( m_current_texture ) )
		{
			tex->Release( );
		}

		m_current_texture = nullptr;
	}

	void model_preview::reset( )
	{
		logging::console::print( "[model_preview] reset() called" );
		clear_texture( );

		m_preview_pawn = 0;
		m_panel = 0;
		m_script_injected = false;
		m_ui_engine = 0;
		m_script_panel = 0;
		m_panel_walk_throttle = 0;
		m_state_assert_throttle = 0;
		m_menu_was_open = true;
		m_init_throttle = 0;
	}

	void model_preview::shutdown( )
	{
		reset( );
	}

    bool model_preview::on_generate_primitives(
        std::uintptr_t owner_entity,
        std::uint32_t owner_hash,
        std::uintptr_t scene_object,
        std::uintptr_t primitive_buffer,
        void( __fastcall* original_fn )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t ),
        std::uintptr_t a1,
        std::uintptr_t scene_view )
    {
        if ( !m_initialized || !owner_entity )
        {
            return false;
        }

        if ( owner_hash != "C_CSGO_PreviewPlayer"_hash )
        {
            return false;
        }

        m_preview_pawn = owner_entity;

        // Применяем чамсы к превью игрока, используя настройки для enemy/team в зависимости от активной вкладки
        const auto& chams_cfg = settings::g_esp.m_player.m_chams;
        const auto subtab = rendering::g_menu.get_subtab( );
        
        // subtab 0 = Enemy, subtab 1 = Team, subtab 2 = Local
        const settings::esp::chams_config* target{ nullptr };
        
        if ( subtab == 0 )
        {
            target = &chams_cfg.enemy;
        }
        else if ( subtab == 1 )
        {
            target = &chams_cfg.team;
        }
        else if ( subtab == 2 )
        {
            target = &chams_cfg.local;
        }
        
        // Если настройки чамсов не включены, просто возвращаем false
        if ( !target || !target->enabled.value )
        {
            return false;
        }
        
        // Применяем чамсы через систему player chams
        // Создаем экземпляр chams и используем его логику применения
        const auto apply_preview_chams = [ & ]( const settings::esp::chams_config& cfg )
        {
            if ( cfg.occluded.enabled.value )
            {
                features::esp::player::g_chams.apply_layer( primitive_buffer, original_fn, a1, scene_object, scene_view, cfg.occluded.color, cfg.occluded.material );
            }

            if ( cfg.visible.enabled.value )
            {
                features::esp::player::g_chams.apply_layer( primitive_buffer, original_fn, a1, scene_object, scene_view, cfg.visible.color, cfg.visible.material );
            }

            if ( !cfg.visible.enabled.value && !cfg.occluded.enabled.value )
            {
                original_fn( a1, scene_object, scene_view, primitive_buffer );
            }
        };
        
        apply_preview_chams( *target );
        
        return true;
    }	void model_preview::set_preview_texture( void* srv )
	{
		static auto s_logged{ false };
		if ( srv && !s_logged )
		{
			s_logged = true;
			logging::console::print( "[model_preview] composition texture captured" );
		}
		
		if ( !srv && m_current_texture )
		{
			logging::console::print( "[model_preview] WARNING: texture set to null (was 0x{:X})", reinterpret_cast<std::uintptr_t>( m_current_texture ) );
		}

		m_current_texture = srv;
	}
}
