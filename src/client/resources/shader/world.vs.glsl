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

void main()
{
    bool is_masked = octant_mask[int(octant)];
    float child_time = child_draw_times[int(octant)];

    float half_animation_time = animation_time / 2.0;
    v_alpha = clamp(current_time - own_draw_time, 0.0, half_animation_time) / half_animation_time;

    if (is_masked)
    {
        float fadeout_start_time = max(own_draw_time, child_time) + half_animation_time;
        float own_hide_alpha = 1.0 - (clamp(current_time - fadeout_start_time, 0.0, half_animation_time) / half_animation_time);
        v_alpha = v_alpha * own_hide_alpha;
    }

    float mask = 1.0;
    if (v_alpha == 0.0)
    {
        mask = 0.0;
    }

    vec4 worldpos = worldmatrix * vec4(position, 1.0);
    v_worldpos = worldpos.xyz / worldpos.w;

    v_normal = normal;
    v_texcoords = (texcoords + uv_offset) * uv_scale * mask;
    gl_Position = transform * vec4(position, 1.0) * mask;
}
