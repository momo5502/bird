#pragma once

#include <iostream>

#include "../gl_objects.hpp"
#include "../shader_context.hpp"

static void TraceImpl(const char* in_fmt, ...)
{
	// Format the message
	va_list list;
	va_start(list, in_fmt);
	char buffer[1024]{0};
	vsnprintf(buffer, sizeof(buffer), in_fmt, list);
	va_end(list);

	// Print to the TTY
	std::cout << buffer << std::endl;
}

#ifdef JPH_ENABLE_ASSERTS

static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
{
	std::cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr ? inMessage : "") <<
		std::endl;
	return true;
}

#endif

struct physics_setup
{
	physics_setup()
	{
		JPH::RegisterDefaultAllocator();
		if (JPH::Factory::sInstance)
		{
			throw std::runtime_error("Physics already setup");
		}

		JPH::Trace = TraceImpl;
		JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

		JPH::Factory::sInstance = new JPH::Factory();

		JPH::RegisterTypes();
	}

	~physics_setup()
	{
		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
	}
};

namespace Layers
{
	static constexpr JPH::ObjectLayer NON_MOVING = 0;
	static constexpr JPH::ObjectLayer MOVING = 1;
	static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};

namespace BroadPhaseLayers
{
	static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
	static constexpr JPH::BroadPhaseLayer MOVING(1);
	static constexpr JPH::uint NUM_LAYERS(2);
}

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
	BPLayerInterfaceImpl()
	{
		// Create a mapping table from object to broad phase layer
		object_to_broad_phase_[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
		object_to_broad_phase_[Layers::MOVING] = BroadPhaseLayers::MOVING;
	}

	JPH::uint GetNumBroadPhaseLayers() const override
	{
		return BroadPhaseLayers::NUM_LAYERS;
	}

	JPH::BroadPhaseLayer GetBroadPhaseLayer(const JPH::ObjectLayer in_layer) const override
	{
		JPH_ASSERT(in_layer < Layers::NUM_LAYERS);
		return object_to_broad_phase_[in_layer];
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char* GetBroadPhaseLayerName(const JPH::BroadPhaseLayer inLayer) const override
	{
		switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer))
		{
		case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NON_MOVING):
			return "NON_MOVING";
		case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::MOVING):
			return "MOVING";
		default: JPH_ASSERT(false);
			return "INVALID";
		}
	}
#endif

private:
	JPH::BroadPhaseLayer object_to_broad_phase_[Layers::NUM_LAYERS];
};

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
	bool ShouldCollide(const JPH::ObjectLayer in_object1, const JPH::ObjectLayer in_object2) const override
	{
		switch (in_object1)
		{
		case Layers::NON_MOVING:
			return in_object2 == Layers::MOVING; // Non moving only collides with moving
		case Layers::MOVING:
			return true; // Moving collides with everything
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
	bool ShouldCollide(const JPH::ObjectLayer in_layer1, const JPH::BroadPhaseLayer in_layer2) const override
	{
		switch (in_layer1)
		{
		case Layers::NON_MOVING:
			return in_layer2 == BroadPhaseLayers::MOVING;
		case Layers::MOVING:
			return true;
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

class world
{
public:
	world()
		: temp_allocator_(10 * 1024 * 1024),
		  job_system_(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
		              static_cast<int>(std::thread::hardware_concurrency()) - 2)


	{
		physics_system_.Init(1024 * 100, 0, 1024, 1024, broad_phase_layer_interface_,
		                     object_vs_broadphase_layer_filter_,
		                     object_vs_object_layer_filter_);
	}

	~world() = default;

	gl_bufferer& get_bufferer()
	{
		return this->bufferer_;
	}

	const shader_context& get_shader_context() const
	{
		return this->context_;
	}

	JPH::PhysicsSystem& get_physics_system()
	{
		return this->physics_system_;
	}

	JPH::TempAllocatorImpl& get_temp_allocator()
	{
		return this->temp_allocator_;
	}

	JPH::JobSystemThreadPool& get_job_system()
	{
		return this->job_system_;
	}

private:
	physics_setup setup_{};
	JPH::TempAllocatorImpl temp_allocator_;
	JPH::JobSystemThreadPool job_system_;

	BPLayerInterfaceImpl broad_phase_layer_interface_;
	ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter_;
	ObjectLayerPairFilterImpl object_vs_object_layer_filter_;

	JPH::PhysicsSystem physics_system_;

	shader_context context_{};
	gl_bufferer bufferer_{};
};
