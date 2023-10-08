#pragma once

#include "task_manager.hpp"
#include "mesh.hpp"

class rocktree;

class rocktree_object
{
public:
	rocktree_object(rocktree& rocktree);
	virtual ~rocktree_object() = default;

	rocktree_object(rocktree_object&&) = delete;
	rocktree_object(const rocktree_object&) = delete;
	rocktree_object& operator=(rocktree_object&&) = delete;
	rocktree_object& operator=(const rocktree_object&) = delete;

	rocktree& get_rocktree() const
	{
		return *this->rocktree_;
	}

	const std::string& get_planet() const;

	bool is_fresh() const
	{
		return this->state_ == state::fresh;
	}

	bool is_ready() const
	{
		return this->state_ == state::ready;
	}

	bool is_fetching() const
	{
		return this->state_ == state::fetching;
	}

	bool can_be_used()
	{
		if (this->is_ready())
		{
			return true;
		}

		this->fetch();
		return false;
	}

	virtual bool can_be_removed() const
	{
		return !this->is_fetching();
	}

	void fetch();

protected:
	virtual void populate() = 0;

	virtual bool is_high_priority() const
	{
		return false;
	}

private:
	enum class state
	{
		fresh,
		fetching,
		ready,
		failed,
	};

	std::atomic<state> state_{state::fresh};
	rocktree* rocktree_{};
};

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

	std::vector<mesh> meshes{};

private:
	uint32_t epoch_{};
	std::string path_{};

	texture_format format_{};
	std::optional<uint32_t> imagery_epoch_{};

	void populate() override;
};

class bulk final : public rocktree_object
{
public:
	bulk(rocktree& rocktree, uint32_t epoch, std::string path = {});

	glm::dvec3 head_node_center{};
	std::map<std::string, std::unique_ptr<node>> nodes{};
	std::map<std::string, std::unique_ptr<bulk>> bulks{};

	bool can_be_removed() const override;

	const std::string& get_path() const;

private:
	uint32_t epoch_{};
	std::string path_{};

	void populate() override;
};

class planetoid final : public rocktree_object
{
public:
	using rocktree_object::rocktree_object;

	float radius{};
	std::unique_ptr<bulk> root_bulk{};

	bool can_be_removed() const override;

private:
	void populate() override;
};

class rocktree
{
public:
	friend rocktree_object;

	rocktree(std::string planet);

	const std::string& get_planet() const
	{
		return this->planet_;
	}

	planetoid* get_planetoid() const
	{
		return this->planetoid_.get();
	}

private:
	std::string planet_{};
	std::unique_ptr<planetoid> planetoid_{};

	task_manager task_manager_{};
};
