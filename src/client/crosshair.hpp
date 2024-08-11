#pragma once
#include "shader.hpp"

class crosshair
{
public:
	crosshair();
	~crosshair() = default;

	crosshair(const crosshair&) = delete;
	crosshair& operator=(const crosshair&) = delete;

	crosshair(crosshair&& obj) noexcept = delete;
	crosshair& operator=(crosshair&& obj) noexcept = delete;

	void draw() const;

private:
	shader shader_{};
	gl_object vao_{};
	gl_object vertex_buffer_{};

	GLint position_loc_{};
	GLint screen_size_loc_{};
};
