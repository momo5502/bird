#include "world_mesh.hpp"

#include "../rocktree/rocktree.hpp"

world_mesh::world_mesh(node& node)
    : node_data(node)
{
    this->meshes_.reserve(node.meshes_.size());
    for (const auto& mesh : node.meshes_)
    {
        this->meshes_.emplace_back(mesh);
    }

    if (node.sdata_.is_leaf && !node.meshes_.empty())
    {
        this->physics_node_.emplace(node.get_rocktree().with<world>(), node.meshes_, node.matrix_globe_from_mesh);
    }
}

void world_mesh::buffer_meshes()
{
    if (this->buffer_meshes_internal())
    {
        this->mark_as_buffered();
    }
}

bool world_mesh::is_buffered() const
{
    return this->buffer_state_ == buffer_state::buffered;
}

bool world_mesh::is_buffering() const
{
    return this->buffer_state_ == buffer_state::buffering;
}

bool world_mesh::mark_for_buffering()
{
    auto expected = buffer_state::unbuffered;
    return this->buffer_state_.compare_exchange_strong(expected, buffer_state::buffering);
}

float world_mesh::draw(const shader_context& ctx, const uint64_t frame_index, const float current_time,
                       const std::array<float, 8>& child_draw_time, const std::array<int, 8>& octant_mask)
{
    if (!this->draw_time_)
    {
        this->draw_time_ = current_time;
    }

    this->last_frame_index_ = frame_index;

    const auto own_draw_time = *this->draw_time_;

    glUniform1f(ctx.own_draw_time_loc, own_draw_time);

    glUniform1iv(ctx.octant_mask_loc, 8, octant_mask.data());
    glUniform1fv(ctx.child_draw_times_loc, 8, child_draw_time.data());

    for (auto& mesh : this->meshes_)
    {
        mesh.draw(ctx);
    }

    return own_draw_time;
}

void world_mesh::buffer_queue(std::queue<world_mesh*> meshes)
{
    std::queue<world_mesh*> meshes_to_notify{};

    while (!meshes.empty())
    {
        auto* mesh = meshes.front();
        meshes.pop();

        if (mesh && !mesh->get_node().is_being_deleted() && mesh->buffer_meshes_internal())
        {
            meshes_to_notify.push(mesh);
        }
    }

    glFinish();

    while (!meshes_to_notify.empty())
    {
        auto* node = meshes_to_notify.front();
        meshes_to_notify.pop();

        node->mark_as_buffered();
    }
}

bool world_mesh::buffer_meshes_internal()
{
    if (this->is_buffered())
    {
        return false;
    }

    const auto& node = this->get_node();

    auto& game_world = node.get_rocktree().with<world>();
    auto& bufferer = game_world.get_bufferer();
    auto& shader_ctx = game_world.get_shader_context();

    for (auto& m : this->meshes_)
    {
        m.buffer(bufferer, shader_ctx);
    }

    return true;
}

void world_mesh::mark_as_buffered()
{
    this->buffer_state_ = buffer_state::buffered;
}
