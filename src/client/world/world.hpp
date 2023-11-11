#pragma once

#include "../gl_objects.hpp"
#include "../shader_context.hpp"

#include <utils/priority_mutex.hpp>

#include "reactphysics3d/engine/PhysicsWorld.h"

static reactphysics3d::PhysicsWorld::WorldSettings get_world_settings()
{
	reactphysics3d::PhysicsWorld::WorldSettings settings{};
	settings.defaultVelocitySolverNbIterations = 10;
	settings.defaultPositionSolverNbIterations = 5;
		return settings;
}

class world
{
public:
	world()
		: world_(common_.createPhysicsWorld(get_world_settings()))
	{
	}

	~world()
	{
		if (this->world_)
		{
			this->common_.destroyPhysicsWorld(this->world_);
			this->world_ = nullptr;
		}
	}

	template <typename F>
	void access_physics(F&& functor, const bool high_priority = false)
	{
		if (high_priority)
		{
			std::lock_guard _{this->phys_mutex_.high_priority()};
			functor(this->common_, *this->world_);
		}
		else
		{
			std::lock_guard _{this->phys_mutex_};
			functor(this->common_, *this->world_);
		}
	}

	gl_bufferer& get_bufferer()
	{
		return this->bufferer_;
	}

	const shader_context& get_shader_context() const
	{
		return this->context_;
	}

private:
	utils::priority_mutex phys_mutex_{};

	reactphysics3d::PhysicsCommon common_{};
	reactphysics3d::PhysicsWorld* world_{};

	shader_context context_{};
	gl_bufferer bufferer_{};
};
