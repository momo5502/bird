#include "std_include.hpp"
#include "window.hpp"
#include "rocktree/rocktree.hpp"
#include "input.hpp"

#include "crosshair.hpp"
#include "text_renderer.hpp"

#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/thread.hpp>
#include <utils/concurrency.hpp>
#include <utils/finally.hpp>

#include <cmrc/cmrc.hpp>

#include "world/world.hpp"
#include "world/world_mesh.hpp"

#include "multiplayer.hpp"
#include "world/physics_vector.hpp"

CMRC_DECLARE(bird);

//#define USE_ADAPTIVE_RENDER_DISTANCE

namespace
{
	constexpr float ANIMATION_TIME = 350.0f;

	bool perform_object_cleanup(generic_object& obj)
	{
		if (obj.try_perform_deletion())
		{
			return true;
		}

		if (!obj.was_used_within(10s, 5s, 3s))
		{
			obj.mark_for_deletion();
			return true;
		}

		return false;
	}

	void perform_bulk_cleanup(bulk& current_bulk)
	{
		if (perform_object_cleanup(current_bulk) || !current_bulk.is_in_final_state())
		{
			return;
		}

		for (const auto& [_, node] : current_bulk.nodes)
		{
			perform_object_cleanup(*node);
		}

		for (const auto& [_, bulk] : current_bulk.bulks)
		{
			perform_bulk_cleanup(*bulk);
		}
	}

	void perform_cleanup(rocktree& rocktree, const bool clean)
	{
		if (clean)
		{
			profiler p("Clean");
			p.silence();

			const auto planetoid = rocktree.get_planetoid();
			if (!planetoid || !planetoid->is_in_final_state()) return;

			const auto& current_bulk = planetoid->root_bulk;
			if (!current_bulk || !current_bulk->is_in_final_state()) return;

			perform_bulk_cleanup(*current_bulk);
		}
		else
		{
			profiler p("Dangling");
			p.silence();

			rocktree.cleanup_dangling_objects(300ms);
		}
	}

	constexpr double A_EARTH = 6378.1370;
	constexpr double EARTH_ECC = 0.08181919084262157;
	constexpr double NAV_E2 = EARTH_ECC * EARTH_ECC;
	constexpr double deg2rad = glm::pi<double>() / 180.0;
	constexpr double rad2deg = 180.0 / glm::pi<double>();

	glm::dvec3 lla_to_ecef(const double latitude, const double longitude, const double altitude)
	{
		if ((latitude < -90.0) || (latitude > +90.0) || (longitude < -180.0) || (longitude > +360.0))
		{
			return {};
		}

		const double slat = sin(latitude * deg2rad);
		const double clat = cos(latitude * deg2rad);

		const double slon = sin(longitude * deg2rad);
		const double clon = cos(longitude * deg2rad);

		const double r_n = A_EARTH / sqrt(1.0 - NAV_E2 * slat * slat);
		const auto x = (r_n + altitude) * clat * clon;
		const auto y = (r_n + altitude) * clat * slon;
		const auto z = (r_n * (1.0 - NAV_E2) + altitude) * slat;

		return glm::dvec3{x, y, z};
	}

	glm::dvec3 ecef_to_lla(const glm::dvec3& ecef)
	{
		const double x = ecef.x;
		const double y = ecef.y;
		const double z = ecef.z;

		const double b = A_EARTH * sqrt(1.0 - NAV_E2);
		const double ep2 = (A_EARTH * A_EARTH - b * b) / (b * b);

		const double p = sqrt(x * x + y * y);
		const double theta = atan2(z * A_EARTH, p * b);

		double lon = atan2(y, x);
		double lat = atan2(z + ep2 * b * pow(sin(theta), 3),
		                   p - NAV_E2 * A_EARTH * pow(cos(theta), 3));

		const double r_n = A_EARTH / sqrt(1.0 - NAV_E2 * sin(lat) * sin(lat));
		const double alt = p / cos(lat) - r_n;

		// Convert from radians to degrees
		lat *= rad2deg;
		lon *= rad2deg;

		return glm::dvec3{lat, lon, alt};
	}

	void draw_sky(const double altitude)
	{
		constexpr auto up_limit = 500'000.0;
		constexpr auto low_limit = 10'000.0;

		auto middle = std::min(altitude, up_limit);
		middle = std::max(middle, low_limit);
		middle -= low_limit;

		constexpr auto diff = up_limit - low_limit;
		const auto dark_scale = middle / diff;
		const auto sky_scale = 1.0f - dark_scale;

		constexpr auto sky = 0x83b5fc;
		constexpr auto dark = 0x091321;

		constexpr auto sky_r = (sky >> 16 & 0xff) / 255.0;
		constexpr auto sky_g = (sky >> 8 & 0xff) / 255.0;
		constexpr auto sky_b = (sky & 0xff) / 255.0;

		constexpr auto dark_r = (dark >> 16 & 0xff) / 255.0;
		constexpr auto dark_g = (dark >> 8 & 0xff) / 255.0;
		constexpr auto dark_b = (dark & 0xff) / 255.0;

		const auto r = sky_r * sky_scale + dark_r * dark_scale;
		const auto g = sky_g * sky_scale + dark_g * dark_scale;
		const auto b = sky_b * sky_scale + dark_b * dark_scale;

		glClearColor(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	std::array<glm::dvec4, 6> get_frustum_planes(const glm::dmat4& projection)
	{
		std::array<glm::dvec4, 6> planes{};
		for (int i = 0; i < 3; ++i)
		{
			planes[i + 0] = glm::row(projection, 3) + glm::row(projection, i);
			planes[i + 3] = glm::row(projection, 3) - glm::row(projection, i);
		}

		return planes;
	}

	enum obb_frustum : int
	{
		obb_frustum_inside = -1,
		obb_frustum_intersect = 0,
		obb_frustum_outside = 1,
	};

	obb_frustum classify_obb_frustum(const oriented_bounding_box& obb, const std::array<glm::dvec4, 6>& planes)
	{
		auto result = obb_frustum_inside;
		const auto obb_orientation_t = glm::transpose(obb.orientation);

		for (int i = 0; i < 6; i++)
		{
			const auto& plane4 = planes[i];
			auto plane3 = glm::dvec3(plane4[0], plane4[1], plane4[2]);

			auto abs_plane = (obb_orientation_t * plane3);
			abs_plane[0] = std::abs(abs_plane[0]);
			abs_plane[1] = std::abs(abs_plane[1]);
			abs_plane[2] = std::abs(abs_plane[2]);

			const auto r = glm::dot(obb.extents, abs_plane);
			const auto d = glm::dot(obb.center, plane3) + plane4[3];

			if (fabs(d) < r) result = obb_frustum_intersect;
			if (d + r < 0.0f) return obb_frustum_outside;
		}
		return result;
	}

	void handle_input(JPH::Character* mCharacter, JPH::Vec3 inMovementDirection, const JPH::Vec3& up, const bool jump)
	{
		// Cancel movement in opposite direction of normal when touching something we can't walk up
		const JPH::Character::EGroundState ground_state = mCharacter->GetGroundState();
		static auto oldState = ground_state;
		if (oldState != ground_state)
		{
			printf("%s\n", JPH::CharacterBase::sToString(ground_state));
			oldState = ground_state;
		}

		if (ground_state == JPH::Character::EGroundState::OnSteepGround
			|| ground_state == JPH::Character::EGroundState::NotSupported)
		{
			JPH::Vec3 normal = mCharacter->GetGroundNormal();
			normal.SetY(0.0f);
			const float dot = normal.Dot(inMovementDirection);
			if (dot < 0.0f)
				inMovementDirection -= (dot * normal) / normal.LengthSq();
		}

		if (/*sControlMovementDuringJump ||*/ mCharacter->IsSupported())
		{
			constexpr float sCharacterSpeed = 6.0f;
			constexpr float sJumpSpeed = 6.0f;

			// Update velocity
			const JPH::Vec3 current_velocity = mCharacter->GetLinearVelocity();

			const auto up_magnitude = current_velocity.Dot(up);

			const JPH::Vec3 desired_velocity = sCharacterSpeed * inMovementDirection + (up_magnitude * up);
			JPH::Vec3 new_velocity = 0.75f * current_velocity + 0.25f * desired_velocity;

			// Jump
			if (jump && ground_state == JPH::Character::EGroundState::OnGround)
			{
				new_velocity += sJumpSpeed * up;
			}

			// Update the velocity
			mCharacter->SetLinearVelocity(new_velocity);
		}
	}

	glm::dvec3 align_vector(const glm::dvec3& source_vector, const glm::dvec3& target_vec)
	{
		const auto source = glm::normalize(source_vector);
		const auto magnitude = glm::dot(source, target_vec);

		return source * magnitude;
	}

	glm::dvec3 vector_forward(const glm::dvec3& vec, const glm::dvec3& up)
	{
		const auto unit_vector = glm::normalize(vec);
		const auto right_vector = glm::cross(unit_vector, up);
		const auto forward_vector = glm::cross(up, right_vector);
		return glm::normalize(forward_vector);
	}

	struct simulation_objects
	{
		window& win;
		rocktree& rock_tree;
		glm::dvec3 spawn_eye;
		glm::dvec3 spawn_direction;
		glm::dvec3& eye;
		glm::dvec3& direction;

		text_renderer& renderer;
		physics_character& character;
		input& input_handler;
		crosshair xhair{};
	};

	struct fps_context
	{
		std::atomic_uint64_t total_frame_counter{0};

		double last_frame_time{0};
		uint32_t frame_counter{0};
		int fps{60};
	};

	struct shooting_context
	{
		bool shot_requested{false};
		std::chrono::milliseconds cooldown{100ms};
		std::chrono::high_resolution_clock::time_point last_shot{};

		bool should_shoot_now()
		{
			if (!this->shot_requested)
			{
				return false;
			}

			this->shot_requested = false;

			const auto now = std::chrono::high_resolution_clock::now();
			const auto diff = now - this->last_shot;

			if (diff < this->cooldown)
			{
				return false;
			}

			this->last_shot = now;
			return true;
		}
	};

	struct rendering_context : simulation_objects, fps_context, shooting_context
	{
		utils::concurrency::container<std::queue<world_mesh*>> meshes_to_buffer{};
		bool gravity_on{true};
		double render_distance{1.2};
		uint64_t last_vertices{0};
		bool is_ready{false};
	};

	void update_fps(fps_context& c)
	{
		const auto current_frame_time = glfwGetTime();
		const auto time_diff = current_frame_time - c.last_frame_time;

		c.frame_counter++;

		if (time_diff >= 1.0 / 4)
		{
			c.fps = static_cast<int>((1.0 / time_diff) * c.frame_counter);
			c.last_frame_time = current_frame_time;
			c.frame_counter = 0;
		}
	}

	void draw_text(const rendering_context& c, world& game_world, const size_t buffer_queue,
	               const uint64_t current_vertices)
	{
		constexpr auto color = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);

		auto offset = 35.0f;

		c.renderer.draw("FPS: " + std::to_string(c.fps), 25.0f, (offset += 25.0f), 1.0f, color);
		c.renderer.draw("Tasks: " + std::to_string(c.rock_tree.get_tasks()), 25.0f, (offset += 25.0f), 1.0f, color);
		c.renderer.draw("Downloads: " + std::to_string(c.rock_tree.get_downloads()), 25.0f, (offset += 25.0f), 1.0f,
		                color);
		c.renderer.draw("Buffering: " + std::to_string(buffer_queue), 25.0f, (offset += 25.0f), 1.0f, color);
		c.renderer.draw("Objects: " + std::to_string(c.rock_tree.get_objects()), 25.0f, (offset += 25.0f), 1.0f, color);
		c.renderer.draw("Vertices: " + std::to_string(current_vertices), 25.0f, (offset += 25.0f), 1.0f, color);
		c.renderer.draw("Distance: " + std::to_string(c.render_distance), 25.0f, (offset += 25.0f), 1.0f, color);
		c.renderer.draw("Gravity: " + std::string(c.gravity_on ? "on" : "off"), 25.0f, (offset += 25.0f), 1.0f, color);
		c.renderer.draw("Players: " + std::to_string(game_world.get_multiplayer().get_player_count()), 25.0f,
		                (offset += 25.0f), 1.0f, color);
	}

	size_t push_meshes_for_buffering(rendering_context& c, std::queue<world_mesh*> new_meshes_to_buffer)
	{
		size_t buffer_queue{0};

		c.meshes_to_buffer.access([&](std::queue<::world_mesh*>& meshes)
		{
			buffer_queue = meshes.size() + new_meshes_to_buffer.size();

			if (meshes.empty())
			{
				meshes = std::move(new_meshes_to_buffer);
				return;
			}

			while (!new_meshes_to_buffer.empty())
			{
				auto* node = new_meshes_to_buffer.front();
				new_meshes_to_buffer.pop();

				meshes.push(node);
			}
		});

		return buffer_queue;
	}

	std::queue<world_mesh*> draw_world(profiler& p, const world& game_world, float current_time,
	                                   const glm::dmat4& viewprojection, uint64_t& current_vertices,
	                                   const std::map<octant_identifier<>, node*>& potential_nodes)
	{
		std::queue<world_mesh*> new_meshes_to_buffer{};

		using mask_list = std::array<int, 8>;
		using time_list = std::array<float, 8>;

		struct octant_mask
		{
			mask_list masks{};
			time_list times{};
		};

		// 8-bit octant mask flags of nodes
		std::unordered_map<octant_identifier<>, octant_mask> mask_map{};

		p.step("Loop 2");

		const auto& ctx = game_world.get_shader_context();
		const auto shader = ctx.use_shader();

		glUniform1f(ctx.animation_time_loc, ANIMATION_TIME);

		for (const auto& potential_node : std::ranges::reverse_view(potential_nodes))
		{
			// reverse order
			const auto& full_path = potential_node.first;
			auto* node = potential_node.second;
			const auto level = full_path.size();

			assert(level > 0);
			assert(node->can_have_data);

			auto& mesh = node->with<world_mesh>();

			if (!mesh.is_buffered())
			{
				if (mesh.mark_for_buffering())
				{
					new_meshes_to_buffer.push(&mesh);
				}

				continue;
			}

			// set octant mask of previous node
			const auto octant = full_path[level - 1];
			auto prev = full_path.substr(0, level - 1);

			auto& prev_entry = mask_map[prev];
			auto& mask_entry = prev_entry;

			mask_entry.masks[octant] = 1;

			const auto& mask = mask_map[full_path];

			bool must_draw = false;
			for (size_t i = 0; i < mask.masks.size() && i < mask.times.size() && !must_draw; ++i)
			{
				must_draw |= !mask.masks.at(i);
				must_draw |= (current_time - mask.times.at(i)) <= ANIMATION_TIME;
			}

			// skip if node is masked completely
			if (!must_draw) continue;

			const glm::mat4 transform = viewprojection * node->matrix_globe_from_mesh;
			const glm::mat4 worldmatrix = node->matrix_globe_from_mesh;

			glUniformMatrix4fv(ctx.transform_loc, 1, GL_FALSE, &transform[0][0]);
			glUniformMatrix4fv(ctx.worldmatrix_loc, 1, GL_FALSE, &worldmatrix[0][0]);

			p.step("Loop2Draw");

			mask_entry.times[octant] = mesh.draw(ctx, current_time, mask.times, mask.masks);
			current_vertices += node->get_vertices();

			p.step("Loop 2");
		}

		return new_meshes_to_buffer;
	}

	std::map<octant_identifier<>, node*> select_nodes(const rendering_context& c, const glm::dmat4& viewprojection,
	                                                  bulk* current_bulk)
	{
		std::map<octant_identifier<>, node*> potential_nodes{};
		std::queue<std::pair<octant_identifier<>, bulk*>> valid{};
		valid.emplace(octant_identifier{}, current_bulk);

		const auto frustum_planes = get_frustum_planes(viewprojection);

		auto lock = c.rock_tree.get_task_manager().lock_high_priority();

		while (!valid.empty())
		{
			const auto& entry = valid.front();

			const auto cur = entry.first;
			auto* bulk = entry.second;

			valid.pop();

			const auto cur_size = cur.size();
			if (cur_size > 0 && cur_size % 4 == 0)
			{
				auto rel = cur.substr(((cur_size - 1) / 4) * 4, 4);
				auto bulk_kv = bulk->bulks.find(rel);
				if (bulk_kv == bulk->bulks.end())
				{
					continue;
				}

				bulk = bulk_kv->second;
				if (!bulk->can_be_used())
				{
					continue;
				}
			}

			for (uint8_t o = 0; o < 8; ++o)
			{
				auto nxt = cur + o;
				auto nxt_rel = nxt.substr(((nxt.size() - 1) / 4) * 4, 4);
				auto node_kv = bulk->nodes.find(nxt_rel);
				if (node_kv == bulk->nodes.end())
				{
					continue;
				}

				auto* node = node_kv->second;

				// cull outside frustum using obb
				// TODO: check if it could cull more
				const auto is_visible = obb_frustum_outside != classify_obb_frustum(node->obb, frustum_planes);
				if (!is_visible && glm::distance2(node->obb.center, c.eye) > 10000)
				{
					continue;
				}

				{
					const auto vec = c.eye + glm::length(c.eye - node->obb.center) * c.direction;
					constexpr auto identity = glm::identity<glm::dmat4>();
					const auto t = glm::translate(identity, vec);

					auto m = viewprojection * t;
					const auto s = m[3][3];
					const auto texels_per_meter = 1.0f / node->meters_per_texel;
					constexpr auto wh = 768; //width < height ? width : height;
					const auto r = (c.render_distance * (1.0 / s)) * wh;
					if (texels_per_meter > r)
					{
						continue;
					}
				}

				if (node->can_be_used() && node->can_have_data && is_visible)
				{
					potential_nodes[nxt] = node;
				}

				valid.emplace(std::move(nxt), bulk);
			}
		}

		return potential_nodes;
	}

#ifdef USE_ADAPTIVE_RENDER_DISTANCE
	void update_render_distance(rendering_context& c)
	{
		constexpr auto min_render_distance = 1.0;
		constexpr auto max_render_distance = 2.0;

		constexpr auto max_vertices = 2'500'000ULL;
		constexpr auto min_change_vertices = 100'000ULL;


		if (c.last_vertices + min_change_vertices < max_vertices)
		{
			c.render_distance += 0.01;
		}

		if (c.last_vertices > max_vertices + min_change_vertices)
		{
			c.render_distance -= 0.01;
		}

		c.render_distance = std::clamp(c.render_distance, min_render_distance, max_render_distance);
	}
#endif

	class body_filter : public JPH::BodyFilter
	{
	public:
		body_filter(rendering_context& c)
			: c_(&c)
		{
		}

		bool ShouldCollide(const JPH::BodyID& inBodyID) const override
		{
			return c_->character.GetBodyID() != inBodyID;
		}

	private:
		rendering_context* c_{};
	};

	void shoot_bullet(rendering_context& c, world& game_world)
	{
		if (!c.should_shoot_now())
		{
			return;
		}

		auto& mp = game_world.get_multiplayer();
		const auto lock = mp.get_player_lock();

		const auto eye = c.eye;
		const auto dir = c.direction;

		JPH::RayCastResult result{};
		const JPH::RRayCast ray(v<JPH::DVec3>(eye), v<JPH::Vec3>(glm::normalize(dir) * 1000.0));

		const body_filter filter{c};
		const auto& narrowQuery = game_world.get_physics_system().GetNarrowPhaseQuery();

		if (narrowQuery.CastRay(ray, result, JPH::BroadPhaseLayerFilter{}, JPH::ObjectLayerFilter{}, filter))
		{
			mp.access_player_by_body_id(result.mBodyID, [&](const player& p)
			{
				static_assert(sizeof(p.guid) == sizeof(unsigned long long));
				printf("Hit player: %llX\n", static_cast<unsigned long long>(p.guid));
				mp.kill(p);
			});
		}
	}

	input_state handle_input(rendering_context& c)
	{
		const auto state = c.input_handler.get_input_state();
		c.shot_requested = state.shooting;

		if (state.gravity_toggle)
		{
			c.gravity_on = !c.gravity_on;
		}

		return state;
	}

	glm::dmat4 simulate(rendering_context& c, world& game_world, const input_state& state, const double altitude,
	                    const double planet_radius)
	{
		// up is the vec from the planetoid's center towards the sky
		const auto up = glm::normalize(c.eye);
		const auto down = -up;

		constexpr auto gravitational_force = 9.81;
		const auto gravity = down * gravitational_force;

		GLint viewport[4]{};
		glGetIntegerv(GL_VIEWPORT, viewport);

		const auto width = viewport[2];
		const auto height = viewport[3];

		const auto aspect_ratio = static_cast<double>(width) / static_cast<double>(height);
		constexpr auto fov = 0.25 * glm::pi<double>();

		const auto horizon = sqrt(altitude * (2 * planet_radius + altitude));
		auto near_val = 0.5;
		auto far_val = horizon;

		if (horizon > 370000)
		{
			near_val = altitude / 2;
		}

		if (near_val >= far_val) near_val = far_val - 1;
		if (std::isnan(far_val) || far_val < near_val) far_val = near_val + 1;

		const glm::dmat4 projection = glm::perspective(fov, aspect_ratio, near_val, far_val);

		// rotation
		double yaw = state.mouse_x * 0.005;
		double pitch = -state.mouse_y * 0.005;
		const auto overhead = glm::dot(c.direction, -up);

		if ((overhead > 0.99 && pitch < 0) || (overhead < -0.99 && pitch > 0))
		{
			pitch = 0;
		}

		auto pitch_axis = glm::cross(c.direction, up);
		auto yaw_axis = glm::cross(c.direction, pitch_axis);

		pitch_axis = glm::normalize(pitch_axis);
		const auto roll_angle = glm::angleAxis(0.0, glm::dvec3(0, 0, 1));
		const auto yaw_angle = glm::angleAxis(yaw, yaw_axis);
		const auto pitch_angle = glm::angleAxis(pitch, pitch_axis);
		auto rotation = roll_angle * yaw_angle * pitch_angle;
		c.direction = glm::normalize(rotation * c.direction);

		// movement
		auto speed_amp = fmin(2600, pow(fmax(0, (altitude - 500) / 10000) + 1, 1.337)) / 6;
		auto mag = 10 * (static_cast<double>(c.win.get_last_frame_time()) / 17000.0) * (1 + state.boost * 40) *
			speed_amp;
		auto sideways = glm::normalize(glm::cross(c.direction, up));
		auto forwards = c.direction * mag;
		auto backwards = -c.direction * mag;
		auto left = -sideways * mag;
		auto right = sideways * mag;

		const auto movement_vector = state.up * forwards
			+ state.down * backwards
			+ state.left * left
			+ state.right * right;

		auto new_eye = c.eye + movement_vector;
		auto pot_altitude = glm::length(new_eye) - planet_radius;
		bool can_change = pot_altitude < 1000 * 1000 * 10;
		const auto is_boosting = state.boost >= 0.01;

		auto velocity = movement_vector * gravitational_force;

		const auto movement_length = glm::length(movement_vector);
		const auto is_moving = movement_length > 0.0;

		auto& physics_system = game_world.get_physics_system();
		physics_system.SetGravity(v<JPH::Vec3>(gravity));

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

		c.character.SetUp(up_vector);
		c.character.set_supporting_volume(JPH::Plane(up_vector, -0.6f));
		c.character.SetRotation(quat.Normalized());

		const auto has_gravity = c.gravity_on && c.is_ready;

		if (can_change)
		{
			if (is_boosting || !has_gravity)
			{
				c.character.SetPosition(v<JPH::RVec3>(new_eye));
				c.character.SetLinearVelocity({});
			}
			else if (is_moving)
			{
				const auto forward_unit = vector_forward(movement_vector, up);
				velocity = align_vector(forward_unit, movement_vector);

				if (state.sprinting)
				{
					const auto view_forward = vector_forward(c.direction, up);
					const auto move_forward = align_vector(view_forward, velocity);

					const auto rest = velocity - move_forward;
					velocity = move_forward * 3.0 + rest * 1.5;
				}

				handle_input(&c.character, v<JPH::Vec3>(velocity), up_vector, state.jumping);
			}
		}

		shoot_bullet(c, game_world);

		if (has_gravity)
		{
			const auto time_delta = static_cast<double>(c.win.get_last_frame_time()) / (1000.0 * 1000.0);

			physics_system.Update(static_cast<float>(time_delta), 1,
			                      &game_world.get_temp_allocator(), &game_world.get_job_system());
			c.character.PostSimulation(0.05f);
		}

		const auto view = glm::lookAt(c.eye, c.eye + c.direction, up);
		const auto viewprojection = projection * view;
		return viewprojection;
	}

	void reset_viewport(const window& window)
	{
		int framebuffer_width{};
		int framebuffer_height{};

		glfwGetFramebufferSize(window, &framebuffer_width,
		                       &framebuffer_height);
		glViewport(0, 0, framebuffer_width, framebuffer_height);
	}


	bool has_meshes_to_buffer(rendering_context& c)
	{
		return c.meshes_to_buffer.access<bool>([](const std::queue<world_mesh*>& q)
		{
			return !q.empty();
		});
	}

	void run_frame(rendering_context& c, profiler& p)
	{
		++c.total_frame_counter;
		const auto current_time = static_cast<float>(c.win.get_current_time());

		uint64_t current_vertices = 0;
		const auto _ = utils::finally([&]
		{
			c.last_vertices = current_vertices;
		});

		if (!c.is_ready)
		{
			c.is_ready = c.total_frame_counter > 30 //
				&& c.rock_tree.get_tasks() == 0 //
				&& c.rock_tree.get_downloads() == 0 //
				&& c.rock_tree.get_objects() > 1 //
				&& !has_meshes_to_buffer(c);
		}

#ifdef USE_ADAPTIVE_RENDER_DISTANCE
		update_render_distance(c);
#endif

		p.step("Input");
		const auto state = handle_input(c);
		if (state.exit)
		{
			c.win.close();
			return;
		}

		p.step("Prepare");
		reset_viewport(c.win);

		auto& game_world = c.rock_tree.with<world>();

		const auto planetoid = c.rock_tree.get_planetoid();
		if (!planetoid || !planetoid->can_be_used()) return;

		auto* current_bulk = planetoid->root_bulk;
		if (!current_bulk || !current_bulk->can_be_used()) return;
		const double planet_radius = planetoid->radius;

		{
			const auto pos = c.character.GetPosition();
			c.eye = v(pos);
		}

		auto& mp = game_world.get_multiplayer();

		if (mp.was_killed())
		{
			c.eye = c.spawn_eye;
			c.direction = c.spawn_direction;
			c.character.SetPosition(v<JPH::RVec3>(c.eye));
			c.character.SetLinearVelocity({});
		}

		mp.transmit_position(c.eye, c.direction);

		const auto altitude = glm::length(c.eye) - planet_radius;

		p.step("Draw sky");
		draw_sky(altitude);

		p.step("Simulate");
		const auto viewprojection = simulate(c, game_world, state, altitude, planet_radius);

		p.step("Select nodes");
		const auto potential_nodes = select_nodes(c, viewprojection, current_bulk);

		p.step("Render");
		auto new_meshes_to_buffer = draw_world(p, game_world, current_time, viewprojection, current_vertices,
		                                       potential_nodes);

		game_world.get_multiplayer().access_players([&](const players& players)
		{
			for (const auto& [_, player] : players)
			{
				game_world.get_player_mesh().
				           draw(viewprojection, player.position, player.orientation);
			}
		});

		p.step("Push buffer");
		const auto buffer_queue = push_meshes_for_buffering(c, std::move(new_meshes_to_buffer));

		c.xhair.draw();

		p.step("Draw Text");
		update_fps(c);
		draw_text(c, game_world, buffer_queue, current_vertices);
	}

#ifdef _WIN32
	void trigger_high_performance_gpu_switch()
	{
		const auto key = utils::nt::open_or_create_registry_key(
			HKEY_CURRENT_USER, R"(Software\Microsoft\DirectX\UserGpuPreferences)");
		if (!key)
		{
			return;
		}

		const auto self = utils::nt::library::get_by_address(
			reinterpret_cast<const void*>(&trigger_high_performance_gpu_switch));
		const auto path = self.get_path().make_preferred().wstring();

		if (RegQueryValueExW(key, path.data(), nullptr, nullptr, nullptr, nullptr) != ERROR_FILE_NOT_FOUND)
		{
			return;
		}

		const std::wstring data = L"GpuPreference=2;";
		RegSetValueExW(key, self.get_path().make_preferred().wstring().data(), 0, REG_SZ,
		               reinterpret_cast<const BYTE*>(data.data()),
		               static_cast<DWORD>((data.size() + 1u) * 2));
	}
#endif

	bool buffer_queue(utils::concurrency::container<std::queue<world_mesh*>>& meshes_to_buffer)
	{
		std::queue<world_mesh*> mesh_queue{};

		meshes_to_buffer.access([&mesh_queue](std::queue<world_mesh*>& nodes)
		{
			if (nodes.empty())
			{
				return;
			}

			mesh_queue = std::move(nodes);
			nodes = {};
		});

		if (mesh_queue.empty())
		{
			return false;
		}

		world_mesh::buffer_queue(mesh_queue);
		return true;
	}

	void bufferer(rendering_context& c, const utils::thread::stop_token& token)
	{
		c.win.use_shared_context([&]
		{
			bool clean = false;
			auto last_cleanup_frame = c.total_frame_counter.load();
			while (!token.stop_requested())
			{
				c.rock_tree.with<world>().get_bufferer().perform_cleanup();

				if (c.total_frame_counter > (last_cleanup_frame + 6))
				{
					clean = !clean;
					perform_cleanup(c.rock_tree, clean);
					last_cleanup_frame = c.total_frame_counter.load();
				}

				if (!buffer_queue(c.meshes_to_buffer))
				{
					std::this_thread::sleep_for(10ms);
				}
			}
		});
	}

	text_renderer create_text_renderer()
	{
		const auto fs = cmrc::bird::get_filesystem();
		const auto opensans = fs.open("resources/font/OpenSans-Regular.ttf");

		return {{opensans.cbegin(), opensans.cend()}, 24};
	}

	void run()
	{
#ifdef _WIN32
		if (utils::nt::is_wine())
		{
			ShowWindow(GetConsoleWindow(), SW_HIDE);
		}

		trigger_high_performance_gpu_switch();
#endif

		utils::thread::set_name("Main");
		utils::thread::set_priority(utils::thread::priority::high);

		window win(1280, 800, "Bird");
		input input_handler(win);

		world game_world{};
		custom_rocktree<world, world_mesh> rock_tree{"earth", game_world};

		auto eye = lla_to_ecef(48.8605, 2.2914, 6364690.0);
		glm::dvec3 direction{0.374077, 0.71839, -0.5865};

		constexpr float cCharacterHeightStanding = 1.0f;
		constexpr float cCharacterRadiusStanding = 0.6f;

		const auto standingShape = JPH::RotatedTranslatedShapeSettings(
			JPH::Vec3(0, 0.5f * cCharacterHeightStanding + cCharacterRadiusStanding, 0), JPH::Quat::sIdentity(),
			new JPH::CapsuleShape(0.5f * cCharacterHeightStanding, cCharacterRadiusStanding)).Create();

		JPH::CharacterSettings character_settings{};
		character_settings.mLayer = Layers::MOVING;
		character_settings.mMaxSlopeAngle = JPH::DegreesToRadians(45.0f);
		character_settings.mShape = standingShape.Get();
		character_settings.mFriction = 10.0f;
		character_settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -cCharacterRadiusStanding);

		physics_character character(&character_settings, v<JPH::RVec3>(eye), JPH::Quat::sIdentity(), 0,
		                            &game_world.get_physics_system());

		character.AddToPhysicsSystem(JPH::EActivation::Activate);

		auto text_renderer = create_text_renderer();

		rendering_context context{
			win, rock_tree, eye, direction, eye, direction, text_renderer, character, input_handler,
		};

		const auto buffer_thread = utils::thread::create_named_jthread(
			"Bufferer", [&](const utils::thread::stop_token& token)
			{
				bufferer(context, token);
			});

		win.show([&](profiler& p)
		{
			p.silence();
			run_frame(context, p);
		});

		puts("Terminating game...");
		const auto lla = ecef_to_lla(context.eye);
		printf("LLA: %g, %g, %g\n", lla.x, lla.y, lla.z);
		printf("Position: %g, %g, %g\n", context.eye.x, context.eye.y, context.eye.z);
		printf("Orientation: %g, %g, %g\n", context.direction.x, context.direction.y, context.direction.z);

		character.RemoveFromPhysicsSystem();
	}
}

int main(int /*argc*/, char** /*argv*/)
{
	try
	{
		run();
		return 0;
	}
	catch (std::exception& e)
	{
		puts(e.what());

#ifdef _WIN32
		MessageBoxA(nullptr, e.what(), "ERROR", MB_ICONERROR);
#endif
	}

	return 1;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
	return main(__argc, __argv);
}
#endif
