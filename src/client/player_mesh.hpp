#pragma once
#include "shader.hpp"
#include "gl_objects.hpp"


class player_mesh
{
public:
	player_mesh(gl_bufferer& bufferer);

	void draw(const glm::dmat4& viewprojection, const glm::dvec3& position, const glm::dvec3& orientation) const;

private:
	shader shader_{};

	gl_object vao_{};
	gl_object index_buffer_{};
	gl_object vertex_buffer_{};
	gl_object vertex_color_buffer_{};

	GLint transform_loc_{};
};