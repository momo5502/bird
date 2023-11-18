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
};

class window;

input_state get_input_state(const window& window);
