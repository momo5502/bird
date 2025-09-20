#include "std_include.hpp"

#include "shader_context.hpp"

shader_context::shader_context(const std::string_view vertex_shader, const std::string_view fragment_shader)
    : shader_(vertex_shader, fragment_shader)
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
