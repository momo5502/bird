#include "physics_node.hpp"

#include "../rocktree/rocktree.hpp"

physics_node::physics_node(world& game_world, const std::vector<mesh_data>& meshes, const glm::dmat4& world_matrix)
	: game_world_(&game_world)
{
	if (meshes.empty())
	{
		return;
	}

	JPH::VertexList vertices{};
	JPH::IndexedTriangleList triangles{};

	for (const auto& mesh_data : meshes)
	{
		if (mesh_data.indices.empty())
		{
			continue;
		}

		const auto base_index = static_cast<uint32_t>(vertices.size());

		for (const auto& vertex : mesh_data.vertices)
		{
			const glm::dvec4 local_position{
				static_cast<double>(vertex.x), //
				static_cast<double>(vertex.y), //
				static_cast<double>(vertex.z), //
				1.0,
			};

			const auto position = world_matrix * local_position;

			vertices.emplace_back( //
				static_cast<float>(position.x), //
				static_cast<float>(position.y), //
				static_cast<float>(position.z) //
			);
		}

		for (size_t i = 2; i < mesh_data.indices.size(); ++i)
		{
			auto index1 = base_index + mesh_data.indices.at(i - 2);
			auto index2 = base_index + mesh_data.indices.at(i - 1);
			auto index3 = base_index + mesh_data.indices.at(i - 0);

			if (i & 1)
			{
				std::swap(index1, index2);
			}

			triangles.emplace_back(index1, index2, index3);
		}
	}

	auto& body_interface = this->game_world_->get_physics_system().GetBodyInterface();

	JPH::MeshShapeSettings mesh_shape_settings(std::move(vertices), std::move(triangles));
	if (mesh_shape_settings.mIndexedTriangles.empty())
	{
		return;
	}

	this->shape_ = mesh_shape_settings.Create();

	JPH::BodyCreationSettings body_settings(this->shape_.Get(), JPH::RVec3(0.0_r, 0.0_r, 0.0_r),
	                                        JPH::Quat::sIdentity(),
	                                        JPH::EMotionType::Static, Layers::NON_MOVING);

	this->body_ = body_interface.CreateBody(body_settings);
	assert(this->body_);

	body_interface.AddBody(this->body_->GetID(), JPH::EActivation::DontActivate);
}

physics_node::~physics_node()
{
	if (!this->game_world_ || !this->body_)
	{
		return;
	}

	auto& body_interface = this->game_world_->get_physics_system().GetBodyInterface();

	body_interface.RemoveBody(this->body_->GetID());
	body_interface.DestroyBody(this->body_->GetID());
}
