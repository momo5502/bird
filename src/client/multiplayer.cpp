#include "std_include.hpp"
#include "multiplayer.hpp"
#include <utils/byte_buffer.hpp>

namespace
{
	constexpr uint32_t PROTOCOL = 3;

	using vec3_t = std::array<double, 3>;
	using vec4_t = std::array<double, 4>;

	using name_t = std::array<char, 64>;

	struct player_state
	{
		vec3_t angles{};
		vec4_t position{};
		vec4_t velocity{};
		float speed;
		int32_t move_type{};
	};

	struct player_info
	{
		uint64_t guid{};
		name_t name{};
		player_state state{};
	};
}

multiplayer::multiplayer(network::address server)
	: identity_(utils::cryptography::ecc::generate_key(512))
	  , server_(std::move(server))
{
	this->manager_.on("states", [this](const network::address& address, const std::string_view& data)
	{
		this->receive_player_states(address, data);
	});

	this->manager_.on("authRequest", [this](const network::address& address, const std::string_view& data)
	{
		this->receive_auth_request(address, data);
	});
}

void multiplayer::transmit_position(const glm::dvec3& position, const glm::dvec3& orientation) const
{
	player_info player{};
	player.guid = this->identity_.get_hash();

	player.name[0] = 'a';
	player.name[1] = 0;

	player.state.position[0] = position[0];
	player.state.position[1] = position[1];
	player.state.position[2] = position[2];
	player.state.position[3] = 0;

	player.state.angles[0] = orientation[0];
	player.state.angles[1] = orientation[1];
	player.state.angles[2] = orientation[2];

	utils::buffer_serializer buffer{};
	buffer.write(PROTOCOL);
	buffer.write(player);

	this->manager_.send(this->server_, "state", buffer.get_buffer());
}

void multiplayer::access_players(const std::function<void(const players&)>& accessor) const
{
	this->players_.access(accessor);
}

size_t multiplayer::get_player_count() const
{
	return this->players_.access<size_t>([](const players& p)
	{
		return p.size();
	});
}

void multiplayer::receive_player_states(const network::address& address, const std::string_view& data)
{
	if (address != this->server_)
	{
		return;
	}

	utils::buffer_deserializer buffer(data);
	const auto protocol = buffer.read<uint32_t>();
	if (protocol != PROTOCOL)
	{
		return;
	}

	const auto own_guid = this->identity_.get_hash();
	const auto player_data = buffer.read_vector<player_info>();

	this->players_.access([&player_data, own_guid](players& players)
	{
		players.resize(0);
		players.reserve(player_data.size());

		for (const auto& player : player_data)
		{
			if (player.guid == own_guid)
			{
				continue;
			}

			::player p{};
			p.position = glm::dvec3(player.state.position[0], player.state.position[1], player.state.position[2]);
			p.orientation = glm::dvec3(player.state.angles[0], player.state.angles[1], player.state.angles[2]);

			players.emplace_back(std::move(p));
		}
	});
}

void multiplayer::receive_auth_request(const network::address& address, const std::string_view& data) const
{
	if (address != this->server_)
	{
		return;
	}

	utils::buffer_deserializer buffer(data);
	const auto protocol = buffer.read<uint32_t>();
	if (protocol != PROTOCOL)
	{
		return;
	}

	const auto nonce = buffer.read_string();

	const auto public_key = this->identity_.serialize(PK_PUBLIC);
	const auto signature = sign_message(this->identity_, nonce);

	utils::buffer_serializer response{};
	response.write(PROTOCOL);
	response.write_string(public_key);
	response.write_string(signature);

	(void)this->manager_.send(address, "authResponse", response.get_buffer());
}
