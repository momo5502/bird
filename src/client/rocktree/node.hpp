#pragma once

#include "rocktree_object.hpp"

#include "../mesh.hpp"

class bulk;

struct oriented_bounding_box
{
	glm::dvec3 center{};
	glm::dvec3 extents{};
	glm::dmat3 orientation{};
};

class physics_node
{
public:
	physics_node() = default;
	physics_node(rocktree& rocktree, const std::vector<mesh>& meshes, const glm::dmat4& world_matrix);
	~physics_node();

	physics_node(physics_node&&) = delete;
	physics_node(const physics_node&) = delete;
	physics_node& operator=(physics_node&&) = delete;
	physics_node& operator=(const physics_node&) = delete;

private:
	struct vertex
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
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

	rocktree* rocktree_{};
	std::vector<physics_mesh> meshes_{};

	reactphysics3d::TriangleMesh* triangle_mesh_{};
	reactphysics3d::ConcaveMeshShape* concave_shape_{};
	reactphysics3d::CollisionBody* body_{};
};

class node : public rocktree_object
{
public:
	node(rocktree& rocktree, const bulk& parent, uint32_t epoch, std::string path, texture_format format,
		std::optional<uint32_t> imagery_epoch, bool is_leaf);

	bool can_have_data{};
	float meters_per_texel{};
	oriented_bounding_box obb{};
	glm::dmat4 matrix_globe_from_mesh{};

	void buffer_meshes();
	bool is_buffered() const;
	bool is_buffering() const;
	bool mark_for_buffering();

	float draw(const shader_context& ctx, float current_time, const std::array<float, 8>& child_draw_time,
		const std::array<int, 8>& octant_mask);

	static void buffer_queue(std::queue<node*> nodes);

	uint64_t get_vertices() const
	{
		return this->vertices_;
	}

	std::vector<mesh> meshes{};

private:
	enum class buffer_state
	{
		unbuffered,
		buffering,
		buffered,
	};

	std::optional<float> draw_time_{};
	std::atomic<buffer_state> buffer_state_{ buffer_state::unbuffered };

	uint64_t vertices_{};
	uint32_t epoch_{};
	std::string path_{};

	texture_format format_{};
	std::optional<uint32_t> imagery_epoch_{};

	bool is_leaf_{};
	std::optional<physics_node> physics_node_{};


	std::string get_filename() const;
	std::string get_url() const override;
	std::filesystem::path get_filepath() const override;

	void populate(const std::optional<std::string>& data) override;
	void clear() override;
	bool can_be_deleted() const override;

	bool buffer_meshes_internal();
	void mark_as_buffered();
};
