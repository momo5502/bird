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
	struct vertex
	{
		float x{0.0f};
		float y{0.0f};
		float z{0.0f};
	};

	struct triangle
	{
		uint16_t x{};
		uint16_t y{};
		uint16_t z{};
	};

	struct physics_mesh
	{
		std::vector<vertex> vertices_{};
		std::vector<triangle> triangles_{};
		std::unique_ptr<reactphysics3d::TriangleVertexArray> vertex_array_{};
	};

	world* game_world_{};
	std::vector<physics_mesh> meshes_{};

	reactphysics3d::TriangleMesh* triangle_mesh_{};
	reactphysics3d::ConcaveMeshShape* concave_shape_{};
	reactphysics3d::RigidBody* body_{};
};
