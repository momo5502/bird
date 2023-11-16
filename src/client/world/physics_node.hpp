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

		bool operator==(const vertex& v) const
		{
			const auto equal = [](const float a, const float b)
			{
				constexpr auto epsilon = std::numeric_limits<float>::epsilon();
				return fabs(a - b) < epsilon;
			};

			return equal(this->x, v.x) && equal(this->y, v.y) && equal(this->z, v.z);
		}

		operator glm::vec3() const
		{
			return {x, y, z};
		}
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
	};

	world* game_world_{};
	std::vector<physics_mesh> meshes_{};
};
