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
