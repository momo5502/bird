#pragma once

#include "task_manager.hpp"
#include "mesh.hpp"

#include <utils/http.hpp>

#include "rocktree/octant_identifier.hpp"
#include "rocktree/rocktree_object.hpp"

struct oriented_bounding_box
{
	glm::dvec3 center{};
	glm::dvec3 extents{};
	glm::dmat3 orientation{};
};

class node final : public rocktree_object
{
public:
	node(rocktree& rocktree, uint32_t epoch, std::string path, texture_format format,
	     std::optional<uint32_t> imagery_epoch);

	bool can_have_data{};
	float meters_per_texel{};
	oriented_bounding_box obb{};
	glm::dmat4 matrix_globe_from_mesh{};

	void buffer_meshes();
	bool is_buffered() const;

	void visit_children(const std::function<void(generic_object&)>& visitor) override;

	std::vector<mesh> meshes{};

private:
	std::atomic_bool buffered_{false};
	uint32_t epoch_{};
	std::string path_{};

	texture_format format_{};
	std::optional<uint32_t> imagery_epoch_{};

	std::string get_url() const override;
	void populate(const std::optional<std::string>& data) override;
	void clear() override;
};

class bulk final : public rocktree_object
{
public:
	bulk(rocktree& rocktree, uint32_t epoch, std::string path = {});

	glm::dvec3 head_node_center{};
	std::map<octant_identifier<>, std::unique_ptr<node>> nodes{};
	std::map<octant_identifier<>, std::unique_ptr<bulk>> bulks{};

	const std::string& get_path() const;

	void visit_children(const std::function<void(generic_object&)>& visitor) override;

private:
	uint32_t epoch_{};
	std::string path_{};

	bool is_high_priority() const override
	{
		return true;
	}

	std::string get_url() const override;
	void populate(const std::optional<std::string>& data) override;
	void clear() override;
};

class planetoid final : public rocktree_object
{
public:
	using rocktree_object::rocktree_object;

	void visit_children(const std::function<void(generic_object&)>& visitor) override;

	float radius{};
	std::unique_ptr<bulk> root_bulk{};

private:
	bool is_high_priority() const override
	{
		return true;
	}

	std::string get_url() const override;
	void populate(const std::optional<std::string>& data) override;
	void clear() override;
};

class rocktree
{
public:
	friend rocktree_object;

	rocktree(std::string planet);
	~rocktree();

	const std::string& get_planet() const
	{
		return this->planet_;
	}

	planetoid* get_planetoid() const
	{
		return this->planetoid_.get();
	}

	void cleanup_dangling_objects();

private:
	std::string planet_{};
	std::map<rocktree_object*, std::unique_ptr<rocktree_object>> objects_{};

	std::unique_ptr<planetoid> planetoid_{};

	utils::http::downloader downloader_{};
	std::jthread downloader_thread_{};

	task_manager task_manager_{};
};
