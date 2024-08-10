#pragma once
#include <network/manager.hpp>
#include <utils/concurrency.hpp>
#include <utils/cryptography.hpp>

inline network::address get_master_server()
{
	return {"server.momo5502.com:28960"};
}

struct player
{
	glm::dvec3 position{};
	glm::dvec3 orientation{};
};

using players = std::vector<player>;

class multiplayer final
{
public:
	multiplayer(network::address server = get_master_server());

	void transmit_position(const glm::dvec3& position, const glm::dvec3& orientation) const;
	void access_players(const std::function<void(const players&)>& accessor) const;
	size_t get_player_count() const;

private:
	utils::cryptography::ecc::key identity_{};
	utils::concurrency::container<players> players_{};

	network::address server_{};
	network::manager manager_{};

	void receive_player_states(const network::address& address, const std::string_view& data);
	void receive_auth_request(const network::address& address, const std::string_view& data) const;
};
