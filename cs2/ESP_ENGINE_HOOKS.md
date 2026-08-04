# ESP / engine-hook pipeline — how `velocity` wires it together

Notes from reading `velocity-main/cs2/velocity-cs2/project/` this session. File:line references are current as of this snapshot; re-check after any pull from upstream `velocity-reborn` or edits of our own.

## 1. Hook installation

All feature-facing engine hooks are installed in one place: `hooks::cheat::initialize()` in [`core/hooks/impl/cheat.cpp:15`](velocity-main/cs2/velocity-cs2/project/core/hooks/impl/cheat.cpp:15). It's a single `hooking::manager::create({...})` call with one entry per target — `{ &member_trampoline, &detour_fn, "name", pattern_or_address }`. Two targets resolve by fixed address (`present`, `resize_buffers`, from `addresses::functions`), everything else resolves via `PATTERN(patterns::...)` byte-pattern scan (`protection/patterns.cpp`).

The ESP-relevant hook targets, in call order per frame:

| Hook | Fires | Purpose for ESP |
|---|---|---|
| `present` | Once per frame, on the D3D11 swapchain's `Present` vtable slot | Entry point for our own rendering (`rendering::g_context.on_present`) and where the window-proc / `OMSetRenderTargets` hooks get lazily installed once the device/context exist. |
| `frame_stage_notify` | Per client frame stage | Drives per-frame system updates (`g_entities`, `g_local`, chams backtrack/onshot) at stage 6/7. Not itself an ESP draw call, but ESP's *data* (entity list, local player state) is refreshed here before rendering. |
| `render_view` | Per view render | Updates `systems::g_view` (camera matrix/FOV/origin) and `systems::g_frame_data` — this is what makes `systems::view::project()` (world→screen) valid for that frame. |
| `is_glowing` / `get_glow_color` | Engine glow-outline query, per glowable entity | Routed to `features::esp::player::g_glow` / `features::esp::item::g_glow` before falling back to the original. This is the *glow* ESP path (outline highlight), separate from chams and from the 2D overlay. |
| `generate_primitives` | Per scene object, when the renderer builds draw primitives | Routed to `features::esp::player::g_chams` / `features::esp::item::g_chams` / `systems::g_model_preview` before falling back to the original — this is the *chams* ESP path (material/mesh override, e.g. wallhack-through-walls model rendering). |
| `sort_primitives` | After primitive generation | Notifies `g_chams.on_sort_primitives(...)` — lets chams reorder/re-tag entries it injected. |
| `get_transforms_for_hitbox_list` | Per-entity hitbox transform fetch | Not ESP proper — this is where `combat::g_shared`'s autowall/backtrack record substitutes cached bone transforms. Documented here because it shares the same bone-cache plumbing ESP's skeleton overlay uses. |

Everything else in that `initialize()` call (movement, camera override, radar, HUD removals, etc.) is unrelated to ESP specifically.

## 2. The three separate "ESP" mechanisms

`core/features/esp/esp.hpp` defines three genuinely different techniques, easy to conflate:

1. **Glow** (`player::glow`, `item::glow`) — answers the engine's own glow-outline query (`is_glowing`/`get_glow_color` hooks above). Cheapest, most native-looking: reuses the game's built-in glow-outline renderer (the same one used for defuser-carrier highlighting), just decides who gets it and what color.
2. **Chams** (`player::chams`, `item::chams`) — intercepts `generate_primitives` at the point the renderer is about to build a scene object's draw primitives, and substitutes/duplicates them with an ignorez (depth-ignoring, i.e. see-through-walls) or translucent material clone (`systems::materials::get_or_create_clone`, `clone_type::ignorez` / `translucent`). This is the "see player through wall" effect. `player::chams` additionally has `backtrack` and `onshot` sub-objects — these track *duplicate* scene objects (a historical-position ghost for backtracking, and a hit-flash ghost) that also need chams applied/excluded in `generate_primitives`/`is_active` checks.
3. **2D overlay** (`player::overlay`, `item::overlay`, `projectile::overlay`, `other::overlay`) — the classic box/skeleton/health-bar/name ESP, drawn as 2D primitives on top of the frame. Not a hook at all — it's a plain per-frame draw call from `rendering::impl/context.cpp:104-114`, inside whatever `present`-driven render pass rendering uses (see §3).

These three are independent and can be (and are) combined per-entity — e.g. a player can have glow *and* chams *and* a box drawn over them simultaneously; nothing in the code couples them.

## 3. Where the 2D overlay actually gets drawn

`core/rendering/impl/context.cpp` (called from the `present` hook's frame pipeline) calls, in this order:
```
features::combat::g_misc.antiaim().on_render(dl)
features::movement::g_edgebug.on_render(dl)
features::esp::item::g_overlay.on_render(dl)
features::esp::projectile::g_overlay.on_render(dl, middle_layer)
features::esp::player::g_overlay.on_render(dl)
features::misc::g_projectile_trajectory.on_render(dl)
features::combat::g_rage.on_render(dl)
features::combat::g_legit.on_render(dl)
features::misc::g_impacts.on_render(dl)
features::misc::g_hud.on_render(dl)
features::esp::other::g_overlay.on_render(dl)
```
`dl` is an `xdraw::draw_list` (the bundled `external/xdraw` immediate-mode 2D drawing lib, not ImGui — worth noting since the sibling `cs2_inter` project uses ImGui directly). Draw order here is z-order: item ESP under projectile ESP under player ESP under HUD, etc.

`player::overlay::on_render` (`core/features/esp/player/player.overlay.cpp`) is the reference implementation to read for "how does this cheat build one ESP entry": for each cached player entity it builds a local `info` struct (health/team/money/armor/ping/distance/name/27 bones/weapon), then:
- `systems::bounds::get(entity)` → 2D screen-space bounding box (feeds `add_box`)
- `systems::g_tracing.is_visible(camera_origin, head_bone_position, pawn, view_pawn)` (`player.overlay.cpp:923`) → the visible-vs-occluded color split (`cfg.visible_color` vs `cfg.occluded_color`) — this is a real engine trace call through `systems::tracing`, not a guess
- per-element draw helpers (`add_box`, `add_skeleton`, `add_health_bar`, `add_ammo_bar`, `add_name`, `add_weapon`, `add_flags`, `add_oof_arrow`) each take the shared `bounds`/`info` and a settings struct, and animate health/ammo bars via a spring (`animation::spring`) keyed per-entity in `m_animations`.

## 4. Supporting systems ESP depends on

- **`systems::g_view`** (`core/systems/impl/view.cpp`) — camera matrix/origin/angles/FOV, refreshed in the `render_view` hook. `project()`/`project_full()` are the world→screen functions every overlay draw call ultimately goes through.
- **`systems::g_entities`** (`core/systems/impl/entities.cpp`) — the tracked-entity cache, populated by the `add_entity`/`remove_entity` hooks (also installed in `cheat::initialize`, not listed above since they're bookkeeping, not ESP-specific). `get_schema_name`/schema-hash lookups are how hooks like `is_glowing`/`generate_primitives` figure out *what* a given `scene_object`/`glow_property` actually belongs to (player vs. item vs. projectile) before dispatching to the right ESP class.
- **`systems::g_bounds`** — per-entity 2D bounding box, used by `add_box`/`add_skeleton`/etc.
- **`systems::g_bones`** — the 27-entry skeleton array (`systems::bones::data`) used by skeleton ESP and by chams' backtrack/onshot ghost setup (`object::setup_bones`).
- **`systems::g_tracing`** — the actual line-of-sight raycast (`is_visible`) backing the visible/occluded color split; separate from `combat`'s autowall trace path (different consumer, same underlying primitive family under `core/systems/impl/tracing.cpp` — worth checking whether they share one native trace call chain or two before assuming behavior transfers between ESP visibility and autowall).
- **`systems::materials`** — chams' material clone cache (`get_or_create_clone`, `clear_clones`), backing the ignorez/translucent effect.

## 5. Things to verify before trusting any of this live

Per CLAUDE.md's standing caution: none of the offsets/patterns backing these hooks (`patterns::is_glowing`, `patterns::generate_primitives`, `patterns::sort_primitives`, etc., in `protection/patterns.cpp`) have been confirmed against a live CS2 build this session. Before relying on any of this for real feature work:
- Confirm each pattern still resolves (hook install failure is a hard `INIT_FAIL` in `entry.cpp`, so a silent hang/crash at launch likely means one of these patterns is stale).
- Confirm the struct-offset reads inside the hooks (e.g. `glow_property + 0x18` for owner entity in `is_glowing`/`get_glow_color`, `scene_object + 0xc0` for owner handle in `generate_primitives`) still match — these are raw offsets, not schema-looked-up fields, so they're the most likely to silently break on a game update.
- The `is_visible` trace call and the chams ignorez material path are the two most likely candidates for "renders, but wrong" bugs (vs. hard crashes) if an offset drifts, since both fail soft (wrong color / no see-through) rather than faulting.

No anti-cheat-evasion code is involved in any of the above — this is standard hook-and-read/inject-primitive ESP plumbing, unrelated to the `vac.cpp` finding in POLICY.md.
