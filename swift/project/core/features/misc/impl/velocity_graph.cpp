#include <pch/pch.hpp>
#include <core/features/misc/misc.hpp>
#include <core/systems/systems.hpp>
#include <core/rendering/rendering.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/math/math.hpp>
#include <core/settings.hpp>
#include <external/xdraw/xui/xui.hpp>
#include <protection/game_addresses.hpp>
#include <chrono>
#include <deque>
#include <algorithm>

namespace features::misc {

	namespace {
		struct velocity_data {
			std::deque<float> velocity_history;
			float max_velocity = 0.0f;
			float jump_height = 0.0f;
			float multiplicator = 1.0f;
			std::chrono::steady_clock::time_point last_max_reset;
			std::chrono::steady_clock::time_point last_mul_reset;
			static constexpr size_t max_history_size = 100;
			static constexpr float max_reset_interval = 1.0f; // seconds
			static constexpr float mul_reset_interval = 2.0f; // seconds
		};
		
		velocity_data g_velocity_data;
	}

	void velocity_graph::update() {
		const auto local = systems::g_local.get();
		if (!local.is_alive || !local.pawn)
			return;

		// Get velocity from pawn - use proper vector3 read like other parts of the codebase
		const auto velocity = memory::read<math::vector3>(local.pawn + SCHEMA("C_BaseEntity", "m_vecVelocity"_hash));
		const auto speed_2d = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
		
		// Calculate jump height (Z velocity component)
		g_velocity_data.jump_height = velocity.z;
		
		// Calculate multiplicator based on jump distance
		static float last_speed = 0.0f;
		static float max_jump_distance = 0.0f;
		const auto now = std::chrono::steady_clock::now();
		
		// Track horizontal distance during jumps
		if (g_velocity_data.jump_height > 0.1f) { // In air
			// Calculate potential jump distance based on current horizontal speed
			const float current_jump_distance = speed_2d * 0.1f; // Approximate distance calculation
			max_jump_distance = std::max(max_jump_distance, current_jump_distance);
		} else if (g_velocity_data.jump_height <= 0.0f && max_jump_distance > 0.0f) { // Just landed
			// Calculate multiplicator based on jump distance
			// Typical good long jump is around 245-255 units
			const float ideal_distance = 250.0f;
			g_velocity_data.multiplicator = std::min(max_jump_distance / ideal_distance, 2.0f);
			max_jump_distance = 0.0f; // Reset for next jump
		}
		
		// Reset multiplicator every 2 seconds if no jumps
		const auto elapsed_mul = std::chrono::duration<float>(now - g_velocity_data.last_mul_reset).count();
		if (elapsed_mul >= velocity_data::mul_reset_interval) {
			if (g_velocity_data.jump_height <= 0.0f) { // Only reset when on ground
				g_velocity_data.multiplicator = 1.0f;
			}
			g_velocity_data.last_mul_reset = now;
		}
		
		last_speed = speed_2d;
		
		// Add current speed to history
		g_velocity_data.velocity_history.push_back(speed_2d);
		if (g_velocity_data.velocity_history.size() > velocity_data::max_history_size) {
			g_velocity_data.velocity_history.pop_front();
		}

		// Update max velocity
		if (speed_2d > g_velocity_data.max_velocity) {
			g_velocity_data.max_velocity = speed_2d;
		}

		// Reset max velocity every 3-4 seconds
		const auto elapsed = std::chrono::duration<float>(now - g_velocity_data.last_max_reset).count();
		
		if (elapsed >= velocity_data::max_reset_interval) {
			g_velocity_data.max_velocity = speed_2d; // Reset to current speed
			g_velocity_data.last_max_reset = now;
		}
	}

	void velocity_graph::render() {
		const auto& cfg = settings::g_misc.m_hud.m_velocity;
		if (!cfg.graph.value) // График показывается независимо от индикатора
			return;
			
		const auto local = systems::g_local.get();
		if (!local.is_alive || !local.pawn || g_velocity_data.velocity_history.empty())
			return;

		auto& dl = xdraw::get();

		// Graph dimensions and position - using configurable values
		const float graph_width = cfg.graph_width.value;
		const float graph_height = cfg.graph_height.value;
		const float line_width = cfg.graph_line_width.value;
		
		// Position in bottom center (без текста сверху)
		const auto [screen_w, screen_h] = xdraw::viewport_size();
		const float x = (static_cast<float>(screen_w) - graph_width) * 0.5f;
		const float y = static_cast<float>(screen_h) - graph_height - 100.0f; // 100px from bottom

		// Draw velocity line (только линии, без текста)
		if (g_velocity_data.velocity_history.size() >= 2) {
			// Fixed scale to prevent graph going out of bounds
			const float fixed_max_scale = 500.0f; // Fixed maximum scale
			
			auto line_color = xdraw::color{ 255, 255, 255, 255 }; // White line
			
			for (size_t i = 1; i < g_velocity_data.velocity_history.size(); ++i) {
				const float speed1 = g_velocity_data.velocity_history[i - 1];
				const float speed2 = g_velocity_data.velocity_history[i];
				
				// Clamp speeds to prevent overflow
				const float clamped_speed1 = std::min(speed1, fixed_max_scale);
				const float clamped_speed2 = std::min(speed2, fixed_max_scale);
				
				const float norm1 = clamped_speed1 / fixed_max_scale;
				const float norm2 = clamped_speed2 / fixed_max_scale;
				
				const float x1 = x + ((i - 1) * graph_width) / static_cast<float>(velocity_data::max_history_size - 1);
				const float y1 = y + graph_height - (norm1 * graph_height);
				const float x2 = x + (i * graph_width) / static_cast<float>(velocity_data::max_history_size - 1);
				const float y2 = y + graph_height - (norm2 * graph_height);
				
				dl.line(x1, y1, x2, y2, line_color, line_width);
			}
		}
	}

	void velocity_graph::render_indicator() {
		const auto& cfg = settings::g_misc.m_hud.m_velocity;
		if (!cfg.indicator.value)
			return;
			
		const auto local = systems::g_local.get();
		if (!local.is_alive || !local.pawn)
			return;

		auto& dl = xdraw::get();
		const auto [screen_w, screen_h] = xdraw::viewport_size();
		
		// Get current values
		const float current_speed = g_velocity_data.velocity_history.empty() ? 0.0f : g_velocity_data.velocity_history.back();
		const float jump_height = g_velocity_data.jump_height;
		const float multiplicator = g_velocity_data.multiplicator;
		
		// Position calculation - Y configurable, X always center
		const float center_x = static_cast<float>(screen_w) * 0.5f;
		const float indicator_y = static_cast<float>(screen_h) * cfg.indicator_y.value;
		
		// Format strings
		char jump_label_buf[16];
		char jump_val_buf[32];
		char at_buf[16];
		char mul_val_buf[32];
		std::snprintf(jump_label_buf, sizeof(jump_label_buf), "jump: ");
		std::snprintf(jump_val_buf, sizeof(jump_val_buf), "%.0f", jump_height);
		std::snprintf(at_buf, sizeof(at_buf), " @ ");
		std::snprintf(mul_val_buf, sizeof(mul_val_buf), "%.2f", multiplicator);
		
		// Split speed text into parts for individual coloring
		char current_text[32], max_val_text[32];
		std::snprintf(current_text, sizeof(current_text), "%.0f", current_speed);
		std::snprintf(max_val_text, sizeof(max_val_text), "%.0f", g_velocity_data.max_velocity);
		
		// Colors
		constexpr auto white{ xdraw::color{ 255, 255, 255, 255 } };
		const auto accent_color = tokens::col_accent;
		auto max_speed_color = cfg.max_color.value;
		
		// Speed color based on velocity ranges
		xdraw::color speed_color;
		if (current_speed <= 100.0f) {
			speed_color = xdraw::color{ 255, 100, 100, 255 }; // Red
		} else if (current_speed <= 200.0f) {
			speed_color = xdraw::color{ 255, 160, 122, 255 }; // Orange  
		} else {
			speed_color = xdraw::color{ 144, 238, 144, 255 }; // Light Green
		}
		
		// Draggable jump height / multiplicator line (same size as the speed indicator)
		xdraw::push_font(rendering::g_fonts.inter_medium[rendering::fonts::size::medium]);
		const auto [jump_label_w, jump_label_h] = xdraw::measure_text(jump_label_buf);
		const auto [jump_val_w, jump_val_h] = xdraw::measure_text(jump_val_buf);
		const auto [at_w, at_h] = xdraw::measure_text(at_buf);
		const auto [mul_val_w, mul_val_h] = xdraw::measure_text(mul_val_buf);
		const float jump_total_w = jump_label_w + jump_val_w + at_w + mul_val_w;

		// Drag state for the jump/mul text; first frame snaps to the default centered position.
		static float jump_pos_x = -1.0f;
		static float jump_pos_y = -1.0f;
		if (jump_pos_x < 0.0f) {
			jump_pos_x = center_x - jump_total_w * 0.5f;
			jump_pos_y = indicator_y - jump_val_h - 10.0f;
		}

		const auto& input = xui::ctx().input;
		static bool dragging{ false };
		static float drag_off_x{ 0.0f };
		static float drag_off_y{ 0.0f };

		const auto jump_rect = xui::rect{ jump_pos_x, jump_pos_y, jump_total_w, jump_val_h };
		if (dragging) {
			if (!input.mouse_down) {
				dragging = false;
			} else {
				jump_pos_x = input.mouse_x - drag_off_x;
				jump_pos_y = input.mouse_y - drag_off_y;
			}
		} else if (input.mouse_clicked && input.in_rect(jump_rect)) {
			dragging = true;
			drag_off_x = input.mouse_x - jump_pos_x;
			drag_off_y = input.mouse_y - jump_pos_y;
		}

		float jump_x = jump_pos_x;
		dl.text(jump_x, jump_pos_y, jump_label_buf, white);
		jump_x += jump_label_w;
		dl.text(jump_x, jump_pos_y, jump_val_buf, accent_color);
		jump_x += jump_val_w;
		dl.text(jump_x, jump_pos_y, at_buf, white);
		jump_x += at_w;
		dl.text(jump_x, jump_pos_y, mul_val_buf, accent_color);
		xdraw::pop_font();
		
		// Draw main speed indicator (24px, centered)
		xdraw::push_font(rendering::g_fonts.inter_bold[rendering::fonts::size::xlarge]);
		
		// Measure both parts
		const auto [current_w, current_h] = xdraw::measure_text(current_text);
		const auto [max_val_w, max_val_h] = xdraw::measure_text(max_val_text);
		const auto [open_w, open_h] = xdraw::measure_text("(");
		const auto [close_w, close_h] = xdraw::measure_text(")");
		
		// Calculate total width and starting position
		const float total_w = current_w + open_w + max_val_w + close_w + 4.0f; // 4px spacing
		const float start_x = center_x - total_w * 0.5f;
		
		// Draw current speed (main color)
		dl.text(start_x, indicator_y, current_text, speed_color);
		
		// Draw white brackets around max speed (different color)
		float mx = start_x + current_w + 4.0f;
		dl.text(mx, indicator_y, "(", white);
		mx += open_w;
		dl.text(mx, indicator_y, max_val_text, max_speed_color);
		mx += max_val_w;
		dl.text(mx, indicator_y, ")", white);
		
		xdraw::pop_font();
	}
}