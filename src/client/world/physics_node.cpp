#include "physics_node.hpp"

#include "../rocktree/rocktree.hpp"

physics_node::physics_node(world& game_world, const std::vector<mesh_data>& meshes, const glm::dmat4& world_matrix)
	: game_world_(&game_world)
{
	if (meshes.empty())
	{
		return;
	}

	this->meshes_.reserve(meshes.size());

	bool has_indices{false};
	for (const auto& mesh_data : meshes)
	{
		if (mesh_data.indices.empty())
		{
			continue;
		}

		physics_mesh p_mesh{};
		p_mesh.vertices_.reserve(mesh_data.vertices.size());

		for (const auto& vertex : mesh_data.vertices)
		{
			const glm::dvec4 local_position{
				static_cast<double>(vertex.x), //
				static_cast<double>(vertex.y), //
				static_cast<double>(vertex.z), //
				1.0,
			};

			const auto position = world_matrix * local_position;

			p_mesh.vertices_.emplace_back(physics_node::vertex{
				static_cast<float>(position.x), //
				static_cast<float>(position.y), //
				static_cast<float>(position.z), //
			});
		}

		p_mesh.triangles_.reserve(std::max(static_cast<size_t>(2), mesh_data.indices.size()) - 2);
		for (size_t i = 2; i < mesh_data.indices.size(); ++i)
		{
			physics_node::triangle t{
				mesh_data.indices.at(i - 2), //
				mesh_data.indices.at(i - 1), //
				mesh_data.indices.at(i - 0), //
			};

			if (i & 1)
			{
				std::swap(t.x, t.y);
			}

			if (t.x == t.y || t.x == t.z || t.y == t.z)
			{
				continue;
			}

			const glm::vec3 v1 = p_mesh.vertices_.at(t.x);
			const glm::vec3 v2 = p_mesh.vertices_.at(t.y);
			const glm::vec3 v3 = p_mesh.vertices_.at(t.z);

			const auto vec1 = v1 - v2;
			const auto vec2 = v1 - v3;

			const auto normal = glm::cross(vec1, vec2);

			if (glm::length(normal) > 0.0f)
			{
				p_mesh.triangles_.emplace_back(std::move(t));
			}
		}

		has_indices |= !p_mesh.triangles_.empty();
		this->meshes_.emplace_back(std::move(p_mesh));
	}

	if (!has_indices)
	{
		return;
	}
}

physics_node::~physics_node()
{
	if (!this->game_world_)
	{
		return;
	}
}
