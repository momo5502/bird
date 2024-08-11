#pragma once

struct input_state
{
	bool exit{false};

	double left{0.0};
	double right{0.0};
	double up{0.0};
	double down{0.0};

	double boost{0.0};

	double mouse_x{0.0};
	double mouse_y{0.0};

	bool jumping{false};
	bool sprinting{false};

	bool gravity_toggle{false};
	bool shooting{false};
};

class window;

class input
{
public:
	input(const window& window)
		: window_(window)
	{
	}

	input_state get_input_state();

private:
	const window& window_;
	bool was_sprinting_{false};
	bool was_gravity_toggled_{false};
};
