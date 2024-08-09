#include "std_include.hpp"
#include "player_mesh.hpp"

#include <utils/finally.hpp>

namespace
{
	std::string_view get_vertex_shader()
	{
		return R"code(
uniform mat4 transform;
attribute vec3 position;
attribute vec3 vertex_color;

varying vec3 color;

void main()
{
	color = vertex_color;
    gl_Position = transform * vec4(position, 1.0);
}
)code";
	}

	std::string_view get_fragment_shader()
	{
		return R"code(
varying vec3 color;

void main()
{
	gl_FragColor = vec4(color, 1.0);
}
)code";
	}

	glm::dmat4 get_rotation_matrix(const glm::dvec3& position, const glm::dvec3& orientation)
	{
		const auto target_up = glm::normalize(position);
		const auto target_right = glm::normalize(glm::cross(orientation, target_up));
		const auto target_backwards = glm::normalize(glm::cross(target_right, target_up));

		glm::dmat4 rotationMatrix = glm::mat4(1.0f);
		rotationMatrix[0] = glm::vec4(target_right, 0.0f);
		rotationMatrix[1] = glm::vec4(target_up, 0.0f);
		rotationMatrix[2] = glm::vec4(target_backwards, 0.0f);

		return rotationMatrix;
	}

	glm::dmat4 get_model_matrix(const glm::dvec3& position, const glm::dvec3& orientation)
	{
		constexpr auto height = 2.5;
		constexpr auto width = 1.0;

		return
			glm::translate(position) //
			* get_rotation_matrix(position, orientation) //
			* glm::translate(glm::dvec3{0.0, -1.0, 0.0})
			* glm::scale(glm::dvec3{width, height, width}); //
	}

	constexpr float vertices[] = {
		// Front face
		0.5, 0.5, 0.5,
		-0.5, 0.5, 0.5,
		-0.5, -0.5, 0.5,
		0.5, -0.5, 0.5,

		// Back face
		0.5, 0.5, -0.5,
		-0.5, 0.5, -0.5,
		-0.5, -0.5, -0.5,
		0.5, -0.5, -0.5,
	};

	constexpr float vertex_colors[] = {
		1.0f, 0.4f, 0.6f,
		1.0f, 0.9f, 0.2f,
		0.7f, 0.3f, 0.8f,
		0.5f, 0.3f, 1.0f,

		0.2f, 0.6f, 1.0f,
		0.6f, 1.0f, 0.4f,
		0.6f, 0.8f, 0.8f,
		0.4f, 0.8f, 0.8f,
	};

	constexpr uint16_t triangle_indices[] = {
		// Front
		0, 1, 2,
		2, 3, 0,

		// Right
		0, 3, 7,
		7, 4, 0,

		// Bottom
		2, 6, 7,
		7, 3, 2,

		// Left
		1, 5, 6,
		6, 2, 1,

		// Back
		4, 7, 6,
		6, 5, 4,

		// Top
		5, 1, 0,
		0, 4, 5,
	};
}

player_mesh::player_mesh(gl_bufferer& bufferer)
	: shader_(get_vertex_shader(), get_fragment_shader())
	  , vao_(create_vertex_array_object())
	  , index_buffer_(bufferer.create_buffer())
	  , vertex_buffer_(bufferer.create_buffer())
	  , vertex_color_buffer_(bufferer.create_buffer())
{
	const auto _ = utils::finally([]
	{
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	});

	glBindVertexArray(this->vao_);

	const auto color_loc = this->shader_.attribute("vertex_color");
	const auto position_loc = this->shader_.attribute("position");
	this->transform_loc_ = this->shader_.uniform("transform");

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->index_buffer_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(triangle_indices), triangle_indices, GL_STATIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, this->vertex_buffer_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(position_loc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
	glEnableVertexAttribArray(position_loc);

	glBindBuffer(GL_ARRAY_BUFFER, this->vertex_color_buffer_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_colors), vertex_colors, GL_STATIC_DRAW);

	glVertexAttribPointer(color_loc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
	glEnableVertexAttribArray(color_loc);
}

void player_mesh::draw(const glm::dmat4& viewprojection, const glm::dvec3& position,
                       const glm::dvec3& orientation) const
{
	const auto _1 = this->shader_.use();

	const glm::mat4 transform = viewprojection * get_model_matrix(position, orientation);

	glUniformMatrix4fv(this->transform_loc_, 1, GL_FALSE, &transform[0][0]);

	scoped_vao _2{this->vao_};
	glDrawElements(GL_TRIANGLES, 6 * 2 * 3, GL_UNSIGNED_SHORT, nullptr);
}
