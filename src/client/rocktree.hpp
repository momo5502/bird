#pragma once

#include "task_manager.hpp"
#include "mesh.hpp"

#include <utils/http.hpp>

#include "rocktree/octant_identifier.hpp"
#include "rocktree/rocktree_object.hpp"

#include "gl_objects.hpp"

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

	rocktree* rocktree_{};
	std::vector<physics_mesh> meshes_{};

	reactphysics3d::TriangleMesh* triangle_mesh_{};
	reactphysics3d::ConcaveMeshShape* concave_shape_{};
	reactphysics3d::CollisionBody* body_{};
};

class node final : public rocktree_object
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
	std::atomic<buffer_state> buffer_state_{buffer_state::unbuffered};

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

class bulk final : public rocktree_object
{
public:
	bulk(rocktree& rocktree, const generic_object& parent, uint32_t epoch, std::string path = {});

	glm::dvec3 head_node_center{};
	std::map<octant_identifier<>, node*> nodes{};
	std::map<octant_identifier<>, bulk*> bulks{};

	const std::string& get_path() const;

private:
	uint32_t epoch_{};
	std::string path_{};

	bool is_high_priority() const override
	{
		return true;
	}

	std::string get_filename() const;
	std::string get_url() const override;
	std::filesystem::path get_filepath() const override;
	void populate(const std::optional<std::string>& data) override;
	void clear() override;
};

class planetoid final : public rocktree_object
{
public:
	planetoid(rocktree& rocktree)
		: rocktree_object(rocktree, nullptr)
	{
	}

	float radius{};
	bulk* root_bulk{};

private:
	bool is_high_priority() const override
	{
		return true;
	}

	std::string get_url() const override;
	std::filesystem::path get_filepath() const override;
	void populate(const std::optional<std::string>& data) override;
	void clear() override;
};

class rocktree
{
public:
	friend rocktree_object;

	rocktree(reactphysics3d::PhysicsCommon& common, reactphysics3d::PhysicsWorld& world, std::string planet);
	~rocktree();

	const std::string& get_planet() const
	{
		return this->planet_;
	}

	template <typename F>
	void access_physics(F&& functor, const bool high_priority = false)
	{
		if (high_priority)
		{
			std::lock_guard _{this->phys_mutex_.high_priority()};
			functor(*this->common_, *this->world_);
		}
		else
		{
			std::lock_guard _{this->phys_mutex_};
			functor(*this->common_, *this->world_);
		}
	}

	planetoid* get_planetoid() const
	{
		return this->planetoid_.get();
	}

	gl_bufferer& get_bufferer()
	{
		return this->bufferer_;
	}

	task_manager& get_task_manager()
	{
		return this->task_manager_;
	}

	void cleanup_dangling_objects(const std::chrono::milliseconds& timeout);

	size_t get_tasks() const;
	size_t get_tasks(size_t i) const;
	size_t get_downloads() const;
	size_t get_objects() const;

private:
	std::string planet_{};
	utils::priority_mutex phys_mutex_{};
	reactphysics3d::PhysicsCommon* common_{};
	reactphysics3d::PhysicsWorld* world_{};

	gl_bufferer bufferer_{};

	using object_list = std::list<std::unique_ptr<generic_object>>;
	utils::concurrency::container<object_list> objects_{};
	utils::concurrency::container<object_list> new_objects_{};
	object_list::iterator object_iterator_ = objects_.get_raw().end();

	std::unique_ptr<planetoid> planetoid_{};
	utils::http::downloader downloader_{};
	task_manager task_manager_{};

	void store_object(std::unique_ptr<generic_object> object);
};
