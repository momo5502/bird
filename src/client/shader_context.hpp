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
	GLint texture_loc;
	GLint position_loc;
	GLint octant_loc;
	GLint texcoords_loc;

	void use_shader() const;

private:
	gl_object vertex_array_object_{};
	shader shader_{};
};
