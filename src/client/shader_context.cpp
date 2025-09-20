#include "std_include.hpp"

#include "shader_context.hpp"

namespace
{
    std::string_view get_vertex_shader()
    {
        return R"code(
uniform mat4 transform;
uniform mat4 worldmatrix;
uniform vec2 uv_offset;
uniform vec2 uv_scale;
uniform bool octant_mask[8];
uniform float current_time;
uniform float own_draw_time;
uniform float child_draw_times[8];
uniform float animation_time;

attribute vec3 position;
attribute vec3 normal;
attribute float octant;
attribute vec2 texcoords;

varying vec2 v_texcoords;
varying float v_alpha;
varying vec3 v_normal;
varying vec3 v_worldpos;

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


	vec4 worldpos = worldmatrix * vec4(position, 1.0);
	v_worldpos = worldpos.xyz / worldpos.w;

    v_normal = normal;
    v_texcoords = (texcoords + uv_offset) * uv_scale * mask;
    gl_Position = transform * vec4(position, 1.0) * mask;
}
)code";
    }

    std::string_view get_fragment_shader()
    {
        return R"code(
#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D textureObj;
varying vec2 v_texcoords;
varying float v_alpha;
varying vec3 v_normal;
varying vec3 v_worldpos;

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
	if(v_alpha <= 0.0001) {
		discard;
	}

	if(v_alpha < 0.999) {
		float selector = 1.0 / v_alpha;

		vec2 seed = v_texcoords + gl_FragCoord.xy;
		
		/*int grouping = 2;
		vec2 seed = gl_FragCoord.xy;
		seed.x = float(int(seed.x) / grouping);
		seed.y = float(int(seed.y) / grouping);
		seed.x += float(int(v_texcoords.x) / grouping);
		seed.y += float(int(v_texcoords.y) / grouping);*/

		float sum = rand(seed) * selector;
		
		if(int(mod(sum, selector)) != 0) {
			discard;
		}
	}

	gl_FragColor = vec4(texture2D(textureObj, v_texcoords).rgb, 1.0);
}
)code";
    }
}

shader_context::shader_context()
    : shader_(get_vertex_shader(), get_fragment_shader())
{
    const auto& s = this->shader_;

    const auto _ = s.use();

    this->transform_loc = s.uniform("transform");
    this->worldmatrix_loc = s.uniform("worldmatrix");
    this->uv_offset_loc = s.uniform("uv_offset");
    this->uv_scale_loc = s.uniform("uv_scale");
    this->octant_mask_loc = s.uniform("octant_mask");

    this->position_loc = s.attribute("position");
    this->normal_loc = s.attribute("normal");
    this->octant_loc = s.attribute("octant");
    this->texcoords_loc = s.attribute("texcoords");

    this->current_time_loc = s.uniform("current_time");
    this->own_draw_time_loc = s.uniform("own_draw_time");
    this->child_draw_times_loc = s.uniform("child_draw_times");
    this->animation_time_loc = s.uniform("animation_time");
}

scoped_shader shader_context::use_shader() const
{
    return this->shader_.use();
}
