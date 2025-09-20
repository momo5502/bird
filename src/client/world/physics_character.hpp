#pragma once
#include "physics_vector.hpp"

struct physics_character : JPH::Character
{
    using JPH::Character::Character;

    void set_supporting_volume(JPH::Plane plane)
    {
        this->mSupportingVolume = std::move(plane);
    }

    void update(const glm::dvec3& position, const glm::dvec3& orientation)
    {
        (void)orientation;
        const auto up = glm::normalize(position);
        const auto down = -up;

        constexpr auto normal_up = glm::dvec3(0.0, 1.0, 0.0);

        const auto axis = glm::cross(normal_up, down);
        const auto dotProduct = glm::dot(normal_up, down);
        const auto angle = acos(dotProduct);

        glm::quat rotationQuat = glm::angleAxis(angle, glm::normalize(axis));

        JPH::Quat quat{
            rotationQuat.x, //
            rotationQuat.y, //
            rotationQuat.z, //
            rotationQuat.w, //
        };

        const auto up_vector = v<JPH::Vec3>(up);
        const auto position_vector = v<JPH::DVec3>(position);

        this->set_supporting_volume(JPH::Plane(up_vector, -0.6f));
        this->SetPositionAndRotation(position_vector, quat.Normalized());
    }
};
