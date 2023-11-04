#pragma once

#include "octant_identifier.hpp"
#include "rocktree_object.hpp"

#include "../mesh.hpp"

class bulk;

struct oriented_bounding_box
{
	glm::dvec3 center{};
	glm::dvec3 extents{};
	glm::dmat3 orientation{};
};

class node_data;

template <typename NodeData>
class typed_node;

struct static_node_data
{
	uint32_t epoch{};
	octant_identifier<> path{};
	texture_format format{};
	std::optional<uint32_t> imagery_epoch{};
	bool is_leaf{};
};

class node : public rocktree_object
{
public:
	friend node_data;

	node(rocktree& rocktree, const bulk& parent, static_node_data&& sdata);

	bool can_have_data{};
	float meters_per_texel{};
	oriented_bounding_box obb{};
	glm::dmat4 matrix_globe_from_mesh{};

	static_node_data sdata_{};

	uint64_t vertices_{};
	std::vector<mesh_data> meshes_{};

	uint64_t get_vertices() const
	{
		return this->vertices_;
	}

	template <typename NodeData>
	typed_node<NodeData>& as()
	{
		return static_cast<typed_node<NodeData>&>(*this);
	}

	template <typename NodeData>
	const typed_node<NodeData>& as() const
	{
		return static_cast<const typed_node<NodeData>&>(*this);
	}

	template <typename NodeData>
	NodeData& with()
	{
		return this->as<NodeData>().get();
	}

	template <typename NodeData>
	const NodeData& as() const
	{
		return this->as<NodeData>().get();
	}

private:
	std::string get_filename() const;
	std::string get_url() const override;
	std::filesystem::path get_filepath() const override;

protected:
	void populate(const std::optional<std::string>& data) override;
	void clear() override;
};

class node_data
{
public:
	node_data(node& node)
		: node_(&node)
	{
	}

	node_data(node_data&&) = delete;
	node_data(const node_data&) = delete;
	node_data& operator=(node_data&&) = delete;
	node_data& operator=(const node_data&) = delete;

	virtual ~node_data() = default;

	node& get_node()
	{
		return *this->node_;
	}

	const node& get_node() const
	{
		return *this->node_;
	}

	virtual bool can_be_deleted() const
	{
		return true;
	}

private:
	node* node_{};
};

template <typename NodeData>
class typed_node : public node
{
public:
	static_assert(std::is_base_of_v<node_data, NodeData>);

	using node::node;

	NodeData& get()
	{
		if (!this->data_)
		{
			throw std::runtime_error("Node not ready!");
		}

		return *this->data_;
	}

	const NodeData& get() const
	{
		if (!this->data_)
		{
			throw std::runtime_error("Node not ready!");
		}

		return *this->data_;
	}

private:
	std::unique_ptr<NodeData> data_{};

	void populate(const std::optional<std::string>& data) override
	{
		node::populate(data);
		this->data_ = std::make_unique<NodeData>(*this);
	}

	void clear() override
	{
		this->data_ = {};
		node::clear();
	}

	bool can_be_deleted() const override
	{
		return node::can_be_deleted() && //
			(!this->data_ || static_cast<const node_data&>(*this->data_).can_be_deleted());
	}
};
