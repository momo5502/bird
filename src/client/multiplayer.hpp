#pragma once
#include <network/manager.hpp>
#include <utils/concurrency.hpp>
#include <utils/cryptography.hpp>
#include "world/physics_character.hpp"

inline network::address get_master_server()
{
    try
    {
        return {"server.momo5502.com:28960"};
    }
    catch (...)
    {
        return {};
    }
}

using player_guid = uint64_t;

class player
{
  public:
    friend class multiplayer;

    ~player();

    player_guid guid{};
    std::string name{};
    glm::dvec3 position{};
    glm::dvec3 orientation{};
    JPH::Body* character{};

  private:
    bool was_accessed{false};
    JPH::PhysicsSystem* physics_system{};
};

using players = std::map<player_guid, player>;

class multiplayer final
{
  public:
    multiplayer(JPH::PhysicsSystem& physics_system, network::address server = get_master_server());

    void transmit_position(const glm::dvec3& position, const glm::dvec3& orientation) const;
    void access_players(const std::function<void(const players&)>& accessor) const;
    size_t get_player_count() const;

    bool access_player_by_body_id(const JPH::BodyID& id, const std::function<void(player&)>& accessor);

    bool was_killed();

    void kill(const player& p) const;

    std::unique_lock<std::recursive_mutex> get_player_lock();

  private:
    bool was_killed_{false};
    JPH::PhysicsSystem* physics_system_{};

    utils::cryptography::ecc::key identity_{};
    utils::concurrency::container<players, std::recursive_mutex> players_{};

    network::address server_{};
    network::manager manager_{};

    void receive_player_states(const network::address& address, const std::string_view& data);
    void receive_killed_command(const network::address& address, const std::string_view& data);
    void receive_auth_request(const network::address& address, const std::string_view& data) const;
};
