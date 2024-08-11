#include "std_include.hpp"
#include "multiplayer.hpp"

#include <utils/byte_buffer.hpp>

#include "world/world.hpp"
#include "world/physics_vector.hpp"

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

player::~player()
{
	if (this->character)
	{
		this->character->RemoveFromPhysicsSystem();
	}
}

multiplayer::multiplayer(JPH::PhysicsSystem& physics_system, network::address server)
	: physics_system_(&physics_system)
	  , identity_(utils::cryptography::ecc::generate_key(512))
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

void multiplayer::access_player_by_body_id(const JPH::BodyID& id, const std::function<void(player&)>& accessor)
{
	this->players_.access([&](players& p)
	{
		for (auto& [_, player] : p)
		{
			if (player.character->GetBodyID() == id)
			{
				accessor(player);
				break;
			}
		}
	});
}

void multiplayer::shift_positions_relative_to(const glm::dvec3& origin)
{
	const auto orig = v<JPH::RVec3>(origin);

	this->players_.access([&orig](players& p)
	{
		for (auto& [_, player] : p)
		{
			const auto pos = player.character->GetPosition();
			const auto new_pos = pos - orig;

			player.character->SetPosition(new_pos);
		}
	});
}

void multiplayer::reset_player_positions()
{
	this->players_.access([](players& p)
	{
		for (auto& [_, player] : p)
		{
			player.character->update(player.position, player.orientation);
		}
	});
}

std::unique_lock<std::recursive_mutex> multiplayer::get_player_lock()
{
	return this->players_.acquire_lock();
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

	this->players_.access([&](players& players)
	{
		for (const auto& player : player_data)
		{
			if (player.guid == own_guid)
			{
				continue;
			}

			auto& entry = players[player.guid];
			if (!entry.character)
			{
				constexpr float cCharacterHeightStanding = 1.0f;
				constexpr float cCharacterRadiusStanding = 0.6f;

				auto standingShape = JPH::RotatedTranslatedShapeSettings(
					JPH::Vec3(0, 0.5f * cCharacterHeightStanding + cCharacterRadiusStanding, 0), JPH::Quat::sIdentity(),
					new JPH::CapsuleShape(0.5f * cCharacterHeightStanding, cCharacterRadiusStanding)).Create();

				JPH::CharacterSettings character_settings{};
				character_settings.mLayer = Layers::NON_MOVING;
				character_settings.mMaxSlopeAngle = JPH::DegreesToRadians(45.0f);
				character_settings.mShape = standingShape.Get();
				character_settings.mFriction = 10.0f;
				character_settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -cCharacterRadiusStanding);

				entry.character = std::make_unique<physics_character>(&character_settings, JPH::DVec3{},
				                                                      JPH::Quat::sIdentity(),
				                                                      0, this->physics_system_);

				entry.character->AddToPhysicsSystem(JPH::EActivation::Activate);

				entry.guid = player.guid;
				entry.name.assign(player.name.data(), strnlen(player.name.data(), player.name.size()));
			}

			entry.was_accessed = true;
			entry.position = glm::dvec3(player.state.position[0], player.state.position[1], player.state.position[2]);
			entry.orientation = glm::dvec3(player.state.angles[0], player.state.angles[1], player.state.angles[2]);

			entry.character->update(entry.position, entry.orientation);
		}

		for (auto i = players.begin(); i != players.end();)
		{
			auto was_accessed = false;
			std::swap(was_accessed, i->second.was_accessed);

			if (was_accessed)
			{
				++i;
			}
			else
			{
				i = players.erase(i);
			}
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
