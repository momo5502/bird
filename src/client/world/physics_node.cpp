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

		has_indices = true;

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

		// technically -2, but fuck it
		p_mesh.triangles_.reserve(mesh_data.indices.size());
		for (size_t i = 2; i < mesh_data.indices.size(); ++i)
		{
			auto& triangle = p_mesh.triangles_.emplace_back(physics_node::triangle{
				mesh_data.indices.at(i - 2), //
				mesh_data.indices.at(i - 1), //
				mesh_data.indices.at(i - 0), //
			});

			if (i & 1)
			{
				std::swap(triangle.x, triangle.y);
			}
		}

		this->meshes_.emplace_back(std::move(p_mesh));
	}

	if (!has_indices)
	{
		return;
	}

	this->game_world_->access_physics([this](reactphysics3d::PhysicsCommon& common, reactphysics3d::PhysicsWorld& world)
	{
		this->triangle_mesh_ = common.createTriangleMesh();

		for (auto& mesh : this->meshes_)
		{
			mesh.vertex_array_ = std::make_unique<reactphysics3d::TriangleVertexArray>(
				static_cast<uint32_t>(mesh.vertices_.size()), mesh.vertices_.data(),
				static_cast<uint32_t>(sizeof(physics_node::vertex)),
				static_cast<uint32_t>(mesh.triangles_.size()), mesh.triangles_.data(),
				static_cast<uint32_t>(sizeof(physics_node::triangle)),
				reactphysics3d::TriangleVertexArray::VertexDataType::VERTEX_FLOAT_TYPE,
				reactphysics3d::TriangleVertexArray::IndexDataType::INDEX_SHORT_TYPE);

			this->triangle_mesh_->addSubpart(mesh.vertex_array_.get());
		}

		this->concave_shape_ = common.createConcaveMeshShape(this->triangle_mesh_);

		this->body_ = world.createRigidBody({});
		this->body_->setType(reactphysics3d::BodyType::STATIC);
		this->body_->addCollider(this->concave_shape_, {});
	});
}

physics_node::~physics_node()
{
	if (!this->game_world_)
	{
		return;
	}

	this->game_world_->access_physics([this](reactphysics3d::PhysicsCommon& common, reactphysics3d::PhysicsWorld& world)
	{
		if (this->body_)
		{
			world.destroyRigidBody(this->body_);
			this->body_ = nullptr;
		}

		if (this->concave_shape_)
		{
			common.destroyConcaveMeshShape(this->concave_shape_);
			this->concave_shape_ = nullptr;
		}

		if (this->triangle_mesh_)
		{
			common.destroyTriangleMesh(this->triangle_mesh_);
			this->triangle_mesh_ = nullptr;
		}
	});
}
