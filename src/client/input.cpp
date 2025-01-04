#include "std_include.hpp"

#include "input.hpp"
#include "window.hpp"

namespace
{
	template <typename... Integers>
	bool is_any_key_pressed(const window& window, Integers... keys)
	{
		int args[]{keys...};

		for (auto key : args)
		{
			if (window.is_key_pressed(key))
			{
				return true;
			}
		}

		return false;
	}

	template <typename... Integers>
	double get_pressed_key_value(const window& window, Integers... keys)
	{
		return is_any_key_pressed(window, keys...) ? 1.0 : 0.0;
	}

	input_state get_keyboard_state(const window& window)
	{
		input_state state{};
		/*
		state.exit = is_any_key_pressed(window, GLFW_KEY_ESCAPE);

		state.up = get_pressed_key_value(window, GLFW_KEY_UP, GLFW_KEY_W);
		state.left = get_pressed_key_value(window, GLFW_KEY_LEFT, GLFW_KEY_A);
		state.down = get_pressed_key_value(window, GLFW_KEY_DOWN, GLFW_KEY_S);
		state.right = get_pressed_key_value(window, GLFW_KEY_RIGHT, GLFW_KEY_D);

		state.boost = get_pressed_key_value(window, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_LEFT_CONTROL,
		                                    GLFW_KEY_RIGHT_CONTROL);
		state.gravity_toggle = is_any_key_pressed(window, GLFW_KEY_TAB);

		state.jumping = is_any_key_pressed(window, GLFW_KEY_SPACE);
		state.sprinting = is_any_key_pressed(window, GLFW_KEY_LEFT_ALT);
		state.shooting = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

		const auto mouse_position = window.get_mouse_position();
		state.mouse_x = mouse_position.first;
		state.mouse_y = mouse_position.second;
		*/
		return state;
	}

	double add_deadzone(const double value, const double deadzone, const double sensitivity = 1.0)
	{
		if (value >= deadzone)
		{
			return ((value - deadzone) / (1.0 - deadzone)) * sensitivity;
		}

		if (value <= -deadzone)
		{
			return ((value + deadzone) / (1.0 - deadzone)) * sensitivity;
		}

		return 0.0;
	}

	input_state get_gamepad_state()
	{
		input_state state{};

		/*GLFWgamepadstate gamepad_state{};
		if (!glfwJoystickIsGamepad(GLFW_JOYSTICK_1) || !glfwGetGamepadState(GLFW_JOYSTICK_1, &gamepad_state))
		{
			return state;
		}

		state.exit = gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_CIRCLE] == GLFW_PRESS;
		state.jumping = gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_CROSS] == GLFW_PRESS;
		state.sprinting = gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB] == GLFW_PRESS;

		state.gravity_toggle = gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_BACK] == GLFW_PRESS;
		state.shooting = gamepad_state.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] == GLFW_PRESS;

		double left_x = gamepad_state.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
		double left_y = gamepad_state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
		double right_x = gamepad_state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
		double right_y = gamepad_state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
		double right_trigger = (gamepad_state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0) / 2.0;
		double left_trigger = (gamepad_state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0) / 2.0;

		constexpr auto limit = 0.1;
		constexpr auto sensitivity_scale = 0.5;

		left_x = add_deadzone(left_x, limit);
		left_y = add_deadzone(left_y, limit);
		right_x = add_deadzone(right_x, limit, sensitivity_scale);
		right_y = add_deadzone(right_y, limit, sensitivity_scale);
		right_trigger = add_deadzone(right_trigger, limit);
		left_trigger = add_deadzone(left_trigger, limit);

		state.right = std::max(left_x, 0.0);
		state.left = std::abs(std::min(left_x, 0.0));

		state.down = std::max(left_y, 0.0);
		state.up = std::abs(std::min(left_y, 0.0));

		state.boost = right_trigger + left_trigger;

		state.mouse_x = right_x * 10.0;
		state.mouse_y = right_y * 10.0;
		*/
		return state;
	}

	input_state merge_input_states(const input_state& state_1, const input_state& state_2)
	{
		input_state state{};

		state.exit = state_1.exit || state_2.exit;

		state.up = std::max(state_1.up, state_2.up);
		state.left = std::max(state_1.left, state_2.left);
		state.down = std::max(state_1.down, state_2.down);
		state.right = std::max(state_1.right, state_2.right);

		state.boost = std::max(state_1.boost, state_2.boost);

		state.mouse_x = state_1.mouse_x + state_2.mouse_x;
		state.mouse_y = state_1.mouse_y + state_2.mouse_y;

		state.jumping = state_1.jumping || state_2.jumping;
		state.sprinting = state_1.sprinting || state_2.sprinting;

		state.gravity_toggle = state_1.gravity_toggle || state_2.gravity_toggle;
		state.shooting = state_1.shooting || state_2.shooting;

		return state;
	}

	input_state get_input_state(const window& window, bool& was_sprinting, bool& was_gravity_toggled)
	{
		const auto keyboard_state = get_keyboard_state(window);
		const auto gamepad_state = get_gamepad_state();

		auto state = merge_input_states(keyboard_state, gamepad_state);
		state.sprinting |= was_sprinting;

		const auto is_moving = (state.right > 0.0 || state.left > 0.0 || state.up > 0.0 || state.down > 0.0);
		was_sprinting = state.sprinting && is_moving;

		const auto is_toggling = state.gravity_toggle;
		state.gravity_toggle &= !was_gravity_toggled;
		was_gravity_toggled = is_toggling;

		return state;
	}
}

input_state input::get_input_state()
{
	return ::get_input_state(this->window_, this->was_sprinting_, this->was_gravity_toggled_);
}
