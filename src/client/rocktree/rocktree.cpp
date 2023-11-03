#include "../std_include.hpp"

#include "rocktree.hpp"
#include "planetoid.hpp"

#include "rocktree_proto.hpp"

#include <utils/http.hpp>
#include <utils/timer.hpp>
#include <utils/finally.hpp>

rocktree::rocktree(reactphysics3d::PhysicsCommon& common, reactphysics3d::PhysicsWorld& world, std::string planet)
	: planet_(std::move(planet))
	  , common_(&common)
	  , world_(&world)
{
	this->planetoid_ = std::make_unique<planetoid>(*this);
}

rocktree::~rocktree()
{
	this->downloader_.stop();
	this->task_manager_.stop();
}

void rocktree::cleanup_dangling_objects(const std::chrono::milliseconds& timeout)
{
	object_list new_objects{};
	this->new_objects_.access([&new_objects](object_list& objects)
	{
		if (!objects.empty())
		{
			new_objects = std::move(objects);
			objects = object_list();
		}
	});

	this->objects_.access([&](object_list& objects)
	{
		const utils::timer timer{};

		if (this->object_iterator_ == objects.end())
		{
			this->object_iterator_ = objects.begin();
		}

		if (!new_objects.empty())
		{
			objects.splice(objects.end(), new_objects);
		}

		while (this->object_iterator_ != objects.end())
		{
			if (timer.has_elapsed(timeout))
			{
				return;
			}

			auto& object = **this->object_iterator_;

			const auto is_unused = !object.has_parent();
			const auto is_final = object.is_in_final_state();

			if (is_unused && is_final)
			{
				this->object_iterator_ = objects.erase(this->object_iterator_);
			}
			else
			{
				if (is_unused)
				{
					object.mark_for_deletion();
				}

				++this->object_iterator_;
			}
		}
	});
}

size_t rocktree::get_tasks() const
{
	return this->task_manager_.get_tasks();
}

size_t rocktree::get_tasks(const size_t i) const
{
	return this->task_manager_.get_tasks(i);
}

size_t rocktree::get_downloads() const
{
	return this->downloader_.get_downloads();
}

size_t rocktree::get_objects() const
{
	return this->objects_.get_raw().size();
}

void rocktree::store_object(std::unique_ptr<generic_object> object)
{
	this->new_objects_.access([&](object_list& list)
	{
		list.push_back(std::move(object));
	});
}
