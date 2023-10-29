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
uniform float current_time;
uniform float own_draw_time;
uniform float child_draw_times[8];
uniform float animation_time;

attribute vec3 position;
attribute float octant;
attribute vec2 texcoords;

varying vec2 v_texcoords;
varying float v_alpha;

void main() {

    bool is_masked = octant_mask[int(octant)];
    float child_time = child_draw_times[int(octant)];

    float half_animation_time = animation_time / 2.0;
    v_alpha = clamp(current_time - own_draw_time, 0.0, half_animation_time) / half_animation_time;

    if(is_masked)
	{
		float fadeout_start_time = max(own_draw_time, child_time) + half_animation_time;
		float own_hide_alpha = 1.0 - (clamp(current_time - fadeout_start_time, 0.0, half_animation_time) / half_animation_time);
	    v_alpha = v_alpha * own_hide_alpha;
    }

	float mask = 1.0;
	if(v_alpha == 0.0)
	{
		mask = 0.0;
	}

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
varying float v_alpha;

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
	if(v_alpha == 0.0) {
		discard;
	}

	if(v_alpha != 1.0) {
		float selector = 1.0 / v_alpha;
		float sum = rand(v_texcoords + gl_FragCoord.xy) * selector;
		
		if(int(mod(sum, selector)) != 0) {
			discard;
		}
	}

	gl_FragColor = vec4(texture2D(texture, v_texcoords).rgb, 1.0);
}
)code";
	}
}

shader_context::shader_context()
	: vertex_array_object_(create_and_bind_vertex_array_object())
	  , shader_(get_vertex_shader(), get_fragment_shader())
{
	this->shader_.use();

	const auto program = this->shader_.get_program();

	this->transform_loc = glGetUniformLocation(program, "transform");
	this->uv_offset_loc = glGetUniformLocation(program, "uv_offset");
	this->uv_scale_loc = glGetUniformLocation(program, "uv_scale");
	this->octant_mask_loc = glGetUniformLocation(program, "octant_mask");
	this->texture_loc = glGetUniformLocation(program, "texture");

	this->position_loc = glGetAttribLocation(program, "position");
	this->octant_loc = glGetAttribLocation(program, "octant");
	this->texcoords_loc = glGetAttribLocation(program, "texcoords");

	this->current_time_loc = glGetUniformLocation(program, "current_time");
	this->own_draw_time_loc = glGetUniformLocation(program, "own_draw_time");
	this->child_draw_times_loc = glGetUniformLocation(program, "child_draw_times");
	this->animation_time_loc = glGetUniformLocation(program, "animation_time");

	glEnableVertexAttribArray(this->position_loc);
	glEnableVertexAttribArray(this->octant_loc);
	glEnableVertexAttribArray(this->texcoords_loc);
}

void shader_context::use_shader() const
{
	this->shader_.use();
}
