#pragma once

#include "rocktree_object.hpp"

#include "node.hpp"
#include "bulk.hpp"
#include "planetoid.hpp"

#include "../task_manager.hpp"
#include "../gl_objects.hpp"

#include <utils/http.hpp>
#include <utils/priority_mutex.hpp>

class planetoid;

template <typename RocktreeData>
class typed_rocktree;

class rocktree
{
public:
	friend rocktree_object;

	rocktree(reactphysics3d::PhysicsCommon& common, reactphysics3d::PhysicsWorld& world, std::string planet);
	virtual ~rocktree();

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

	virtual node* allocate_node(bulk& parent, static_node_data&& data)
	{
		auto obj = std::make_unique<node>(*this, parent, std::move(data));
		auto* ptr = obj.get();

		this->store_object(std::move(obj));

		return ptr;
	}

	void cleanup_dangling_objects(const std::chrono::milliseconds& timeout);

	size_t get_tasks() const;
	size_t get_tasks(size_t i) const;
	size_t get_downloads() const;
	size_t get_objects() const;

	template <typename RocktreeData>
	typed_rocktree<RocktreeData>& as()
	{
		return static_cast<typed_rocktree<RocktreeData>&>(*this);
	}

	template <typename RocktreeData>
	const typed_rocktree<RocktreeData>& as() const
	{
		return static_cast<const typed_rocktree<RocktreeData>&>(*this);
	}

	template <typename RocktreeData>
	RocktreeData& with()
	{
		return this->with<RocktreeData>().get();
	}

	template <typename RocktreeData>
	const RocktreeData& as() const
	{
		return this->with<RocktreeData>().get();
	}

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

protected:
	void store_object(std::unique_ptr<generic_object> object);
};

template <typename RocktreeData>
class typed_rocktree : public rocktree
{
public:
	typed_rocktree(reactphysics3d::PhysicsCommon& common, reactphysics3d::PhysicsWorld& world, std::string planet,
	               RocktreeData data)
		: rocktree(common, world, std::move(planet))
		  , data_(std::move(data))
	{
	}

	RocktreeData& get()
	{
		return this->data_;
	}

	const RocktreeData& get() const
	{
		return this->data_;
	}

private:
	RocktreeData data_{};
};

template <typename RocktreeData, typename NodeData>
class custom_rocktree : public typed_rocktree<RocktreeData>
{
public:
	using typed_rocktree<RocktreeData>::typed_rocktree;

	node* allocate_node(bulk& parent, static_node_data&& data) override
	{
		auto obj = std::make_unique<typed_node<NodeData>>(*this, parent, std::move(data));
		auto* ptr = obj.get();

		this->store_object(std::move(obj));

		return ptr;
	}
};
