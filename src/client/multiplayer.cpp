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

	void update_body(JPH::PhysicsSystem& system, const JPH::Body& body, const glm::dvec3& position,
	                 const glm::dvec3& orientation)
	{
		(void)orientation;
		const auto up = glm::normalize(position);
		const auto down = -up;

		constexpr auto normal_up = glm::dvec3(0.0, 1.0, 0.0);

		const auto axis = glm::cross(normal_up, down);
		const auto dotProduct = glm::dot(normal_up, down);
		const auto angle = acos(dotProduct);

		const glm::quat rotationQuat = glm::angleAxis(angle, glm::normalize(axis));

		const JPH::Quat quat{
			rotationQuat.x, //
			rotationQuat.y, //
			rotationQuat.z, //
			rotationQuat.w, //
		};

		const auto position_vector = v<JPH::DVec3>(position);

		system.GetBodyInterface().SetPositionAndRotation(body.GetID(), position_vector, quat.Normalized(),
		                                                 JPH::EActivation::Activate);
	}

	JPH::Body* create_body(JPH::PhysicsSystem& system)
	{
		constexpr float cCharacterHeightStanding = 1.0f;
		constexpr float cCharacterRadiusStanding = 0.6f;

		const auto standingShape = JPH::RotatedTranslatedShapeSettings(
			JPH::Vec3(0, 0.5f * cCharacterHeightStanding + cCharacterRadiusStanding, 0), JPH::Quat::sIdentity(),
			new JPH::CapsuleShape(0.5f * cCharacterHeightStanding, cCharacterRadiusStanding)).Create();

		auto& body_interface = system.GetBodyInterface();

		const JPH::BodyCreationSettings body_settings(standingShape.Get(), JPH::DVec3{},
		                                              JPH::Quat::sIdentity(),
		                                              JPH::EMotionType::Static, Layers::NON_MOVING);

		auto* body = body_interface.CreateBody(body_settings);
		assert(body);

		body_interface.AddBody(body->GetID(), JPH::EActivation::DontActivate);

		return body;
	}
}

player::~player()
{
	if (this->character && this->physics_system)
	{
		auto& body_interface = this->physics_system->GetBodyInterface();

		body_interface.RemoveBody(this->character->GetID());
		body_interface.DestroyBody(this->character->GetID());
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

bool multiplayer::access_player_by_body_id(const JPH::BodyID& id, const std::function<void(player&)>& accessor)
{
	return this->players_.access<bool>([&](players& p)
	{
		for (auto& [_, player] : p)
		{
			if (player.character->GetID() == id)
			{
				accessor(player);
				return true;
			}
		}

		return false;
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
				entry.physics_system = this->physics_system_;
				entry.character = create_body(*entry.physics_system);

				entry.guid = player.guid;
				entry.name.assign(player.name.data(), strnlen(player.name.data(), player.name.size()));
			}

			entry.was_accessed = true;
			entry.position = glm::dvec3(player.state.position[0], player.state.position[1], player.state.position[2]);
			entry.orientation = glm::dvec3(player.state.angles[0], player.state.angles[1], player.state.angles[2]);

			update_body(*this->physics_system_, *entry.character, entry.position, entry.orientation);
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
