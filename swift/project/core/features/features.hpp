#pragma once

#include "combat/combat.hpp"
#include "esp/esp.hpp"
#include "misc/misc.hpp"
#include "movement/movement.hpp"
#include "world/world.hpp"
#include "changer/changer.hpp"

namespace features {

	namespace combat {

		inline shared g_shared{};
		inline misc g_misc{};
		inline rage g_rage{};
		inline legit g_legit{};

	} 

	namespace esp {

		namespace player {

			inline glow g_glow{};
			inline chams g_chams{};
			inline overlay g_overlay{};

		} 

		namespace item {

			inline glow g_glow{};
			inline chams g_chams{};
			inline overlay g_overlay{};

		} 

		namespace projectile {

			inline overlay g_overlay{};

		} 

		namespace other {

			inline overlay g_overlay{};

		} 

	} 

	namespace misc {

		inline projectile_trajectory g_projectile_trajectory{};
		inline dlight g_dlight{};
		inline impacts g_impacts{};
		inline removals g_removals{};
		inline camera g_camera{};
		inline hud g_hud{};
		inline other g_other{};
		inline velocity_graph g_velocity_graph{};
		inline scoreboard_weapons g_scoreboard_weapons{};

	} 

	namespace movement {

		inline bhop g_bhop{};
		inline airstrafe g_airstrafe{};
		inline test_strafer g_test_strafer{};
		inline jumpbug g_jumpbug{};
		inline fastladder g_fastladder{};
		inline edgejump g_edgejump{};
		inline slowwalk g_slowwalk{};

	} 

	namespace world {

		inline weather g_weather{};
		inline scene g_scene{};
		inline smoke g_smoke{};

	} 

	namespace changer {

		inline econ_item_system g_econ_item_system{};
		inline agents g_agents{};
		inline gloves g_gloves{};
		inline guns g_guns{};
		inline knives g_knives{};

	} 

} 