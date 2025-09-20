#pragma once

#include "../rocktree/node.hpp"
#include "../world/world.hpp"

class physics_node
{
  public:
    physics_node() = default;
    physics_node(world& game_world, const std::vector<mesh_data>& meshes, const glm::dmat4& world_matrix);
    ~physics_node();

    physics_node(physics_node&&) = delete;
    physics_node(const physics_node&) = delete;
    physics_node& operator=(physics_node&&) = delete;
    physics_node& operator=(const physics_node&) = delete;

  private:
    world* game_world_{};
    JPH::ShapeSettings::ShapeResult shape_{};
    JPH::Body* body_{};
};
