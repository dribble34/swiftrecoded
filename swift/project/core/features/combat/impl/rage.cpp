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

	// angle_distance without the final sqrt; threshold compares fine squared
	float angle_distance_sqr( const math::vector3& from, const math::vector3& to )
	{
		const auto pitch = to.x - from.x;
		const auto yaw = math::helpers::normalize_yaw( to.y - from.y );
		return pitch * pitch + yaw * yaw;
	}

	// hitbox index -> body part, matched to systems::hitboxes::hitgroup_from_hitbox.
	// 7-12 are legs, 13-18 are arms here (reverse of the classic CS:GO layout),
	// so check that table before "fixing" these. named hitbox_id to avoid
	// shadowing by the hitbox_index locals elsewhere in this file.
	namespace hitbox_id {

		inline constexpr auto head{ 0 };

		inline constexpr std::array chest{ 4, 5, 6 };
		inline constexpr std::array stomach{ 3, 2 };
		inline constexpr std::array arms{ 13, 14, 15, 16, 17, 18 };
		inline constexpr std::array legs{ 7, 8, 9, 10 };
		inline constexpr std::array feet{ 11, 12 };

		// highest index in hitgroup_from_hitbox's table, plus one
		inline constexpr auto count{ 19 };

	}

	// indices into the ragebot's `hitboxes` checkboxes; order matches
	// detail::hitbox_names in menu.ragebot.cpp
	namespace hitbox_toggle {

		inline constexpr auto head{ 0 };
		inline constexpr auto chest{ 1 };
		inline constexpr auto stomach{ 2 };
		inline constexpr auto arms{ 3 };
		inline constexpr auto legs{ 4 };
		inline constexpr auto feet{ 5 };

	}

	// bone count lagcomp::get_skeleton returns; every emitted bone index
	// subscripts that array, so keep this as the single bound
	inline constexpr auto k_skeleton_bones{ 27 };

	// tie-breaker weight per hitbox, indexed by hitbox index. legs rank below
	// arms on purpose - it's a tuning choice, not forced by the damage model.
	inline constexpr std::array<float, hitbox_id::count> k_hitbox_priority
	{
		4.0f,                                       // head
		3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f,         // neck, stomach, chest
		1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,         // legs
		2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f          // arms
	};

	float hitbox_priority( int hitbox_index )
	{
		if ( hitbox_index < 0 || hitbox_index >= static_cast< int >( k_hitbox_priority.size( ) ) )
		{
			return 0.0f;
		}

		return k_hitbox_priority[ hitbox_index ];
	}

	// target-scoring weights. tier 1: any lethal hit outranks any non-lethal
	// one (the lethal bonus alone beats the largest non-lethal score). tier 2:
	// everything else, ordered by expected damage. within a tier: confidence,
	// then hitbox, then tie-breakers.
	namespace score_weight {

		inline constexpr auto lethal{ 100000.0f };

		// ~17 points of hitchance is worth one hitbox priority tier
		inline constexpr auto lethal_hitchance{ 15000.0f };
		inline constexpr auto lethal_priority{ 2500.0f };

		// hitchance squared to discount marginal shots hard; flat term keeps
		// damage meaningful when hitchance is near zero
		inline constexpr auto damage_confidence{ 150.0f };
		inline constexpr auto damage_flat{ 5.0f };

		// prefer a clean line over a wallbang; sized to sit between the tiers
		inline constexpr auto direct_hit{ 5000.0f };

		// tie-breakers
		inline constexpr auto center_point{ 50.0f };
		inline constexpr auto priority{ 25.0f };
		inline constexpr auto fov_penalty{ 0.1f };

	}

	// world-space centre of a hitbox on a posed skeleton
	math::vector3 hitbox_center( const systems::hitboxes::entry& hitbox, const systems::bones::data& bone )
	{
		return bone.rotation.rotate_vector( ( hitbox.mins + hitbox.maxs ) * 0.5f ) + bone.position;
	}

	// usable only if the bone exists in both the skeleton array and this record's pose
	bool bone_usable( const systems::hitboxes::entry& hitbox, const features::combat::shared::lagcomp::record& record )
	{
		return hitbox.bone >= 0
			&& hitbox.bone < k_skeleton_bones
			&& hitbox.bone < record.bone_count;
	}

	// squared fov gate. squaring loses the sign, so guard against a negative
	// max_fov (a hand-edited config skips the slider's [1,180] clamp) which
	// would otherwise read as its absolute value and re-enable a disabled bot.
	float fov_gate_sqr( float max_fov )
	{
		return max_fov > 0.0f ? max_fov * max_fov : -1.0f;
	}

	bool aim_within_fov(
		const math::vector3& eye,
		const math::vector3& position,
		const math::vector3& view_angles,
		float max_fov_sq,
		math::vector3& out_angle,
		float& out_fov )
	{
		out_angle = math::helpers::calculate_angle( eye, position );

		const auto fov_sq = angle_distance_sqr( view_angles, out_angle );
		if ( fov_sq > max_fov_sq )
		{
			return false;
		}

		out_fov = std::sqrtf( fov_sq );
		return true;
	}

	struct enemy_ref
	{
		std::uintptr_t pawn{};
		std::uintptr_t game_scene_node{};
		int health{};
	};

	// single definition of "an enemy the ragebot may act on", shared by target
	// gathering and autostop. the callback returns false to stop the walk so a
	// caller that's done doesn't keep paying the per-player filter cost.
	template <typename callback_t>
	void for_each_enemy( const systems::local::snapshot& local, callback_t&& callback )
	{
		for ( const auto& p : systems::g_entities.get_by_type( systems::entities::type::player ) )
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

			if ( !pawn || pawn == local.pawn )
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

			// no scene node means no bones to resolve, so nothing can scan it
			const auto game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( !game_scene_node )
			{
				continue;
			}

			if constexpr ( std::is_same_v<decltype( callback( enemy_ref{} ) ), bool> )
			{
				if ( !callback( enemy_ref{ .pawn = pawn, .game_scene_node = game_scene_node, .health = health } ) )
				{
					return;
				}
			}
			else
			{
				callback( enemy_ref{ .pawn = pawn, .game_scene_node = game_scene_node, .health = health } );
			}
		}
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

		// hold the previous brake decision across ticks where can_shoot() bails
		// before run_gun() runs (most ticks on cooldown), otherwise the bot
		// never slows enough between shots to land follow-ups
		const auto was_stopping = this->m_should_stop;

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
				// between shots: hold the brake, re-evaluate next fireable tick
				this->m_should_stop = was_stopping;
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

		// fallback if prediction is unavailable; zero would read as perfect accuracy
		out.predicted_inaccuracy = g_shared.get_inaccuracy( true );

		systems::g_prediction.simulate( cmd, local, [ & ]
			{
				g_shared.sh( ).snapshot( local.pawn, ctx.weapon_services );

				out.velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
				out.spread = g_shared.get_spread( );

				// sample inside simulate (post-move, pre-restore) so velocity/
				// flags/duck are what the shot actually fires with
				out.predicted_inaccuracy = g_shared.get_inaccuracy( true );
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

		const auto eye = g_shared.get_eye_position( local.pawn );
		const auto view_angles = systems::g_input.get_view_angles( );
		const auto max_fov_sq = ( max_fov + 15.0f ) * ( max_fov + 15.0f );

		// candidates hold raw pointers into m_extrapolated_records, so a realloc
		// would dangle them - this reserve is load-bearing, not an optimisation
		constexpr std::size_t k_max_extrapolated{ 64 };

		std::vector<candidate> out;
		out.reserve( k_max_extrapolated );

		this->m_extrapolated_records.clear( );
		this->m_extrapolated_records.reserve( k_max_extrapolated );

		// guard against the real capacity, not k_max_extrapolated, so shrinking
		// the reserve above can't quietly bring the dangling-pointer bug back
		const auto extrapolated_capacity = this->m_extrapolated_records.capacity( );

		for_each_enemy( local, [ & ]( const enemy_ref& enemy )
			{
				if ( max_fov < 180.0f )
				{
					const auto origin = memory::read<math::vector3>( enemy.game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
					const auto aim = math::helpers::calculate_angle( eye, origin );

					if ( angle_distance_sqr( view_angles, aim ) > max_fov_sq )
					{
						return;
					}
				}

				auto records = g_shared.lc( ).get_valid_records( enemy.pawn );

				if ( records.empty( ) )
				{
					if ( this->m_extrapolated_records.size( ) >= extrapolated_capacity )
					{
						return;
					}

					// no usable history: try extrapolation, then a live snapshot,
					// before dropping the candidate
					auto extrap = g_shared.lc( ).extrapolate( enemy.pawn );

					if ( extrap.has_value( ) )
					{
						this->m_extrapolated_records.push_back( std::move( *extrap ) );
					}
					else
					{
						// built in place: a record carries two 128-bone arrays,
						// too big to pass around on the stack per player
						this->m_extrapolated_records.emplace_back( );

						if ( !this->m_extrapolated_records.back( ).setup( enemy.pawn ) )
						{
							this->m_extrapolated_records.pop_back( );
							return;
						}
					}

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
						return;
					}
				}

				candidate c{};
				c.pawn = enemy.pawn;
				c.health = enemy.health;
				c.armor = memory::read<int>( enemy.pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );
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
					c.min_damage = this->get_min_damage( config, enemy.health, config.min_damage_override.value );
				}

				out.push_back( c );
			} );

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

			// autostop must not depend on lag-comp history, so walk the enemy
			// list directly instead of gather_candidates
			if ( autostop_enabled && this->should_stop_movement( ctx ) )
			{
				const auto eye = g_shared.get_eye_position( local.pawn );
				const auto view_angles = systems::g_input.get_view_angles( );
				const auto max_fov_sq = ( config.max_fov.value + 15.0f ) * ( config.max_fov.value + 15.0f );

				for_each_enemy( local, [ & ]( const enemy_ref& enemy )
					{
						const auto origin = memory::read<math::vector3>( enemy.game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
						const auto center = origin + math::vector3{ 0.0f, 0.0f, 45.0f };

						if ( angle_distance_sqr( view_angles, math::helpers::calculate_angle( eye, center ) ) > max_fov_sq )
						{
							return true;
						}

						// only stop for someone actually shootable from here
						// (max_fov is often the full 180 degrees)
						if ( !systems::g_tracing.is_visible( eye, center, enemy.pawn, local.pawn ) )
						{
							auto pen_damage{ 0.0f };
							const auto direction = ( center - eye ).normalized( );

							if ( !g_shared.pen( ).can( eye, direction, pen_damage, local ) )
							{
								return true;
							}

							// pen().can only says whether a bullet survives the
							// wall - no hitbox, no armour/hitgroup scaling - so
							// its damage is pre-scaling and optimistic. gate it on
							// the configured minimum anyway so autostop doesn't
							// brake for walls it can barely scratch.
							if ( pen_damage < this->get_min_damage( config, enemy.health, config.min_damage_override.value ) )
							{
								return true;
							}
						}

						this->m_should_stop = true;
						return false;
					} );
			}

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

			// duck-peek applies to no-spread too: stand up before the shot, duck again after
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

				// both samples share the same weapon state, so the recoil
				// penalty cancels and what's left is the movement share of the
				// inaccuracy - which is what this backstop gates. don't compare
				// total inaccuracy here: it includes m_fAccuracyPenalty and would
				// veto the whole recoil-recovery window, which is hitchance's job.
				const auto stand_inacc = g_shared.get_inaccuracy_at_velocity( local.pawn, {} );
				const auto movement_inacc = std::max( real_inacc - stand_inacc, 0.0f );

				// coarse backstop only; hitchance already accounts for speed.
				// raise for more aggression, lower for less.
				const auto too_inaccurate = movement_inacc > inacc_floor * 5.0f + 0.010f;
				const auto too_fast = ctx.weapon_max_speed > 0.0f && real_speed_2d > ctx.weapon_max_speed * 0.60f;

				// airborne shots are only reliable near the jump apex. the
				// reconstructed get_air_inaccuracy under-reads the engine value,
				// so measure real airborne inaccuracy from the full velocity; a
				// small tolerance keeps the bot off a single-tick window.
				const auto airborne_unshootable = ( prestate.flags & cstypes::entity_flags::on_ground ) == 0
					&& [ & ]( ) -> bool
					{
						const auto inac_jump_apex = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );
						const auto accuracy_penalty = memory::read<float>( g_shared.ctx( ).weapon + SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_hash ) );
						const auto min_air_inaccuracy = accuracy_penalty + inac_jump_apex;
						const auto real_air_inaccuracy = g_shared.get_inaccuracy_at_velocity( local.pawn, full_vel );

						// 0.003 pinned airborne shots to one tick of the arc
						constexpr auto air_tolerance{ 0.010f };
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
			// only stop if the predicted stop position actually yields a hit.
			// a scoped sniper satisfies should_stop_movement at almost any
			// speed, so without this it stops whenever scoped and moving.
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

		// duck-peek: stay stood up while an accurate shot is available, re-duck
		// after firing. the flag latches so hitchance wobble during the stand-up
		// animation doesn't interrupt the peek.
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

						const auto has_direct_hit = this->scan_record( state, eye, inaccuracy, ctx, cand, cand.records[ ri ], local, candidate_hits );

						// freshest record is both more reliable and cheaper
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

		const auto add = [ &state ]( int hitbox )
			{
				if ( state.scan_count < static_cast< int >( state.scan_order.size( ) ) )
				{
					state.scan_order[ state.scan_count++ ] = hitbox;
				}
			};

		const auto add_group = [ & ]( std::size_t toggle, const auto& group )
			{
				if ( state.config->hitboxes.values[ toggle ] )
				{
					for ( const auto hitbox : group )
					{
						add( hitbox );
					}
				}
			};

		// scan in priority order: head, torso, limbs, so downstream early-outs
		// land on the highest-value boxes first
		if ( !state.force_body && state.config->hitboxes.values[ hitbox_toggle::head ] )
		{
			add( hitbox_id::head );
		}

		add_group( hitbox_toggle::chest, hitbox_id::chest );
		add_group( hitbox_toggle::stomach, hitbox_id::stomach );
		add_group( hitbox_toggle::arms, hitbox_id::arms );
		add_group( hitbox_toggle::legs, hitbox_id::legs );
		add_group( hitbox_toggle::feet, hitbox_id::feet );

		// nothing enabled: fall back to head + torso so the bot still fires
		if ( state.scan_count == 0 )
		{
			if ( !state.force_body )
			{
				add( hitbox_id::head );
			}

			for ( const auto hitbox : hitbox_id::chest )
			{
				add( hitbox );
			}

			for ( const auto hitbox : hitbox_id::stomach )
			{
				add( hitbox );
			}
		}

		state.pen_static = g_shared.pen( ).prepare_target_static( cand.pawn );

		return state;
	}

	bool rage::scan_record( const player_scan_state& state, const math::vector3& eye, float inaccuracy, const aim_context& ctx, candidate& cand, shared::lagcomp::record* record, const systems::local::snapshot& local, std::vector<scan_hit>& out ) const
	{
		const auto& config = *state.config;
		const auto skeleton = g_shared.lc( ).get_skeleton( *record );
		const auto pen_ctx = g_shared.pen( ).prepare_target( cand.pawn, record, state.pen_static );

		const auto max_fov_sq = fov_gate_sqr( config.max_fov );

		// reused across hitboxes to avoid an allocation per multipoint batch
		std::vector<math::vector3> multipoints;

		auto found_direct{ false };

		// test one candidate point, append to `out` if viable
		const auto test_point = [ & ](
			const math::vector3& position,
			const systems::hitboxes::entry& hitbox,
			int hitbox_index,
			bool is_center )
			{
				math::vector3 aim{};
				auto fov{ 0.0f };

				if ( !aim_within_fov( eye, position, ctx.view_angles, max_fov_sq, aim, fov ) )
				{
					return;
				}

				shared::penetration::result pen{};
				if ( !g_shared.pen( ).run( eye, position, pen_ctx, local.pawn, local.team, pen ) )
				{
					return;
				}

				if ( pen.damage < cand.min_damage )
				{
					return;
				}

				// record whatever the engine says was hit, but keep aiming where
				// we traced - retargeting to another hitbox's centre fires an
				// unvalidated ray
				auto resolved_hitbox = hitbox;
				auto resolved_hitbox_index = hitbox_index;
				auto resolved_bone_index = hitbox.bone;

				if ( pen.hitbox != hitbox_index )
				{
					if ( pen.hitbox < 0 || pen.hitbox >= static_cast< int >( state.hitbox_by_index.size( ) ) || !state.hitbox_by_index[ pen.hitbox ].valid )
					{
						return;
					}

					const auto& actual_hitbox = state.hitbox_by_index[ pen.hitbox ].entry;
					if ( !bone_usable( actual_hitbox, *record ) )
					{
						return;
					}

					resolved_hitbox = actual_hitbox;
					resolved_hitbox_index = actual_hitbox.index;
					resolved_bone_index = actual_hitbox.bone;
				}

				scan_hit h{};
				h.position = position;
				h.aim_angle = aim;
				h.damage = pen.damage;
				h.fov = fov;
				h.hitbox_index = resolved_hitbox_index;
				h.hitgroup = pen.hitgroup;
				h.bone_index = resolved_bone_index;
				h.hitbox = resolved_hitbox;
				h.is_center = is_center && pen.hitbox == hitbox_index;
				h.penetrated = pen.penetrated;
				h.pawn = cand.pawn;
				h.health = cand.health;
				h.record = record;

				found_direct = found_direct || !h.penetrated;

				out.push_back( h );
			};

		for ( auto idx = 0; idx < state.scan_count; ++idx )
		{
			const auto hitbox_index = state.scan_order[ idx ];
			if ( hitbox_index >= static_cast< int >( state.hitbox_by_index.size( ) ) || !state.hitbox_by_index[ hitbox_index ].valid )
			{
				continue;
			}

			const auto& hb = state.hitbox_by_index[ hitbox_index ].entry;
			if ( !bone_usable( hb, *record ) )
			{
				continue;
			}

			const auto& bone = skeleton[ hb.bone ];
			const auto center = hitbox_center( hb, bone );

			test_point( center, hb, hitbox_index, true );

			if ( config.pointscale > 0.0f )
			{
				multipoints.clear( );
				this->generate_multipoints( hb, center, bone.rotation, config.pointscale, eye, inaccuracy, multipoints );

				for ( const auto& mp : multipoints )
				{
					test_point( mp, hb, hitbox_index, false );
				}
			}
		}

		return found_direct;
	}

	rage::target rage::select_best( const aim_context& aim_ctx, const std::vector<scan_hit>& hits, float eval_inaccuracy ) const
	{
		const auto& config = settings::g_combat.m_ragebot.get_group( g_shared.ctx( ).weapon_type );
		const auto needed_hc = config.hitchance_override.value
			? static_cast< float >( config.hitchance_override_value ) / 100.0f
			: static_cast< float >( config.hitchance ) / 100.0f;

		// hitchance-independent part of the score, shared by score_for and
		// min_hitchance_to_beat so they can't drift apart
		const auto score_constant = [ ]( const scan_hit& h )
		{
			auto out{ 0.0f };

			if ( !h.penetrated )
			{
				out += score_weight::direct_hit;
			}

			if ( h.is_center )
			{
				out += score_weight::center_point;
			}

			out += hitbox_priority( h.hitbox_index ) * score_weight::priority;
			out -= h.fov * score_weight::fov_penalty;

			return out;
		};

		const auto score_for = [ &score_constant ]( const scan_hit& h, float hc )
		{
			const auto priority = hitbox_priority( h.hitbox_index );
			const auto is_lethal = h.damage >= static_cast< float >( h.health );

			// only relative ordering matters, so no base offset - a large one
			// costs float precision and quantises the fov tie-breaker away
			auto score = score_constant( h );

			if ( is_lethal )
			{
				score += score_weight::lethal
					+ hc * score_weight::lethal_hitchance
					+ priority * score_weight::lethal_priority;
			}
			else
			{
				score += h.damage * hc * hc * score_weight::damage_confidence
					+ h.damage * score_weight::damage_flat;
			}

			return score;
		};

		// smallest hitchance at which this hit could score above `target`.
		// score_for increases monotonically in hc, so it inverts cleanly;
		// a result above 1 means the hit can never get there.
		const auto min_hitchance_to_beat = [ &score_constant ]( const scan_hit& h, float target )
		{
			const auto priority = hitbox_priority( h.hitbox_index );
			const auto is_lethal = h.damage >= static_cast< float >( h.health );
			const auto constant = score_constant( h );

			if ( is_lethal )
			{
				const auto needed = target - score_weight::lethal - priority * score_weight::lethal_priority - constant;
				return needed / score_weight::lethal_hitchance;
			}

			const auto scale = h.damage * score_weight::damage_confidence;
			if ( scale <= 0.0f )
			{
				// no hitchance can lift a zero-damage hit above anything
				return 2.0f;
			}

			const auto needed = target - h.damage * score_weight::damage_flat - constant;
			if ( needed <= 0.0f )
			{
				return 0.0f;
			}

			return std::sqrt( needed / scale );
		};

		// best this hit could score; lets us skip hitchance sampling for
		// candidates that can't win even at perfect confidence
		const auto upper_bound_score = [ & ]( const scan_hit& h ) { return score_for( h, 1.0f ); };

		target best{};
		target best_any{};

		for ( const auto& h : hits )
		{
			if ( !h.record || !h.record->valid )
			{
				continue;
			}

			if ( h.bone_index >= k_skeleton_bones || h.bone_index >= h.record->bone_count )
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
				// this hit only matters if it can become `best` (needs needed_hc
				// and a score above best.score) or `best_any` (score above
				// best_any.score). below the lower bar the sampler can stop
				// early. best is not gated on best_any: while best is invalid,
				// any hit clearing needed_hc becomes best regardless of score.
				const auto bar_for_best = best.valid
					? std::max( needed_hc, min_hitchance_to_beat( h, best.score ) )
					: needed_hc;

				const auto bar_for_any = best_any.valid
					? min_hitchance_to_beat( h, best_any.score )
					: 0.0f;

				const auto abort_below = std::clamp( std::min( bar_for_best, bar_for_any ), 0.0f, 1.0f );

				hc = g_shared.calculate_hitchance( h.source_eye.position, h.aim_angle, h.hitbox, bone, eval_inaccuracy, aim_ctx.spread, 256, abort_below );
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

		// a candidate is a matched pair: position and player_tick/player_frac
		// both come from the same weapon-services ring sample, and the server
		// reconstructs the bullet origin from that stamp. never substitute a
		// position without fixing up (or suppressing) its stamp - the two come
		// from different clocks and the mismatch shows up as aim and wall
		// misses at range. use the entries unmodified, like the known-good tree.
		if ( eye_candidates.count > 0 )
		{
			return eye_candidates;
		}

		// no usable ring sample. the fallback position has no matching stamp, so
		// mark it uninterpolated - write_history_entry then keeps the engine's
		// own player_tick_count instead of writing a mismatched one.
		const auto local_pawn = systems::g_local.get( ).pawn;
		const auto fallback_eye = local_pawn
			? g_shared.get_interpolated_shoot_position( local_pawn )
			: math::vector3{};

		if ( fallback_eye.length_sqr( ) > 1.0f )
		{
			eye_candidates.entries[ 0 ].position = fallback_eye;
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

		// accumulate displacement per tick over the window that ends when the
		// shot becomes accurate, and sample inaccuracy from the velocity at
		// that moment - not from a fixed 15-tick decay to a standstill

		// scoped snipers are the exception: shared::is_max_accuracy only clears
		// them at a standstill, everything else at the weapon's threshold
		const auto scoped_sniper = shared_ctx.weapon_type == cstypes::weapon_type::sniper && shared_ctx.is_scoped;
		const auto target_speed = scoped_sniper ? 0.1f : ctx.accurate_threshold;

		math::vector3 displacement{};

		for ( auto i = 0; i < 15; ++i )
		{
			const auto sim_speed = sim_vel.length_2d( );
			if ( sim_speed <= target_speed || sim_speed < 1.0f )
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
			}

			// the engine integrates position with the post-friction velocity,
			// so the step is accumulated after the update, not before
			displacement += sim_vel * cstypes::tick_interval;

			if ( new_speed <= 0.0f )
			{
				break;
			}
		}

		return stop_prediction
		{
			.eye =
			{
				current_eye.x + displacement.x,
				current_eye.y + displacement.y,
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
		if ( !hit.record || !hit.record->valid || hit.bone_index < 0 || hit.bone_index >= k_skeleton_bones )
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

				const auto max_fov_sq = fov_gate_sqr( settings::g_combat.m_zeusbot.max_fov.value );

				for ( auto i = 0; i < hitbox_set.count; ++i )
				{
					const auto& hb = hitbox_set.entries[ i ];

					if ( !bone_usable( hb, *record ) )
					{
						continue;
					}

					const auto& bone = skeleton[ hb.bone ];
					const auto center = hitbox_center( hb, bone );

					math::vector3 aim{};
					auto fov{ 0.0f };

					if ( !aim_within_fov( eye, center, ctx.view_angles, max_fov_sq, aim, fov ) )
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

				const auto max_fov_sq = fov_gate_sqr( settings::g_combat.m_knifebot.max_fov.value );

				for ( auto i = 0; i < hitbox_set.count; ++i )
				{
					const auto& hb = hitbox_set.entries[ i ];

					if ( !bone_usable( hb, *record ) )
					{
						continue;
					}

					const auto& bone = skeleton[ hb.bone ];
					const auto center = hitbox_center( hb, bone );
					const auto dist = ( center - eye ).length( );

					math::vector3 aim{};
					auto fov{ 0.0f };

					if ( !aim_within_fov( eye, center, ctx.view_angles, max_fov_sq, aim, fov ) )
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

		// seed tick = interpolated-eye player tick plus the lerp offset, written
		// into the history as player_tick_count so the server derives the spread
		// seed from a deterministic tick, not the live attack subtick (which
		// flips between tick_base and tick_base-1 on dt==1 shots)
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
			// search the correction on the raw aim angle: the server re-adds the
			// aim punch on top of what we write, so the seed check must match the
			// post-punch view; the punch is subtracted only when writing the
			// history/base angles. inaccuracy/spread come from the create-move
			// accuracy state, same as the server rolls with on the fire tick.
			auto correction_error{ 0.0f };
			const auto corrected = g_shared.find_spread_correction( aim_angle, stamp_tick, &correction_error );

			if ( corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f )
			{
				this->m_firing_this_tick = false;
				return;
			}

			// exact match returns zero error; otherwise accept the closest one
			// if the leftover spread still lands inside the target
			if ( correction_error > 0.0f )
			{
				const auto distance = ( tgt.hit.position - shoot_eye ).length( );
				const auto extent = tgt.hit.hitbox.radius > 0.001f ? tgt.hit.hitbox.radius : 2.0f;
				const auto tolerance = distance > 1.0f ? ( extent * 0.75f ) / distance : 0.0f;

				if ( correction_error > tolerance )
				{
					this->m_firing_this_tick = false;
					return;
				}
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

			// history carries the corrected roll and the seed-tick stamp;
			// the command angles keep the roll zeroed
			shared::write_history_entry( entry, history_angle, config.no_spread.value, record_time.tick + 1, &tgt.hit.source_eye );
		}

		set_command_button( cmd, cstypes::command_buttons::in_attack, true );

		if ( history_size > 0 )
		{
			cmd->csgo_user_cmd.set_attack1_start_history_index( history_size - 1 );
		}

		// hide onshot: base->viewangles still holds the fake antiaim angle from
		// earlier this tick. if it faces >~45deg off the target, sending the real
		// shot angle as our view would look like a hard snap to spectators and
		// resolvers. the bullet is unaffected (fixed by the history entries
		// above) - this only changes the view direction sent on this command.
		auto output_angle = command_angle;

		if ( settings::g_combat.m_antiaim.enabled.value && settings::g_combat.m_antiaim.hide_shots.value )
		{
			math::vector3 current_view_angles{};
			if ( const auto angles = base->viewangles( ) )
			{
				current_view_angles = { angles->x( ), angles->y( ), angles->z( ) };
			}

			math::vector3 forward{};
			math::helpers::angle_vectors_left( current_view_angles, &forward );

			const auto to_target = ( tgt.hit.record->origin - systems::g_prediction.pre( ).networked_origin ).normalized( );
			const auto facing_away = forward.dot( to_target ) < 0.707107f;

			if ( facing_away )
			{
				output_angle.x = 179.9f;
				output_angle.y = std::remainderf( command_angle.y + 180.0f, 360.0f );
			}
		}

		if ( const auto angles = base->mutable_viewangles( ) )
		{
			angles->set_x( output_angle.x );
			angles->set_y( output_angle.y );

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
			// the spread seed comes from the live CSGOInput view at serialize
			// time, so even silent no-spread must push the corrected angle into
			// the live view; restore the user's view at view setup afterwards
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

		// undo fire_gun's live-view write, in both the input and the view setup
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

	// auto lineup: finds wallbang positions, cached per map, checked incrementally
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

			// nothing penetrates past k_max_penetration_distance, so measure
			// along the bullet path and skip positions that cannot work
			if ( cand.records[ 0 ] )
			{
				constexpr auto max_pen_dist_sq{ shared::penetration::k_max_penetration_distance * shared::penetration::k_max_penetration_distance };

				if ( ( cand.records[ 0 ]->origin - pos ).length_sqr( ) > max_pen_dist_sq )
				{
					return false;
				}
			}

			const auto trace = systems::g_tracing.trace_hull( local_origin, pos, { -16, -16, 0 }, { 16, 16, 72 }, local.pawn, 0x1c3003, 4 );
			if ( trace.fraction < 1.0f )
			{
				return false;
			}

			// independent of the record and eye being tested, so build it once
			// (prepare_player re-queries the whole hitbox set and scan order)
			auto& mutable_cand = const_cast< candidate& >( cand );
			const auto scan_state = this->prepare_player( mutable_cand );

			// prepare_player returns a null config for a pawn with no scene node
			// and scan_record would deref it immediately
			if ( scan_state.hitbox_set.count <= 0 || !scan_state.config )
			{
				return false;
			}

			const auto eye_candidates = this->get_eye_candidates( );

			std::vector<scan_hit> hits;

			for ( auto ri = 0; ri < cand.record_count; ++ri )
			{
				auto* record = cand.records[ ri ];
				if ( !record || !record->valid )
				{
					continue;
				}

				for ( auto ei = 0; ei < eye_candidates.count; ++ei )
				{
					const auto shoot_eye = eye_candidates.entries[ ei ].position;

					hits.clear( );
					this->scan_record( scan_state, shoot_eye, ctx.predicted_inaccuracy, ctx, mutable_cand, record, local, hits );

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
