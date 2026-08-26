#include <external/xorstr.hpp>

#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace {

	void set_command_button( systems::input::usercmd* cmd, std::uintptr_t button, bool pressed )
	{
		if ( pressed )
		{
			cmd->buttons.value |= button;
			cmd->buttons.value_changed |= button;
			cmd->buttons.value_scroll |= button;
		}
		else
		{
			cmd->buttons.value &= ~button;
			cmd->buttons.value_changed |= button;
			cmd->buttons.value_scroll &= ~button;
		}
	}

	// same as math::helpers::angle_distance, but without the final sqrt —
	// the comparison against a threshold is identical on the squared value.
	float angle_distance_sqr( const math::vector3& from, const math::vector3& to )
	{
		const auto pitch = to.x - from.x;
		const auto yaw = math::helpers::normalize_yaw( to.y - from.y );
		return pitch * pitch + yaw * yaw;
	}

} 

namespace features::combat {

	void rage::on_create_move( systems::input::usercmd* cmd )
	{
		auto& ctx = g_shared.ctx( );
		const auto local = systems::g_local.get( );
		this->update_penetration_crosshair( local );

		if ( !ctx.valid )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		this->m_should_stop = false;
		this->m_firing_this_tick = false;

		if ( !settings::g_combat.m_duckpeek.enabled.value )
		{
			this->m_release_duck_for_shot = false;
			this->m_duckpeek_reduck = false;
		}

		if ( this->m_zeus_fired )
		{
			this->m_zeus_fired = false;

			if ( settings::g_combat.m_zeusbot.drop_after && !systems::g_local.is_in_deathmatch( ) )
			{
				memory::call<void>(PATTERN (patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, "drop", 0x7ffef001 );
			}

			return;
		}

		const auto is_knife = ctx.weapon_type == cstypes::weapon_type::knife;
		const auto is_taser = ctx.weapon_type == cstypes::weapon_type::taser;
		const auto is_revolver = ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver;

		if ( !is_knife && !is_taser && ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg ) )
		{
			return;
		}

		auto aim_ctx = this->build_context( cmd, local );

		if ( !is_revolver )
		{
			this->m_revolver_cock_ticks = 0;

			if ( !g_shared.can_shoot( cmd, local.controller ) )
			{
				return;
			}
		}

		if ( is_knife )
		{
			this->run_knife( cmd, aim_ctx, local );
		}
		else if ( is_taser )
		{
			this->run_taser( cmd, aim_ctx, local );
		}
		else if ( is_revolver )
		{
			this->auto_revolver( cmd, aim_ctx, local );
		}
		else
		{
			this->run_gun( cmd, aim_ctx, local );
		}
	}

	void rage::on_render( xdraw::draw_list& draw_list )
	{
		this->draw_penetration_crosshair( draw_list );
	}

	rage::aim_context rage::build_context( systems::input::usercmd* cmd, const systems::local::snapshot& local ) const
	{
		auto& ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );

		aim_context out{};
		out.predicted_inaccuracy = g_shared.get_inaccuracy( true );

		systems::g_prediction.simulate( cmd, local, [ & ]
			{
				g_shared.sh( ).snapshot( local.pawn, ctx.weapon_services );

				out.velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
				out.spread = g_shared.get_spread( );
			} );

		ctx.spread = out.spread;
		ctx.inaccuracy = out.predicted_inaccuracy;

		out.view_angles = systems::g_input.get_view_angles( );
		out.on_ground = ( prestate.flags & cstypes::entity_flags::on_ground ) != 0;
		out.is_scoped = ctx.is_scoped;
		out.weapon_max_speed = ctx.weapon_max_speed;
		out.accurate_threshold = ctx.weapon_max_speed * 0.34f;

		return out;
	}

	std::vector<rage::candidate> rage::gather_candidates( const systems::local::snapshot& local, float max_fov, float max_distance_sq ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );

		const auto eye = g_shared.get_eye_position( local.pawn );
		const auto view_angles = systems::g_input.get_view_angles( );
		const auto max_fov_sq = ( max_fov + 15.0f ) * ( max_fov + 15.0f );

		std::vector<candidate> out;
		out.reserve( players.size( ) );

		this->m_extrapolated_records.clear( );
		this->m_extrapolated_records.reserve( players.size( ) );

		for ( const auto& p : players )
		{
			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}

			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( pawn == local.pawn )
			{
				continue;
			}

			const auto team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			const auto health = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
			{
				continue;
			}

			if ( memory::read<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_hash ) ) )
			{
				continue;
			}

			if ( max_fov < 180.0f )
			{
				const auto game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				if ( !game_scene_node )
				{
					continue;
				}

				const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
				const auto aim = math::helpers::calculate_angle( eye, origin );

				if ( angle_distance_sqr( view_angles, aim ) > max_fov_sq )
				{
					continue;
				}
			}

			auto records = g_shared.lc( ).get_valid_records( pawn );

			if ( records.empty( ) )
			{
				auto extrap = g_shared.lc( ).extrapolate( pawn );
				if ( !extrap.has_value( ) )
				{
					continue;
				}

				this->m_extrapolated_records.push_back( std::move( *extrap ) );
				records.push_back( &this->m_extrapolated_records.back( ) );
			}

			if ( max_distance_sq > 0.0f )
			{
				const auto& origin = systems::g_prediction.pre( ).origin;
				auto closest_sq = ( records.front( )->origin - origin ).length_sqr( );

				if ( records.size( ) > 1 )
				{
					closest_sq = std::min( closest_sq, ( records.back( )->origin - origin ).length_sqr( ) );
				}

				if ( closest_sq > max_distance_sq )
				{
					continue;
				}
			}

			candidate c{};
			c.pawn = pawn;
			c.health = health;
			c.armor = memory::read<int>( pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );
			c.min_damage = 1.0f;

			c.records[ 0 ] = records[ 0 ];
			c.record_count = 1;

			if ( records.size( ) > 1 && ( records.front( )->origin - records.back( )->origin ).length_sqr( ) > 4.0f )
			{
				c.records[ 1 ] = records.back( );
				c.record_count = 2;
			}

			if ( shared_ctx.weapon_type >= cstypes::weapon_type::pistol && shared_ctx.weapon_type <= cstypes::weapon_type::lmg )
			{
				const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
				c.min_damage = this->get_min_damage( config, health, config.min_damage_override.value );
			}

			out.push_back( c );
		}

		return out;
	}

	void rage::run_gun( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local, bool allow_fire )
	{
		if ( !settings::g_combat.m_ragebot.enabled )
		{
			return;
		}

		auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		const auto autostop_enabled = config.autostop.value;

		auto candidates = this->gather_candidates( local, config.max_fov.value );

		if ( candidates.empty( ) )
		{
			this->m_release_duck_for_shot = false;
			return;
		}

		auto eye_candidates = this->get_eye_candidates( );

		auto lineup_pos = this->find_auto_lineup( ctx, candidates, local );
		if ( lineup_pos.has_value( ) && lineup_pos->valid )
		{
			eye_candidates.entries[ 0 ].position = lineup_pos->position;
			eye_candidates.count = 1;
		}

		const auto scan_from_eye_candidates = [ & ]( const math::vector3& eye_offset, float inaccuracy )
		{
			std::vector<scan_hit> hits_out;

			for ( auto i = 0; i < eye_candidates.count; ++i )
			{
				const auto eye = eye_candidates.entries[ i ].position + eye_offset;
				auto hits = this->scan_players( eye, inaccuracy, ctx, candidates, local );
				auto found_direct{ false };

				for ( auto& hit : hits )
				{
					auto source_eye = eye_candidates.entries[ i ];
					source_eye.position = eye;
					hit.source_eye = source_eye;
					found_direct = found_direct || !hit.penetrated;
					hits_out.push_back( std::move( hit ) );
				}

				if ( found_direct )
				{
					break;
				}
			}

			return hits_out;
		};

		if ( config.no_spread.value )
		{
			shared_ctx.inaccuracy = g_shared.get_inaccuracy( false );
			auto all_hits = scan_from_eye_candidates( {}, shared_ctx.inaccuracy );

			if ( all_hits.empty( ) )
			{
				this->m_release_duck_for_shot = false;
				return;
			}

			const auto best = this->select_best( ctx, all_hits, shared_ctx.inaccuracy );
			if ( !best.valid )
			{
				this->m_release_duck_for_shot = false;
				return;
			}

			if ( !allow_fire )
			{
				return;
			}

			// Duck-peek и для no-spread: встаём перед выстрелом, приседаем сразу после.
			const auto& prestate = systems::g_prediction.pre( );
			const auto ducking = ( prestate.flags & cstypes::entity_flags::ducking ) != 0;
			const auto duckpeek_active = settings::g_combat.m_duckpeek.enabled.value && ctx.on_ground;

			if ( duckpeek_active )
			{
				this->m_release_duck_for_shot = true;
			}

			const auto ready_to_fire = !duckpeek_active || ( !ducking && this->m_release_duck_for_shot );

			if ( !ready_to_fire )
			{
				return;
			}

			this->fire_gun( cmd, best, best.hit.source_eye.position, local );

			if ( duckpeek_active )
			{
				this->m_duckpeek_reduck = true;
				this->m_release_duck_for_shot = false;
			}

			return;
		}

		const auto primary_eye = eye_candidates.entries[ 0 ].position;
		const auto& prestate = systems::g_prediction.pre( );

		auto current_hits = scan_from_eye_candidates( {}, ctx.predicted_inaccuracy );
		const auto best = this->select_best( ctx, current_hits, ctx.predicted_inaccuracy );

		if ( settings::g_combat.m_autos.scope.value && shared_ctx.weapon_type == cstypes::weapon_type::sniper && !shared_ctx.is_scoped && best.valid && !( cmd->buttons.value & cstypes::command_buttons::in_second_attack ) )
		{
			set_command_button( cmd, cstypes::command_buttons::in_second_attack, true );
		}

		const auto needed_hc = config.hitchance_override.value ? static_cast< float >( config.hitchance_override_value ) / 100.0f : static_cast< float >( config.hitchance ) / 100.0f;
		const auto duckpeek_active = settings::g_combat.m_duckpeek.enabled.value && ctx.on_ground;
		const auto is_ducked = ( prestate.flags & cstypes::entity_flags::ducking ) != 0;

		const auto standing_inaccuracy = duckpeek_active ? this->get_standing_inaccuracy( local, ctx ) : ctx.predicted_inaccuracy;
		const auto standing_hc = best.valid
			? ( duckpeek_active ? this->evaluate_hitchance( best.hit, ctx, standing_inaccuracy ) : best.hitchance )
			: 0.0f;

		const auto accurate = best.valid && standing_hc >= needed_hc;
		const auto max_acc = g_shared.is_max_accuracy( standing_inaccuracy );
		const auto force = best.valid && config.force_shot.value && max_acc;
		auto shot_viable = accurate || force;

		if ( best.valid )
		{
			const auto& weapon_vdata = g_shared.ctx( ).weapon_vdata;
			if ( weapon_vdata )
			{
				const auto full_vel = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
				auto real_vel = full_vel;
				real_vel.z = 0.0f;
				const auto real_speed_2d = real_vel.length_2d( );

				const auto inacc_stand = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_hash ) );
				const auto inacc_floor = std::max( inacc_stand, 0.004f );
				const auto real_inacc = g_shared.get_inaccuracy_at_velocity( local.pawn, real_vel );

				const auto too_inaccurate = real_inacc > inacc_floor * 2.0f + 0.003f;
				const auto too_fast = ctx.weapon_max_speed > 0.0f && real_speed_2d > ctx.weapon_max_speed * 0.20f;

				// airborne shots are only reliable right around the jump apex — the
				// jump spread grows quickly away from it. the reconstructed
				// get_air_inaccuracy under-estimates the real engine value (it
				// reports ~0 until |vz| > ~19 u/s), so measure the real airborne
				// inaccuracy with the full velocity instead. a small tolerance keeps
				// the bot from being locked to a single tick of the jump
				const auto airborne_unshootable = ( prestate.flags & cstypes::entity_flags::on_ground ) == 0
					&& [ & ]( ) -> bool
					{
						const auto inac_jump_apex = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );
						const auto accuracy_penalty = memory::read<float>( g_shared.ctx( ).weapon + SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_hash ) );
						const auto min_air_inaccuracy = accuracy_penalty + inac_jump_apex;
						const auto real_air_inaccuracy = g_shared.get_inaccuracy_at_velocity( local.pawn, full_vel );

						constexpr auto air_tolerance{ 0.003f };
						return real_air_inaccuracy > min_air_inaccuracy + air_tolerance;
					}( );

				if ( too_inaccurate || too_fast || airborne_unshootable )
				{
					shot_viable = false;
				}
			}
		}

		if ( autostop_enabled && !shot_viable && this->should_stop_movement( ctx ) )
		{
			const auto stop = this->predict_stop( ctx, primary_eye, local );
			if ( stop )
			{
				const auto future_offset = stop->eye - primary_eye;
				auto planned_hits = scan_from_eye_candidates( future_offset, stop->inaccuracy );
				const auto planned = this->select_best( ctx, planned_hits, stop->inaccuracy );
				this->m_should_stop = planned.valid;
			}
			else
			{
				this->m_should_stop = best.valid;
			}
		}

		if ( !best.valid )
		{
			if ( duckpeek_active )
			{
				this->m_release_duck_for_shot = false;
			}

			return;
		}

		// Duck-peek: держим ctrl отжатым (встаём), пока есть точный выстрел, и нажимаем его сразу после выстрела.
		// Флаг защёлкивается, чтобы колебания hit-chance во время анимации вставания не прерывали его.
		if ( duckpeek_active && allow_fire )
		{
			if ( shot_viable )
			{
				this->m_release_duck_for_shot = true;
			}
			else if ( !this->m_duckpeek_reduck )
			{
				this->m_release_duck_for_shot = false;
			}
		}

		auto ready_to_fire = shot_viable;
		if ( duckpeek_active )
		{
			if ( is_ducked )
			{
				ready_to_fire = false;
			}
			else
			{
				ready_to_fire = ready_to_fire && this->m_release_duck_for_shot;
			}
		}

		if ( ready_to_fire && allow_fire )
		{
			this->fire_gun( cmd, best, best.hit.source_eye.position, local );

			if ( duckpeek_active )
			{
				this->m_duckpeek_reduck = true;
				this->m_release_duck_for_shot = false;
			}
		}
	}

	void rage::run_taser( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_zeusbot.enabled )
		{
			return;
		}

		auto candidates = this->gather_candidates( local, settings::g_combat.m_zeusbot.max_fov.value );

		if ( candidates.empty( ) )
		{
			return;
		}

		auto eye_candidates = this->get_eye_candidates( );

		std::vector<scan_hit> all_hits;

		for ( auto i = 0; i < eye_candidates.count; ++i )
		{
			auto hits = this->scan_taser( eye_candidates.entries[ i ].position, ctx, candidates, local );

			for ( auto& h : hits )
			{
				h.source_eye = eye_candidates.entries[ i ];
				all_hits.push_back( std::move( h ) );
			}
		}

		if ( all_hits.empty( ) )
		{
			return;
		}

		target best{};

		for ( const auto& h : all_hits )
		{
			if ( !best.valid || h.score > best.score )
			{
				best.hit = h;
				best.hitchance = 1.0f;
				best.score = h.score;
				best.valid = true;
			}
		}

		this->m_zeus_fired = this->fire_melee( cmd, best, local );
	}

	void rage::run_knife( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !settings::g_combat.m_knifebot.enabled )
		{
			return;
		}

		const auto info = this->get_knife_info( local );
		if ( !info.can_slash && !info.can_stab )
		{
			return;
		}

		constexpr auto max_knife_dist_sq = 150.0f * 150.0f;
		auto candidates = this->gather_candidates( local, settings::g_combat.m_knifebot.max_fov.value, max_knife_dist_sq );
		if ( candidates.empty( ) )
		{
			return;
		}

		auto eye_candidates = this->get_eye_candidates( );

		std::vector<scan_hit> all_hits;

		for ( auto i = 0; i < eye_candidates.count; ++i )
		{
			auto hits = this->scan_knife( eye_candidates.entries[ i ].position, ctx, info, candidates, local );

			for ( auto& h : hits )
			{
				h.source_eye = eye_candidates.entries[ i ];
				all_hits.push_back( std::move( h ) );
			}
		}

		if ( all_hits.empty( ) )
		{
			return;
		}

		target best{};
		target best_backstab{};

		for ( const auto& h : all_hits )
		{
			auto& dest = h.is_backstab ? best_backstab : best;

			if ( !dest.valid || h.score > dest.score )
			{
				dest.hit = h;
				dest.hitchance = 1.0f;
				dest.score = h.score;
				dest.valid = true;
			}
		}

		auto& chosen = best_backstab.valid ? best_backstab : best;
		this->fire_melee( cmd, chosen, local );
	}

	void rage::auto_revolver( systems::input::usercmd* cmd, const aim_context& ctx, const systems::local::snapshot& local )
	{
		if ( !g_shared.can_shoot( cmd, local.controller ) || !settings::g_combat.m_autos.revolver.value )
		{
			this->m_revolver_cock_ticks = 0;
			return;
		}

		constexpr auto cock_ticks{ 13 };
		if ( this->m_revolver_cock_ticks >= cock_ticks )
		{
			set_command_button( cmd, cstypes::command_buttons::in_attack, false );
			cmd->csgo_user_cmd.set_attack1_start_history_index( -1 );
			this->m_revolver_cock_ticks = 0;

			this->run_gun( cmd, ctx, local );
			return;
		}

		this->run_gun( cmd, ctx, local, false );

		set_command_button( cmd, cstypes::command_buttons::in_attack, true );

		const auto history_index = cmd->csgo_user_cmd.input_history_size( ) - 1;
		if ( history_index >= 0 )
		{
			cmd->csgo_user_cmd.set_attack1_start_history_index( history_index );
		}

		++this->m_revolver_cock_ticks;
	}

	std::vector<rage::scan_hit> rage::scan_players( const math::vector3& eye, float inaccuracy, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		std::vector<std::vector<scan_hit>> per_candidate( candidates.size( ) );

		threadpool::parallel_for( 0, static_cast< int >( candidates.size( ) ), [ & ]( int begin, int end )
			{
				for ( auto ci = begin; ci < end; ++ci )
				{
					auto& cand = candidates[ ci ];
					auto& candidate_hits = per_candidate[ ci ];
					candidate_hits.reserve( 36 );

					const auto state = this->prepare_player( cand );
					if ( state.hitbox_set.count <= 0 )
					{
						continue;
					}

					for ( auto ri = 0; ri < cand.record_count; ++ri )
					{
						if ( !cand.records[ ri ]->valid )
						{
							continue;
						}

						auto hits = this->scan_record( state, eye, inaccuracy, ctx, cand, cand.records[ ri ], local );
						const auto has_direct_hit = std::any_of( hits.begin( ), hits.end( ), [ ]( const scan_hit& hit )
							{
								return !hit.penetrated;
							} );

						for ( auto& h : hits )
						{
							candidate_hits.push_back( std::move( h ) );
						}

						// выстрел по свежей записи надёжнее и дешевле, чем перебор старых поз
						if ( has_direct_hit )
						{
							break;
						}
					}
				}
			}, 2 );

		std::vector<scan_hit> flat;
		flat.reserve( candidates.size( ) * 36 );

		for ( auto& v : per_candidate )
		{
			for ( auto& h : v )
			{
				flat.push_back( std::move( h ) );
			}
		}

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		if ( config.auto_lineup.value && this->m_auto_lineup.initialized )
		{
			constexpr auto test_per_frame{ 2 };
			auto tested{ 0 };

			for ( const auto& cand : candidates )
			{
				if ( tested >= test_per_frame )
				{
					break;
				}

				const auto lineup_origin = this->get_lineup_origin( cand, local );
				lineup_position pos{};
				if ( this->test_lineup_position( lineup_origin, ctx, cand, local, pos ) )
				{
					auto exists = std::any_of( this->m_auto_lineup.positions.begin( ), this->m_auto_lineup.positions.end( ),
						[ & ]( const lineup_position& p ) { return ( p.position - pos.position ).length_sqr( ) < 64.0f; } );

					if ( !exists )
					{
						this->m_auto_lineup.positions.push_back( pos );
					}
				}
				++tested;
			}
		}

		return flat;
	}

	rage::player_scan_state rage::prepare_player( candidate& cand ) const
	{
		player_scan_state state{};

		const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return state;
		}

		state.hitbox_set = systems::g_hitboxes.query( game_scene_node );

		for ( const auto& entry : state.hitbox_set )
		{
			if ( entry.index < static_cast< int >( state.hitbox_by_index.size( ) ) )
			{
				state.hitbox_by_index[ entry.index ].entry = entry;
				state.hitbox_by_index[ entry.index ].valid = true;
			}
		}

		const auto& shared_ctx = g_shared.ctx( );
		state.config = &settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		state.force_body = state.config->body_aim.value;

		if ( !state.force_body && state.config->hitboxes.values[ 0 ] )
		{
			state.scan_order[ state.scan_count++ ] = 0;
		}

		if ( state.config->hitboxes.values[ 1 ] )
		{
			state.scan_order[ state.scan_count++ ] = 4;
			state.scan_order[ state.scan_count++ ] = 5;
			state.scan_order[ state.scan_count++ ] = 6;
		}

		if ( state.config->hitboxes.values[ 2 ] )
		{
			state.scan_order[ state.scan_count++ ] = 3;
			state.scan_order[ state.scan_count++ ] = 2;
		}

		if ( state.config->hitboxes.values[ 3 ] )
		{
			for ( auto idx : { 13, 14, 15, 16, 17, 18 } )
			{
				state.scan_order[ state.scan_count++ ] = idx;
			}
		}

		if ( state.config->hitboxes.values[ 4 ] )
		{
			for ( auto idx : { 7, 8, 9, 10 } )
			{
				state.scan_order[ state.scan_count++ ] = idx;
			}
		}

		if ( state.config->hitboxes.values[ 5 ] )
		{
			for ( auto idx : { 11, 12 } )
			{
				state.scan_order[ state.scan_count++ ] = idx;
			}
		}

		if ( state.scan_count == 0 )
		{
			if ( !state.force_body )
			{
				state.scan_order[ state.scan_count++ ] = 0;
			}

			for ( auto idx : { 4, 5, 6, 3, 2 } )
			{
				state.scan_order[ state.scan_count++ ] = idx;
			}
		}

		state.pen_static = g_shared.pen( ).prepare_target_static( cand.pawn );

		return state;
	}

	std::vector<rage::scan_hit> rage::scan_record( const player_scan_state& state, const math::vector3& eye, float inaccuracy, const aim_context& ctx, candidate& cand, shared::lagcomp::record* record, const systems::local::snapshot& local ) const
	{
		const auto& config = *state.config;
		const auto skeleton = g_shared.lc( ).get_skeleton( *record );
		const auto pen_ctx = g_shared.pen( ).prepare_target( cand.pawn, record, state.pen_static );

		struct trace_point
		{
			math::vector3 position;
			int hitbox_index;
			int bone_index;
			systems::hitboxes::entry hitbox;
			bool is_center;
		};

		const auto max_fov_sq = config.max_fov * config.max_fov;

		std::vector<trace_point> points;
		points.reserve( static_cast< std::size_t >( state.scan_count ) * 12 );

		// reused across hitboxes to avoid an allocation per multipoint batch
		std::vector<math::vector3> multipoints;

		for ( auto idx = 0; idx < state.scan_count; ++idx )
		{
			const auto hitbox_index = state.scan_order[ idx ];
			if ( hitbox_index >= static_cast< int >( state.hitbox_by_index.size( ) ) || !state.hitbox_by_index[ hitbox_index ].valid )
			{
				continue;
			}

			const auto& hb = state.hitbox_by_index[ hitbox_index ].entry;
			if ( hb.bone >= 27 || hb.bone >= record->bone_count )
			{
				continue;
			}

			const auto& bone = skeleton[ hb.bone ];

			const auto hitbox_center = ( hb.mins + hb.maxs ) * 0.5f;
			const auto center = bone.rotation.rotate_vector( hitbox_center ) + bone.position;

			trace_point cp{};
			cp.position = center;
			cp.hitbox_index = hitbox_index;
			cp.bone_index = hb.bone;
			cp.hitbox = hb;
			cp.is_center = true;
			points.push_back( cp );

			if ( config.pointscale > 0.0f )
			{
				multipoints.clear( );
				this->generate_multipoints( hb, center, bone.rotation, config.pointscale, eye, inaccuracy, multipoints );

				for ( const auto& mp : multipoints )
				{
					trace_point tp{};
					tp.position = mp;
					tp.hitbox_index = hitbox_index;
					tp.bone_index = hb.bone;
					tp.hitbox = hb;
					tp.is_center = false;
					points.push_back( tp );
				}
			}
		}

		if ( points.empty( ) )
		{
			return {};
		}

		std::vector<scan_hit> results;
		results.reserve( points.size( ) );

		for ( auto pi = 0u; pi < points.size( ); ++pi )
		{
			const auto& tp = points[ pi ];

			const auto aim = math::helpers::calculate_angle( eye, tp.position );

			if ( angle_distance_sqr( ctx.view_angles, aim ) > max_fov_sq )
			{
				continue;
			}

			shared::penetration::result pen{};
			if ( !g_shared.pen( ).run( eye, tp.position, pen_ctx, local.pawn, local.team, pen ) )
			{
				continue;
			}

			if ( pen.damage < cand.min_damage )
			{
				continue;
			}

			auto resolved_hitbox = tp.hitbox;
			auto resolved_hitbox_index = tp.hitbox_index;
			auto resolved_bone_index = tp.bone_index;
			auto resolved_position = tp.position;

			if ( pen.hitbox != tp.hitbox_index )
			{
				if ( pen.hitbox < 0 || pen.hitbox >= static_cast< int >( state.hitbox_by_index.size( ) ) || !state.hitbox_by_index[ pen.hitbox ].valid )
				{
					continue;
				}

				const auto& actual_hitbox = state.hitbox_by_index[ pen.hitbox ].entry;
				if ( actual_hitbox.bone >= 27 || actual_hitbox.bone >= record->bone_count )
				{
					continue;
				}

				resolved_hitbox = actual_hitbox;
				resolved_hitbox_index = actual_hitbox.index;
				resolved_bone_index = actual_hitbox.bone;

				const auto& actual_bone = skeleton[ resolved_bone_index ];
				resolved_position = actual_bone.rotation.rotate_vector( ( actual_hitbox.mins + actual_hitbox.maxs ) * 0.5f ) + actual_bone.position;
			}

			const auto resolved_aim = math::helpers::calculate_angle( eye, resolved_position );
			scan_hit h{};
			h.position = resolved_position;
			h.aim_angle = resolved_aim;
			h.damage = pen.damage;
			h.fov = math::helpers::angle_distance( ctx.view_angles, resolved_aim );
			h.hitbox_index = resolved_hitbox_index;
			h.hitgroup = pen.hitgroup;
			h.bone_index = resolved_bone_index;
			h.hitbox = resolved_hitbox;
			h.is_center = tp.is_center && pen.hitbox == tp.hitbox_index;
			h.penetrated = pen.penetrated;
			h.pawn = cand.pawn;
			h.health = cand.health;
			h.record = record;

			results.push_back( h );
		}

		return results;
	}

	rage::target rage::select_best( const aim_context& aim_ctx, const std::vector<scan_hit>& hits, float eval_inaccuracy ) const
	{
		static constexpr int hitgroup_priority_table[ 19 ] =
		{
			4,
			3, 3, 3, 3, 3, 3,
			1, 1, 1, 1,
			1, 1,
			2, 2, 2, 2, 2, 2
		};

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		const auto needed_hc = config.hitchance_override.value
			? static_cast< float >( config.hitchance_override_value ) / 100.0f
			: static_cast< float >( config.hitchance ) / 100.0f;

		const auto score_for = [ & ]( const scan_hit& h, float hc )
		{
			const auto priority = static_cast< float >( hitgroup_priority_table[ h.hitbox_index ] );

			auto score = 1000000.0f;

			if ( h.damage >= static_cast< float >( h.health ) )
			{
				score += 100000.0f + hc * 15000.0f + priority * 2500.0f;
			}
			else
			{
				score += h.damage * hc * hc * 150.0f + h.damage * 5.0f;
			}

			score += h.penetrated ? 0.0f : 5000.0f;
			score += h.is_center ? 50.0f : 0.0f;
			score += priority * 25.0f;
			score -= h.fov * 0.1f;

			return score;
		};

		const auto upper_bound_score = [ & ]( const scan_hit& h ) { return score_for( h, 1.0f ); };

		target best{};
		target best_any{};

		for ( const auto& h : hits )
		{
			if ( !h.record || !h.record->valid )
			{
				continue;
			}

			if ( h.bone_index >= 28 || h.bone_index >= h.record->bone_count )
			{
				continue;
			}

			if ( best.valid && upper_bound_score( h ) <= best.score )
			{
				continue;
			}

			const auto& bone = h.record->bones[ h.bone_index ];
			auto hc{ 1.0f };
			if ( !config.no_spread.value )
			{
				hc = g_shared.calculate_hitchance( h.source_eye.position, h.aim_angle, h.hitbox, bone, eval_inaccuracy, aim_ctx.spread, 256, needed_hc );
			}
			const auto passes_hitchance = config.no_spread.value || hc >= needed_hc;

			const auto score = score_for( h, hc );

			if ( !best_any.valid || score > best_any.score )
			{
				best_any.hit = h;
				best_any.hitchance = hc;
				best_any.score = score;
				best_any.valid = true;
			}

			if ( passes_hitchance && ( !best.valid || score > best.score ) )
			{
				best.hit = h;
				best.hitchance = hc;
				best.score = score;
				best.valid = true;
			}
		}

		return best.valid ? best : best_any;
	}

	shared::shoot_history::eye_candidates rage::get_eye_candidates( ) const
	{
		auto eye_candidates = g_shared.sh( ).get_candidates( );

		// the server uses the interpolated shoot position as the bullet origin; the
		// raw eye (both the ring candidates and the plain fallback) can be up to a
		// lerp worth of movement ahead of it on jumpscout / moving shots, which
		// shifts the aim ray off the hitbox at range — so always aim from the
		// interpolated position
		const auto local_pawn = systems::g_local.get( ).pawn;
		const auto interpolated_eye = local_pawn
			? g_shared.get_interpolated_shoot_position( local_pawn )
			: math::vector3{};

		if ( eye_candidates.count > 0 )
		{
			eye_candidates.entries[ 0 ].position = interpolated_eye;
		}
		else if ( interpolated_eye.length_sqr( ) > 1.0f )
		{
			eye_candidates.entries[ 0 ].position = interpolated_eye;
			eye_candidates.entries[ 0 ].is_uninterpolated = true;
			eye_candidates.count = 1;
		}

		return eye_candidates;
	}

	std::optional<rage::stop_prediction> rage::predict_stop( const aim_context& ctx, const math::vector3& current_eye, const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );
		const auto speed = prestate.networked_velocity.length_2d( );
		const auto will_stop = ctx.on_ground && ( speed > ctx.accurate_threshold || ( ctx.is_scoped && speed > 1.0f ) );

		if ( !will_stop )
		{
			return std::nullopt;
		}

		auto sim_vel = prestate.networked_velocity;
		sim_vel.z = 0.0f;

		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );
		const auto sv_accelerate = CONVAR ("sv_accelerate")->get<float>( );
		const auto surface_friction = prestate.surface_friction;

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		const auto max_move_speed = movement_services ? memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) ) : 250.0f;

		for ( auto i = 0; i < 15; ++i )
		{
			const auto sim_speed = sim_vel.length_2d( );
			if ( sim_speed < 1.0f )
			{
				break;
			}

			const auto control = std::fmaxf( sim_speed, sv_stopspeed );
			const auto drop = sv_friction * surface_friction * control * cstypes::tick_interval;
			auto new_speed = std::fmaxf( sim_speed - drop, 0.0f );
			auto accel = sv_accelerate;

			if ( shared_ctx.is_scoped )
			{
				const auto weapon_ratio = std::fminf( 1.0f, shared_ctx.weapon_max_speed / 250.0f );
				const auto scoped_max = std::fmaxf( 250.0f, max_move_speed ) * weapon_ratio * 0.52f;

				if ( new_speed > scoped_max - 5.0f )
				{
					const auto t = 1.0f - std::fmaxf( 0.0f, new_speed - ( scoped_max - 5.0f ) ) / std::fmaxf( 0.01f, 5.0f );
					accel *= std::clamp( t, 0.0f, 1.0f );
				}
			}

			const auto accel_speed = std::fminf( accel * shared_ctx.weapon_max_speed * surface_friction * cstypes::tick_interval, new_speed );
			new_speed = std::fmaxf( new_speed - accel_speed, 0.0f );

			if ( new_speed > 0.0f )
			{
				sim_vel *= ( new_speed / sim_speed );
			}
			else
			{
				sim_vel = {};
				break;
			}
		}

		const auto avg_vel = ( prestate.networked_velocity + sim_vel ) * 0.5f;
		const auto stop_ticks = g_shared.calculate_stop_ticks( prestate.networked_velocity, shared_ctx.weapon_max_speed, local.pawn );
		const auto stop_time = static_cast< float >( stop_ticks ) * cstypes::tick_interval;

		return stop_prediction
		{
			.eye =
			{
				current_eye.x + avg_vel.x * stop_time,
				current_eye.y + avg_vel.y * stop_time,
				current_eye.z
			},
			.inaccuracy = g_shared.get_inaccuracy_at_velocity( local.pawn, sim_vel )
		};
	}

	bool rage::should_stop_movement( const aim_context& ctx ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto& prestate = systems::g_prediction.pre( );
		const auto velocity = prestate.networked_velocity;

		if ( shared_ctx.weapon_type == cstypes::weapon_type::sniper && !ctx.is_scoped )
		{
			return false;
		}

		if ( ctx.on_ground )
		{
			const auto speed_2d = velocity.length_2d( );
			if ( speed_2d <= 0.1f )
			{
				return false;
			}

			const auto inaccuracy_move = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyMove"_hash ) );
			const auto inaccuracy_stand = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_hash ) );

			return speed_2d * inaccuracy_move > inaccuracy_stand;
		}

		if ( shared_ctx.weapon_type != cstypes::weapon_type::sniper )
		{
			return false;
		}

		if ( velocity.z > 140.0f )
		{
			return false;
		}

		const auto sv_gravity = CONVAR ("sv_gravity")->get<float>( );
		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );

		const auto inac_jump_initial = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_hash ) );
		const auto inac_jump_apex = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );
		const auto shootable_threshold = inac_jump_apex + 0.001f;
		const auto early_threshold = inac_jump_initial * 0.55f + inac_jump_apex * 0.45f;
		const auto air_inaccuracy = g_shared.get_air_inaccuracy( velocity.z, inac_jump_initial, inac_jump_apex );

		if ( air_inaccuracy <= shootable_threshold || air_inaccuracy <= early_threshold )
		{
			return true;
		}

		auto sim_vz = velocity.z;
		auto ticks_to_shootable{ 0 };

		for ( auto i = 1; i <= 32; ++i )
		{
			sim_vz -= sv_gravity * cstypes::tick_interval;

			if ( g_shared.get_air_inaccuracy( sim_vz, inac_jump_initial, inac_jump_apex ) <= shootable_threshold )
			{
				ticks_to_shootable = i;
				break;
			}
		}

		if ( ticks_to_shootable == 0 )
		{
			return false;
		}

		const auto speed_2d = velocity.length_2d( );
		const auto max_speed = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) );
		const auto accurate_threshold = max_speed * 0.34f;

		if ( speed_2d <= accurate_threshold )
		{
			return true;
		}

		auto sim_speed = speed_2d;
		auto ticks_to_stop{ 32 };

		for ( auto i = 1; i <= 32; ++i )
		{
			const auto drop = std::fmaxf( sim_speed, sv_stopspeed ) * sv_friction * cstypes::tick_interval;
			sim_speed -= drop;

			if ( sim_speed <= accurate_threshold )
			{
				ticks_to_stop = i;
				break;
			}
		}

		return ticks_to_shootable <= ticks_to_stop + 2;
	}

	float rage::evaluate_hitchance( const scan_hit& hit, const aim_context& ctx, float inaccuracy ) const
	{
		if ( !hit.record || !hit.record->valid || hit.bone_index < 0 || hit.bone_index >= 28 )
		{
			return 0.0f;
		}

		return g_shared.calculate_hitchance( hit.source_eye.position, hit.aim_angle, hit.hitbox, hit.record->bones[ hit.bone_index ], inaccuracy, ctx.spread );
	}

	float rage::get_standing_inaccuracy( const systems::local::snapshot& local, const aim_context& ctx ) const
	{
		const auto& prestate = systems::g_prediction.pre( );
		auto velocity = prestate.networked_velocity;
		velocity.z = 0.0f;

		const auto speed = velocity.length_2d( );
		if ( speed > ctx.accurate_threshold )
		{
			return g_shared.get_inaccuracy_at_velocity( local.pawn, velocity );
		}

		const auto& shared_ctx = g_shared.ctx( );
		if ( !shared_ctx.weapon_vdata )
		{
			return ctx.predicted_inaccuracy;
		}

		const auto inaccuracy_stand = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_hash ) );
		return std::max( inaccuracy_stand, g_shared.get_inaccuracy_at_velocity( local.pawn, velocity ) );
	}

	std::vector<rage::scan_hit> rage::scan_taser( const math::vector3& eye, const aim_context& ctx, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		std::vector<scan_hit> results;

		for ( auto& cand : candidates )
		{
			const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( !game_scene_node )
			{
				continue;
			}

			const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
			if ( hitbox_set.count <= 0 )
			{
				continue;
			}

			for ( auto ri = 0; ri < cand.record_count; ++ri )
			{
				auto* record = cand.records[ ri ];
				if ( !record->valid )
				{
					continue;
				}

				const auto skeleton = g_shared.lc( ).get_skeleton( *record );

				for ( auto i = 0; i < hitbox_set.count; ++i )
				{
					const auto& hb = hitbox_set.entries[ i ];

					if ( hb.bone >= 27 || hb.bone >= record->bone_count )
					{
						continue;
					}

					const auto& bone = skeleton[ hb.bone ];

					const auto center = bone.rotation.rotate_vector( ( hb.mins + hb.maxs ) * 0.5f ) + bone.position;
					const auto aim = math::helpers::calculate_angle( eye, center );
					const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );

					if ( fov > settings::g_combat.m_zeusbot.max_fov )
					{
						continue;
					}

					math::vector3 forward{};
					math::helpers::angle_vectors_left( aim, &forward );

					const auto trace = this->trace_taser_hit( eye, forward, shared_ctx.range * 0.85f, cand.pawn, local.pawn );
					if ( trace.hit_entity != cand.pawn )
					{
						continue;
					}

					const auto dist = ( center - eye ).length( );
					const auto range_fraction = dist / shared_ctx.range;

					scan_hit h{};
					h.position = center;
					h.aim_angle = aim;
					h.damage = 500.0f;
					h.score = ( 10000.0f - dist ) * ( range_fraction > 0.92f ? 0.8f : 1.0f );
					h.fov = fov;
					h.hitbox_index = hb.index;
					h.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( hb.index );
					h.bone_index = hb.bone;
					h.hitbox = hb;
					h.is_center = true;
					h.pawn = cand.pawn;
					h.health = cand.health;
					h.record = record;

					results.push_back( h );
				}
			}
		}

		return results;
	}

	rage::knife_info rage::get_knife_info( const systems::local::snapshot& local ) const
	{
		const auto& shared_ctx = g_shared.ctx( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto next_primary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );
		const auto next_secondary = memory::read<int>( shared_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash ) );
		const auto last_shot_time = memory::read<float>( shared_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_fLastShotTime"_hash ) );
		const auto cur_time = static_cast< float >( tick_base ) * cstypes::tick_interval;

		return knife_info
		{
			.can_slash = tick_base >= next_primary,
			.can_stab = tick_base >= next_secondary,
			.charged = ( cur_time - last_shot_time ) > 0.4f,
			.armor_ratio = memory::read<float>( shared_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flArmorRatio"_hash ) )
		};
	}

	std::vector<rage::scan_hit> rage::scan_knife( const math::vector3& eye, const aim_context& ctx, const knife_info& info, std::vector<candidate>& candidates, const systems::local::snapshot& local ) const
	{
		constexpr auto stab_range{ 50.0f };
		constexpr auto slash_range{ 66.0f };

		std::vector<scan_hit> results;

		for ( auto& cand : candidates )
		{
			const auto eye_angles = memory::read<math::vector3>( cand.pawn + SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_hash ) );
			const auto hp = static_cast< float >( cand.health );

			const auto frontal_slash_dmg = this->get_knife_damage( info.charged ? 40.0f : 25.0f, cand.armor, info.armor_ratio );
			const auto frontal_stab_dmg = this->get_knife_damage( 65.0f, cand.armor, info.armor_ratio );
			const auto frontal_can_kill = ( info.can_slash && frontal_slash_dmg >= hp ) || ( info.can_stab && frontal_stab_dmg >= hp );

			const auto game_scene_node = memory::read<std::uintptr_t>( cand.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( !game_scene_node )
			{
				continue;
			}

			const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
			if ( hitbox_set.count <= 0 )
			{
				continue;
			}

			for ( auto ri = 0; ri < cand.record_count; ++ri )
			{
				auto* record = cand.records[ ri ];
				if ( !record->valid )
				{
					continue;
				}

				const auto skeleton = g_shared.lc( ).get_skeleton( *record );

				const auto delta = record->origin - systems::g_prediction.pre( ).origin;
				const auto dist_2d = delta.length_2d( );
				auto backstab = false;

				if ( dist_2d > 0.001f )
				{
					const auto dir = delta / dist_2d;

					math::vector3 body_forward{};
					math::helpers::angle_vectors_left( record->rotation, &body_forward );

					math::vector3 eye_forward{};
					math::helpers::angle_vectors_left( eye_angles, &eye_forward );

					backstab = ( dir.x * body_forward.x + dir.y * body_forward.y ) > 0.475f ||
						( dir.x * eye_forward.x + dir.y * eye_forward.y ) > 0.475f;
				}

				const auto wait_for_backstab = backstab && !frontal_can_kill;

				for ( auto i = 0; i < hitbox_set.count; ++i )
				{
					const auto& hb = hitbox_set.entries[ i ];

					if ( hb.bone >= 27 || hb.bone >= record->bone_count )
					{
						continue;
					}

					const auto& bone = skeleton[ hb.bone ];

					const auto center = bone.rotation.rotate_vector( ( hb.mins + hb.maxs ) * 0.5f ) + bone.position;
					const auto dist = ( center - eye ).length( );

					const auto aim = math::helpers::calculate_angle( eye, center );
					const auto fov = math::helpers::angle_distance( ctx.view_angles, aim );

					if ( fov > settings::g_combat.m_knifebot.max_fov )
					{
						continue;
					}

					math::vector3 forward{};
					math::helpers::angle_vectors_left( aim, &forward );

					for ( const auto try_stab : { true, false } )
					{
						if ( ( try_stab && !info.can_stab ) || ( !try_stab && !info.can_slash ) )
						{
							continue;
						}

						const auto reach = try_stab ? stab_range : slash_range;
						if ( dist > reach )
						{
							continue;
						}

						const auto raw_dmg = try_stab ? ( backstab ? 180.0f : 65.0f ) : ( backstab ? 90.0f : ( info.charged ? 40.0f : 25.0f ) );
						const auto damage = this->get_knife_damage( raw_dmg, cand.armor, info.armor_ratio );
						const auto can_kill = damage >= hp;

						if ( wait_for_backstab && !can_kill )
						{
							continue;
						}

						const auto trace = this->trace_knife_hit( eye, forward, reach, cand.pawn, local.pawn );
						if ( trace.hit_entity != cand.pawn )
						{
							continue;
						}

						const auto reach_margin = 1.0f - ( dist / reach );

						scan_hit h{};
						h.position = center;
						h.aim_angle = aim;
						h.damage = damage;
						h.score = can_kill ? ( 10000.0f + damage * reach_margin ) : ( damage * 100.0f * reach_margin );
						h.fov = fov;
						h.hitbox_index = hb.index;
						h.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( hb.index );
						h.bone_index = hb.bone;
						h.hitbox = hb;
						h.is_center = true;
						h.is_backstab = backstab;
						h.attack_type = try_stab ? 1 : 0;
						h.pawn = cand.pawn;
						h.health = cand.health;
						h.record = record;
						results.push_back( h );
						break;
					}
				}
			}
		}

		return results;
	}

	void rage::fire_gun( systems::input::usercmd* cmd, const target& tgt, const math::vector3& shoot_eye, const systems::local::snapshot& local )
	{
		if ( !tgt.hit.record || !tgt.hit.record->valid )
		{
			return;
		}

		this->m_firing_this_tick = true;

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto& shared_ctx = g_shared.ctx( );
		const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );
		auto aim_angle = config.no_spread.value ? math::helpers::calculate_angle( shoot_eye, tgt.hit.position ) : tgt.hit.aim_angle;

		const auto record_time = cstypes::tick_fraction::from_value( tgt.hit.record->simulation_time / cstypes::tick_interval );

		// the seed tick is the eye-stamp tick: the player tick of the
		// interpolated eye plus the lerp offset. it is written into the history
		// as player_tick_count so the server re-derives the spread seed from a
		// deterministic tick instead of the live attack subtick, which flips
		// between tick_base and tick_base - 1 for dt == 1 shots and voids the
		// correction half the time
		auto stamp_tick = tick_base;

		if ( !tgt.hit.source_eye.is_uninterpolated )
		{
			auto tick_add = [ ]( int t, float f, int tick_delta, float frac_delta )
				{
					f += frac_delta;
					const auto carry = static_cast< int >( std::floor( f ) );
					f -= static_cast< float >( carry );
					return std::pair{ t + tick_delta + carry, f };
				};

			stamp_tick = tick_add( tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac, tgt.hit.source_eye.lerp_ticks_int, tgt.hit.source_eye.lerp_ticks_frac ).first;
		}

		if ( config.no_spread.value )
		{
			// search the correction on the raw aim angle: the server re-applies
			// the aim punch on top of the angles we write, so the seed check must
			// match the view after the punch is added back — the punch is
			// therefore subtracted only when writing the history/base angles.
			// the inaccuracy and spread come from the weapon accuracy state
			// captured at create move (m_ctx), the same state the server rolls
			// the spread with on the fire tick
			const auto corrected = g_shared.find_spread_correction( aim_angle, stamp_tick );
			if ( corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f )
			{
				this->m_firing_this_tick = false;
				return;
			}

			aim_angle = corrected;
		}

		const auto command_angle = math::vector3{ aim_angle.x - aim_punch.x, aim_angle.y - aim_punch.y, 0.0f };
		const auto history_angle = math::vector3
		{
			aim_angle.x - aim_punch.x,
			aim_angle.y - aim_punch.y,
			config.no_spread.value ? aim_angle.z : 0.0f
		};

		g_shared.last_shoot_tick( ) = tick_base;

		features::misc::g_impacts.on_boom( tgt.hit.pawn, tgt.hit.hitgroup, tgt.hit.damage, tgt.hitchance, shared_ctx.inaccuracy, shared_ctx.spread, aim_angle, aim_punch, shoot_eye, tgt.hit.record->tick, stamp_tick, tick_base, memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) ), g_shared.lc( ).get_skeleton( *tgt.hit.record ) );
		const auto history_size = cmd->csgo_user_cmd.input_history_size( );
		for ( auto i = 0; i < history_size; ++i )
		{
			const auto entry = cmd->csgo_user_cmd.mutable_input_history( i );
			if ( !entry )
			{
				continue;
			}

			// the history carries the corrected roll and the player tick stamp
			// (seed tick); the command angles keep the roll zeroed
			shared::write_history_entry( entry, history_angle, config.no_spread.value, record_time.tick + 1, &tgt.hit.source_eye );
		}

		set_command_button( cmd, cstypes::command_buttons::in_attack, true );

		if ( history_size > 0 )
		{
			cmd->csgo_user_cmd.set_attack1_start_history_index( history_size - 1 );
		}

		if ( const auto angles = base->mutable_viewangles( ) )
		{
			angles->set_x( command_angle.x );
			angles->set_y( command_angle.y );

			if ( config.no_spread.value )
			{
				angles->set_z( command_angle.z );
			}
		}

		if ( !config.silent.value )
		{
			systems::g_input.set_view_angles( command_angle );
		}
		else if ( config.no_spread.value )
		{
			// the spread seed is derived from the live CSGOInput view at
			// serialize time, so even silent no-spread must push the corrected
			// angle into the live view — park the user's view here and restore it
			// at view setup, after the command has been serialized
			this->m_silent_restore_view = systems::g_input.get_view_angles( );
			this->m_silent_restore_required = true;
			systems::g_input.set_view_angles( command_angle );
		}
	}

	void rage::on_override_view( std::uintptr_t view_setup )
	{
		if ( !this->m_silent_restore_required )
		{
			return;
		}

		this->m_silent_restore_required = false;

		const auto local = systems::g_local.get( );
		if ( !local.is_alive )
		{
			return;
		}

		// undo the live-view write from fire_gun: restore the user's view in the
		// input (next command base) and in the view setup (camera angles)
		systems::g_input.set_view_angles( this->m_silent_restore_view );
		memory::write<math::vector3>( view_setup + 0x4b8, this->m_silent_restore_view );
	}

	bool rage::fire_melee( systems::input::usercmd* cmd, const target& tgt, const systems::local::snapshot& local )
	{
		if ( !tgt.hit.record || !tgt.hit.record->valid )
		{
			return false;
		}

		this->m_firing_this_tick = true;

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto tick_base = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		g_shared.last_shoot_tick( ) = tick_base;

		const auto record_time = cstypes::tick_fraction::from_value( tgt.hit.record->simulation_time / cstypes::tick_interval );
		const auto history_index = cmd->csgo_user_cmd.input_history_size( ) - 1;
		const auto entry = history_index >= 0 ? cmd->csgo_user_cmd.mutable_input_history( history_index ) : nullptr;

		if ( entry )
		{
			shared::write_history_entry( entry, tgt.hit.aim_angle, false, record_time.tick + 1, &tgt.hit.source_eye );
		}

		const auto is_secondary = tgt.hit.attack_type == 1;
		const auto attack_button = is_secondary
			? cstypes::command_buttons::in_second_attack
			: cstypes::command_buttons::in_attack;

		set_command_button( cmd, attack_button, true );

		if ( history_index >= 0 )
		{
			if ( is_secondary )
			{
				cmd->csgo_user_cmd.set_attack2_start_history_index( history_index );
			}
			else
			{
				cmd->csgo_user_cmd.set_attack1_start_history_index( history_index );
			}
		}

		if ( const auto angles = base->mutable_viewangles( ) )
		{
			angles->set_x( tgt.hit.aim_angle.x );
			angles->set_y( tgt.hit.aim_angle.y );
		}

		return true;
	}

	void rage::generate_multipoints( const systems::hitboxes::entry& hitbox, const math::vector3& center, const math::quaternion& bone_rot, float pointscale, const math::vector3& shoot_pos, float inaccuracy, std::vector<math::vector3>& out ) const
	{
		auto scale = std::clamp( pointscale / 100.0f, 0.0f, 1.0f );
		if ( scale <= 0.01f )
		{
			return;
		}

		const auto hb_mid   = ( hitbox.mins + hitbox.maxs ) * 0.5f;
		const auto capsule_a = center + bone_rot.rotate_vector( hitbox.mins - hb_mid );
		const auto capsule_b = center + bone_rot.rotate_vector( hitbox.maxs - hb_mid );

		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		if ( config.dynamic_pointscale.value && hitbox.radius > 0.001f )
		{
			const auto cone = std::max( inaccuracy + g_shared.ctx( ).spread, 0.0f );
			const auto cone_radius = std::tanf( cone ) * ( center - shoot_pos ).length( );
			const auto automatic_scale = std::clamp( 1.0f - cone_radius / ( hitbox.radius * 2.0f ), 0.45f, 1.0f );
			scale = std::min( scale, automatic_scale );
		}

		const auto shoot_dir = ( center - shoot_pos ).normalized( );
		const auto ang       = math::helpers::vector_to_angle( shoot_dir );

		math::vector3 left{}, up{};
		math::helpers::angle_vectors_left( ang, nullptr, &left, &up );

		const auto right = math::vector3{ -left.x, -left.y, -left.z };

		const auto surface_point = [ & ]( const math::vector3& direction ) -> math::vector3
		{
			const auto dir = direction.normalized( );

			if ( hitbox.radius > 0.001f )
			{
				const auto reach = ( capsule_b - capsule_a ).length( ) + hitbox.radius * 2.0f + 1.0f;
				const auto origin = center + dir * reach;
				const auto delta = dir * ( reach * -2.0f );
				auto fraction{ 1.0f };

				if ( g_shared.ray_vs_capsule( origin, delta, capsule_a, capsule_b, hitbox.radius, fraction ) )
				{
					return origin + delta * fraction;
				}
			}
			else
			{
				auto inverse = bone_rot;
				inverse.x = -inverse.x;
				inverse.y = -inverse.y;
				inverse.z = -inverse.z;
				const auto local_dir = inverse.rotate_vector( dir );
				const auto extents = ( hitbox.maxs - hitbox.mins ) * 0.5f;
				auto distance = 8192.0f;

				if ( std::fabs( local_dir.x ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.x / local_dir.x ) );
				if ( std::fabs( local_dir.y ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.y / local_dir.y ) );
				if ( std::fabs( local_dir.z ) > 1.0e-6f ) distance = std::min( distance, std::fabs( extents.z / local_dir.z ) );

				if ( distance < 8192.0f )
				{
					return center + dir * distance;
				}
			}

			return center;
		};

		const auto scaled_surface = [ & ]( const math::vector3& direction )
		{
			const auto surface = surface_point( direction );
			return center + ( surface - center ) * scale;
		};

		switch ( hitbox.index )
		{
		case 0: 
		{
			out.reserve( 4 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			out.push_back( scaled_surface( up ) );
			out.push_back( scaled_surface( -up ) );
			break;
		}

		case 2: case 3: 
		{
			out.reserve( 2 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			break;
		}

		case 4: case 5: case 6: 
		{
			out.reserve( 3 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			if ( hitbox.index == 6 )
			{
				out.push_back( scaled_surface( up ) );
			}
			break;
		}

		case 7: case 8: case 9: case 10: case 11: case 12: 
		{
			out.reserve( 2 );
			out.push_back( capsule_a );
			out.push_back( capsule_b );
			break;
		}

		case 13: case 14: case 15: case 16: case 17: case 18: 
		{
			out.reserve( 1 );
			out.push_back( capsule_b );
			break;
		}

		default:
		{
			out.reserve( 2 );
			out.push_back( scaled_surface( right ) );
			out.push_back( scaled_surface( -right ) );
			break;
		}
		}
	}

	float rage::get_min_damage( const settings::combat::ragebot::weapon_group& config, int target_health, bool override_active ) const
	{
		const auto value = override_active ? config.min_damage_override_value : config.min_damage;

		if ( value == settings::combat::ragebot::k_lethal_min_damage )
		{
			return static_cast< float >( target_health );
		}

		const auto hp = static_cast< float >( target_health );

		if ( value > 100 )
		{
			const auto overkill = static_cast< float >( value - 100 );
			return hp + overkill;
		}

		const auto base = static_cast< float >( value );
		return std::min( base, hp );
	}

	float rage::get_knife_damage( float raw, int armor, float armor_ratio ) const
	{
		if ( armor <= 0 )
		{
			return raw;
		}

		const auto ratio = armor_ratio * 0.5f;
		auto damage_to_health = raw * ratio;
		const auto damage_to_armor = ( raw - damage_to_health ) * 0.5f;

		if ( damage_to_armor > static_cast< float >( armor ) )
		{
			damage_to_health = raw - static_cast< float >( armor ) * 2.0f;
		}

		return std::max( 0.0f, std::floorf( damage_to_health ) );
	}

	systems::tracing::result rage::trace_taser_hit( const math::vector3& origin, const math::vector3& forward, float range, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const
	{
		const auto end = origin + forward * range;
		const int filter_extras[ ]{ 0, 15 };

		for ( const auto extra : filter_extras )
		{
			const auto filter = extra == 0 ? systems::g_tracing.make_filter( local_pawn, 0x001c1003, 4 ) : systems::g_tracing.make_filter( local_pawn, 0x001c1003, 4, 15 );
			auto result = systems::g_tracing.trace( origin, end, filter );

			if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
			{
				return result;
			}

			for ( auto radius = 2.0f; radius <= 4.0f; radius += 2.0f )
			{
				const auto sweep_end = end - forward * radius;
				result = systems::g_tracing.trace_sphere( origin, sweep_end, radius, filter );

				if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
				{
					return result;
				}
			}
		}

		systems::tracing::result miss{};
		miss.fraction = 1.0f;
		miss.hit_entity = 0;
		return miss;
	}

	systems::tracing::result rage::trace_knife_hit( const math::vector3& origin, const math::vector3& forward, float reach, std::uintptr_t target_pawn, std::uintptr_t local_pawn ) const
	{
		const auto end = origin + forward * reach;
		const auto knife_filter = systems::g_tracing.make_filter( local_pawn, 0x0c3001, 4 );
		auto result = systems::g_tracing.trace( origin, end, knife_filter );

		if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
		{
			return result;
		}

		const auto weapon_filter = systems::g_tracing.make_filter( local_pawn, 0x0c3001, 4, 15 );
		result = systems::g_tracing.trace( origin, end, weapon_filter );

		if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
		{
			return result;
		}

		for ( auto radius = 14.0f; radius > 0.0f; radius -= 3.0f )
		{
			const auto sweep_end = end - forward * radius;
			result = systems::g_tracing.trace_sphere( origin, sweep_end, radius, weapon_filter );

			if ( ( result.fraction < 1.0f || result.all_solid ) && result.hit_entity == target_pawn )
			{
				return result;
			}
		}

		result.fraction = 1.0f;
		result.hit_entity = 0;
		return result;
	}

	// auto lineup - finds positions for wallbangs
		// optimized: cached positions per map, incremental checks per frame
		void rage::init_auto_lineup( const systems::local::snapshot& local )
		{
			if ( !local.is_alive || !local.pawn )
			{
				this->m_auto_lineup.initialized = false;
				return;
			}

			const auto game_rules = memory::read<std::uintptr_t>( addresses::globals::game_rules );
			if ( !game_rules )
			{
				this->m_auto_lineup.initialized = false;
				return;
			}

			const auto map_name_ptr = memory::read<std::uintptr_t>( game_rules + SCHEMA( "C_CSGameRules", "m_sMapName"_hash ) );
			if ( !map_name_ptr )
			{
				this->m_auto_lineup.initialized = false;
				return;
			}

			const auto map_name = memory::read_string( map_name_ptr, 128 );
			if ( this->m_current_map_name != map_name )
			{
				this->m_current_map_name = map_name;
				this->m_auto_lineup.positions.clear( );
				this->m_auto_lineup.current_check = 0;
				this->m_auto_lineup.initialized = true;
			}
		}

		std::optional<rage::lineup_position> rage::find_auto_lineup( const aim_context& ctx, const std::vector<candidate>& candidates, const systems::local::snapshot& local )
		{
			const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
			if ( !config.auto_lineup.value || candidates.empty( ) )
			{
				return std::nullopt;
			}

			this->init_auto_lineup( local );

			if ( !this->m_auto_lineup.initialized )
			{
				return std::nullopt;
			}

			if ( this->m_auto_lineup.positions.empty( ) )
			{
				this->m_auto_lineup.current_check = 0;
				this->m_auto_lineup.checks_this_frame = 0;
			}

			constexpr auto checks_per_frame{ 4 };
			const auto max_checks = checks_per_frame;

			while ( this->m_auto_lineup.current_check < this->m_auto_lineup.positions.size( ) && this->m_auto_lineup.checks_this_frame < max_checks )
			{
				const auto& pos = this->m_auto_lineup.positions[ this->m_auto_lineup.current_check ];
				++this->m_auto_lineup.current_check;
				++this->m_auto_lineup.checks_this_frame;

				for ( const auto& cand : candidates )
				{
					if ( ( cand.pawn - pos.target_pawn ) == 0 && ( pos.target_pos - cand.records[ 0 ]->origin ).length_sqr( ) < 100.0f )
					{
						return pos;
					}
				}
			}

			if ( this->m_auto_lineup.current_check >= this->m_auto_lineup.positions.size( ) )
			{
				this->m_auto_lineup.current_check = 0;
				this->m_auto_lineup.checks_this_frame = 0;
			}

			return std::nullopt;
		}

		bool rage::test_lineup_position( const math::vector3& pos, const aim_context& ctx, const candidate& cand, const systems::local::snapshot& local, lineup_position& out ) const
		{
			const auto& shared_ctx = g_shared.ctx( );
			const auto& config = settings::g_combat.m_ragebot.get_group( shared_ctx.weapon_type );

			const auto eye = g_shared.get_eye_position( local.pawn );
			const auto local_origin = systems::g_prediction.pre( ).origin;

			const auto to_pos = pos - local_origin;
			const auto dist_sq = to_pos.length_sqr( );

			constexpr auto max_dist_sq{ 3000.0f * 3000.0f };
			if ( dist_sq > max_dist_sq )
			{
				return false;
			}

			const auto trace = systems::g_tracing.trace_hull( local_origin, pos, { -16, -16, 0 }, { 16, 16, 72 }, local.pawn, 0x1c3003, 4 );
			if ( trace.fraction < 1.0f )
			{
				return false;
			}

			for ( auto ri = 0; ri < cand.record_count; ++ri )
			{
				auto* record = cand.records[ ri ];
				if ( !record || !record->valid )
				{
					continue;
				}

				const auto eye_candidates = this->get_eye_candidates( );
				for ( auto ei = 0; ei < eye_candidates.count; ++ei )
				{
					const auto shoot_eye = eye_candidates.entries[ ei ].position;

					auto hits = this->scan_record( this->prepare_player( const_cast< candidate& >( cand ) ), shoot_eye, ctx.predicted_inaccuracy, ctx, const_cast< candidate& >( cand ), record, local );

					for ( const auto& hit : hits )
					{
						constexpr auto lineup_min_damage{ 15.0f };

						if ( hit.damage >= config.min_damage.value && !hit.penetrated )
						{
							continue;
						}

						if ( hit.damage >= lineup_min_damage )
						{
							out = lineup_position
							{
								.position = pos,
								.target_pos = record->origin,
								.aim_angle = hit.aim_angle,
								.damage = hit.damage,
								.hitgroup = hit.hitgroup,
								.target_pawn = cand.pawn,
								.valid = true
							};
							return true;
						}
					}
				}
			}

			return false;
		}

		math::vector3 rage::get_lineup_origin( const candidate& cand, const systems::local::snapshot& local ) const
		{
			const auto local_origin = systems::g_prediction.pre( ).origin;
			const auto target_origin = cand.records[ 0 ]->origin;

			const auto dir = ( target_origin - local_origin ).normalized( );
			const auto perp = math::vector3{ -dir.y, dir.x, 0.0f };

			return local_origin + perp * 50.0f;
		}

		void rage::update_penetration_crosshair( const systems::local::snapshot& local )
	{
		const auto& cfg = settings::g_combat.m_penetration_crosshair;
		const auto& ctx = g_shared.ctx( );

		if ( !cfg.enabled.value || !ctx.valid || !local.is_alive || local.team < 2
			|| !local.pawn || !ctx.weapon
			|| ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
		{
			this->m_penetration_crosshair_state.store( penetration_crosshair_state::unavailable, std::memory_order_relaxed );
			return;
		}

		const auto eye_pos = g_shared.get_eye_position( local.pawn );
		auto view_angles = systems::g_input.get_view_angles( );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );
		view_angles.x += aim_punch.x;
		view_angles.y += aim_punch.y;

		math::vector3 forward{};
		math::helpers::angle_vectors_left( view_angles, &forward );

		auto pen_damage{ 0.0f };
		const auto can_pen = g_shared.pen( ).can( eye_pos, forward, pen_damage, local );
		this->m_penetration_crosshair_state.store(
			can_pen ? penetration_crosshair_state::penetrable : penetration_crosshair_state::blocked,
			std::memory_order_relaxed );
	}

	void rage::draw_penetration_crosshair( xdraw::draw_list& draw_list ) const
	{
		const auto& cfg = settings::g_combat.m_penetration_crosshair;
		if ( !cfg.enabled.value )
		{
			return;
		}

		const auto state = this->m_penetration_crosshair_state.load( std::memory_order_relaxed );
		const auto local = systems::g_local.get( );
		if ( state == penetration_crosshair_state::unavailable || !local.is_alive || systems::g_local.is_in_cinematic( ) )
		{
			return;
		}

		const auto can_pen = state == penetration_crosshair_state::penetrable;

		const auto& fill = can_pen ? cfg.can_penetrate_fill : cfg.blocked_fill;
		const auto& outline = can_pen ? cfg.can_penetrate_outline : cfg.blocked_outline;
		const auto [ screen_w, screen_h ] = xdraw::viewport_size( );
		const auto cx = std::floorf( static_cast< float >( screen_w ) * 0.5f );
		const auto cy = std::floorf( static_cast< float >( screen_h ) * 0.5f );
		constexpr auto half_size{ 3.0f };
		constexpr auto outline_size{ 1.0f };

		if ( cfg.glow )
		{
			auto& glow = xdraw::get_glow( );
			const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( outline.value.a ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ outline.value.r, outline.value.g, outline.value.b, glow_a };

			glow.rect_filled( cx - half_size - outline_size, cy - half_size - outline_size,
				( half_size + outline_size ) * 2.0f, ( half_size + outline_size ) * 2.0f, glow_col );
		}

		draw_list.rect_filled( cx - half_size - outline_size, cy - half_size - outline_size,
			( half_size + outline_size ) * 2.0f, ( half_size + outline_size ) * 2.0f, outline );
		draw_list.rect_filled( cx - half_size, cy - half_size, half_size * 2.0f, half_size * 2.0f, fill );
	}

} 
