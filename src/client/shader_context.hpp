#pragma once
#include "shader.hpp"

class shader_context
{
public:
	shader_context();

	GLint transform_loc;
	GLint uv_offset_loc;
	GLint uv_scale_loc;
	GLint octant_mask_loc;
	GLint position_loc;
	GLint octant_loc;
	GLint texcoords_loc;
	GLint current_time_loc;
	GLint own_draw_time_loc;
	GLint child_draw_times_loc;
	GLint animation_time_loc;

	[[nodiscard]] scoped_shader use_shader() const;

private:
	gl_object vertex_array_object_{};
	shader shader_{};
};
