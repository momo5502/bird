#pragma once

template <typename T>
T v(const glm::dvec3& vec)
{
    using Type = decltype(static_cast<T*>(nullptr)->GetX());

    return T(static_cast<Type>(vec.x), static_cast<Type>(vec.y), static_cast<Type>(vec.z));
}

inline glm::dvec3 v(const JPH::Vec3& vec)
{
    return {vec.GetX(), vec.GetY(), vec.GetZ()};
}

inline glm::dvec3 v(const JPH::RVec3& vec)
{
    return {vec.GetX(), vec.GetY(), vec.GetZ()};
}
