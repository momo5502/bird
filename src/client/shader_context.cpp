#include "std_include.hpp"

#include "shader_context.hpp"

#include <utils/finally.hpp>

namespace
{
	void delete_vertex_array_object(const GLuint vao)
	{
		glBindVertexArray(0);
		glDeleteVertexArrays(1, &vao);
	}

	gl_object create_and_bind_vertex_array_object()
	{
		GLuint vao = 0;
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);

		return {vao, delete_vertex_array_object};
	}

	std::string_view get_vertex_shader()
	{
		return R"code(
	uniform mat4 transform;
	uniform vec2 uv_offset;
	uniform vec2 uv_scale;
	uniform bool octant_mask[8];
	attribute vec3 position;
	attribute float octant;
	attribute vec2 texcoords;
	varying vec2 v_texcoords;
	void main() {
		float mask = octant_mask[int(octant)] ? 0.0 : 1.0;
		v_texcoords = (texcoords + uv_offset) * uv_scale * mask;
		gl_Position = transform * vec4(position, 1.0) * mask;
	}
)code";
	}

	std::string_view get_fragment_shader()
	{
		return R"code(
	#ifdef GL_ES
	precision mediump float;
	#endif

	uniform sampler2D texture;
	varying vec2 v_texcoords;
	void main() {
		gl_FragColor = vec4(texture2D(texture, v_texcoords).rgb, 1.0);
	}
)code";
	}
}

shader_context::shader_context()
	: vertex_array_object_(create_and_bind_vertex_array_object())
	  , shader_(get_vertex_shader(), get_fragment_shader())
{
	const auto program = this->shader_.get_program();

	glUseProgram(program);

	this->transform_loc = glGetUniformLocation(program, "transform");
	this->uv_offset_loc = glGetUniformLocation(program, "uv_offset");
	this->uv_scale_loc = glGetUniformLocation(program, "uv_scale");
	this->octant_mask_loc = glGetUniformLocation(program, "octant_mask");
	this->texture_loc = glGetUniformLocation(program, "texture");
	this->position_loc = glGetAttribLocation(program, "position");
	this->octant_loc = glGetAttribLocation(program, "octant");
	this->texcoords_loc = glGetAttribLocation(program, "texcoords");

	glEnableVertexAttribArray(this->position_loc);
	glEnableVertexAttribArray(this->octant_loc);
	glEnableVertexAttribArray(this->texcoords_loc);
}
