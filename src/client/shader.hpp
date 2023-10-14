#pragma once
#include "gl_object.hpp"

class shader
{
public:
	shader() = default;
	shader(const std::string_view& vertex_shader, const std::string_view& fragment_shader);

	GLuint get_program() const;

private:
	gl_object program_{};
};
