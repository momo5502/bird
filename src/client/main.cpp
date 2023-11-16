#include "std_include.hpp"
#include "window.hpp"
#include "rocktree/rocktree.hpp"
#include "input.hpp"

#include "text_renderer.hpp"

#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/thread.hpp>
#include <utils/concurrency.hpp>
#include <utils/finally.hpp>

#include <cmrc/cmrc.hpp>

#include "world/world.hpp"
#include "world/world_mesh.hpp"

CMRC_DECLARE(bird);

//#define USE_ADAPTIVE_RENDER_DISTANCE

namespace
{
	constexpr float ANIMATION_TIME = 350.0f;

	glm::dvec3 v(const reactphysics3d::Vector3& vector)
	{
		return {vector.x, vector.y, vector.z};
	}

	reactphysics3d::Vector3 v(const glm::dvec3& vector)
	{
		return {vector.x, vector.y, vector.z};
	}

	bool perform_object_cleanup(generic_object& obj)
	{
		if (obj.try_perform_deletion())
		{
			return true;
		}

		if (!obj.was_used_within(10s, 6s, 2s))
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

		for (auto* node : current_bulk.nodes | std::views::values)
		{
			perform_object_cleanup(*node);
		}

		for (auto* bulk : current_bulk.bulks | std::views::values)
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

	glm::dvec3 lla_to_ecef(const double latitude, const double longitude, const double altitude)
	{
		if ((latitude < -90.0) || (latitude > +90.0) || (longitude < -180.0) || (longitude > +360.0))
		{
			return {};
		}

		constexpr double A_EARTH = 6378.1370;
		constexpr double EARTH_ECC = 0.08181919084262157;
		constexpr double NAV_E2 = EARTH_ECC * EARTH_ECC;
		constexpr double deg2rad = glm::pi<double>() / 180.0;

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

	void paint_sky(const double altitude)
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

	std::atomic_uint64_t frame_counter{0};

	void run_frame(profiler& p, window& window, rocktree& rocktree, glm::dvec3& eye, glm::dvec3& direction,
	               utils::concurrency::container<std::queue<world_mesh*>>& meshes_to_buffer, text_renderer& renderer,
	               reactphysics3d::RigidBody* camera)
	{
		++frame_counter;

		static double RENDER_DISTANCE = 1.4;

		uint64_t current_vertices = 0;

#ifdef USE_ADAPTIVE_RENDER_DISTANCE
		static uint64_t last_vertices = 0;
		constexpr auto min_render_distance = 1.0;
		constexpr auto max_render_distance = 2.0;

		constexpr auto max_vertices = 2'500'000ULL;
		constexpr auto min_change_vertices = 100'000ULL;

		const auto _ = utils::finally([&]
		{
			last_vertices = current_vertices;
		});

		if (RENDER_DISTANCE < max_render_distance && last_vertices + min_change_vertices < max_vertices)
		{
			RENDER_DISTANCE += 0.01;
		}

		if (last_vertices > max_vertices + min_change_vertices)
		{
			RENDER_DISTANCE -= 0.01;
		}

		if (RENDER_DISTANCE < min_render_distance)
		{
			RENDER_DISTANCE = min_render_distance;
		}
#endif

		const auto current_time = static_cast<float>(window.get_current_time());

		p.step("Input");

		const auto state = get_input_state(window);
		if (state.exit)
		{
			window.close();
			return;
		}

		p.step("Prepare");

		const auto planetoid = rocktree.get_planetoid();
		if (!planetoid || !planetoid->can_be_used()) return;

		auto* current_bulk = planetoid->root_bulk;
		if (!current_bulk || !current_bulk->can_be_used()) return;
		const auto planet_radius = planetoid->radius;
		constexpr auto gravitational_force = 9.81;

		p.step("Calculate");

		GLint viewport[4]{};
		glGetIntegerv(GL_VIEWPORT, viewport);

		const auto width = viewport[2];
		const auto height = viewport[3];

		// up is the vec from the planetoid's center towards the sky
		const auto up = glm::normalize(eye);
		const auto down = -up;
		const auto gravity = down * gravitational_force;

		// projection
		const auto aspect_ratio = static_cast<double>(width) / static_cast<double>(height);
		constexpr auto fov = 0.25 * glm::pi<double>();
		const auto altitude = glm::length(eye) - planet_radius;

		paint_sky(altitude);

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
		const auto overhead = glm::dot(direction, -up);

		if ((overhead > 0.99 && pitch < 0) || (overhead < -0.99 && pitch > 0))
		{
			pitch = 0;
		}

		auto pitch_axis = glm::cross(direction, up);
		auto yaw_axis = glm::cross(direction, pitch_axis);

		pitch_axis = glm::normalize(pitch_axis);
		const auto roll_angle = glm::angleAxis(0.0, glm::dvec3(0, 0, 1));
		const auto yaw_angle = glm::angleAxis(yaw, yaw_axis);
		const auto pitch_angle = glm::angleAxis(pitch, pitch_axis);
		auto rotation = roll_angle * yaw_angle * pitch_angle;
		direction = glm::normalize(rotation * direction);

		// movement
		auto speed_amp = fmin(2600, pow(fmax(0, (altitude - 500) / 10000) + 1, 1.337)) / 6;
		auto mag = 10 * (static_cast<double>(window.get_last_frame_time()) / 17000.0) * (1 + state.boost * 40) *
			speed_amp;
		auto sideways = glm::normalize(glm::cross(direction, up));
		auto forwards = direction * mag;
		auto backwards = -direction * mag;
		auto left = -sideways * mag;
		auto right = sideways * mag;

		const auto movement_vector = state.up * forwards
			+ state.down * backwards
			+ state.left * left
			+ state.right * right;

		auto pos = camera->getTransform().getPosition();
		eye = glm::dvec3(pos.x, pos.y, pos.z);


		auto new_eye = eye + movement_vector;
		auto pot_altitude = glm::length(new_eye) - planet_radius;
		bool can_change = pot_altitude < 1000 * 1000 * 10;

		auto& game_world = rocktree.with<world>();

		const auto can_fly = state.boost >= 0.1;
		auto velocity = movement_vector * gravitational_force * 1000.0;

		const auto movement_length = glm::length(movement_vector);
		const auto is_moving = movement_length > 0.0;


		if (!can_fly)
		{
			if (is_moving)
			{
				const auto direction_vector = glm::normalize(movement_vector);
				const auto right_vector = glm::cross(direction_vector, up);
				const auto forward_vector = glm::cross(up, right_vector);
				const auto forward_unit = glm::normalize(forward_vector);
				const auto forward_length = glm::dot(direction_vector, forward_unit);
				velocity = forward_unit * forward_length;
			}
		}

		game_world.access_physics([&](reactphysics3d::PhysicsCommon&, reactphysics3d::PhysicsWorld& world)
		{
			camera->enableGravity(!can_fly);

			if (can_change && is_moving)
			{
				camera->applyWorldForceAtCenterOfMass(v(velocity));
			}

			world.setGravity({gravity[0], gravity[1], gravity[2]});
			world.update(static_cast<double>(window.get_last_frame_time()) / 1'000'000.0);

			pos = camera->getTransform().getPosition();
			eye = v(pos);
		}, true);

		/*if (!c.did_hit)
		{
			eye = new_eye;
		}*/

		const auto view = glm::lookAt(eye, eye + direction, up);
		const auto viewprojection = projection * view;

		auto frustum_planes = get_frustum_planes(viewprojection);

		std::queue<std::pair<octant_identifier<>, bulk*>> valid{};
		valid.emplace(octant_identifier{}, current_bulk);

		std::map<octant_identifier<>, node*> potential_nodes;

		p.step("Loop1");

		auto lock = rocktree.get_task_manager().lock_high_priority();

		while (!valid.empty())
		{
			auto& entry = valid.front();

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
				// todo: check if it could cull more
				bool is_visible = obb_frustum_outside != classify_obb_frustum(node->obb, frustum_planes);
				if (!is_visible && glm::distance2(node->obb.center, eye) > 10000)
				{
					continue;
				}

				{
					const auto vec = eye + glm::length(eye - node->obb.center) * direction;
					constexpr auto identity = glm::identity<glm::dmat4>();
					const auto t = glm::translate(identity, vec);

					auto m = viewprojection * t;
					auto s = m[3][3];
					auto texels_per_meter = 1.0f / node->meters_per_texel;
					auto wh = 768; //width < height ? width : height;
					auto r = (RENDER_DISTANCE * (1.0 / s)) * wh;
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

		lock.unlock();

		p.step("Between");

		std::queue<world_mesh*> new_meshes_to_buffer{};

		using mask_list = std::array<int, 8>;
		using time_list = std::array<float, 8>;

		struct octant_mask
		{
			mask_list masks{};
			time_list times{};
		};

		// 8-bit octant mask flags of nodes
		std::map<octant_identifier<>, octant_mask> mask_map{};
		static std::unordered_set<mesh*> last_drawn_meshes{};
		std::unordered_set<mesh*> drawn_meshes{};
		drawn_meshes.reserve(last_drawn_meshes.size());

		p.step("Loop 2");

		const auto& ctx = game_world.get_shader_context();
		ctx.use_shader();

		glUniform1f(ctx.animation_time_loc, ANIMATION_TIME);

		for (const auto& potential_node : std::ranges::reverse_view(potential_nodes))
		{
			// reverse order
			const auto& full_path = potential_node.first;
			auto* node = potential_node.second;
			auto level = full_path.size();

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
			auto octant = full_path[level - 1];
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

			glm::mat4 transform = viewprojection * node->matrix_globe_from_mesh;

			glUniformMatrix4fv(ctx.transform_loc, 1, GL_FALSE, &transform[0][0]);

			p.step("Loop2Draw");

			mask_entry.times[octant] = mesh.draw(ctx, current_time, mask.times, mask.masks);
			current_vertices += node->get_vertices();

			p.step("Loop 2");
		}

		p.step("Push buffer");

		size_t buffer_queue{0};

		meshes_to_buffer.access([&](std::queue<::world_mesh*>& meshes)
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

		p.step("Draw Text");

		static double prevTime = 0;
		auto crntTime = glfwGetTime();
		auto timeDiff = crntTime - prevTime;
		static unsigned int counter = 0;
		static int fps = 60;

		counter++;

		if (timeDiff >= 1.0 / 4)
		{
			fps = static_cast<int>((1.0 / timeDiff) * counter);
			prevTime = crntTime;
			counter = 0;
		}

		constexpr auto color = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);

		auto offset = 35.0f;

		renderer.draw("FPS: " + std::to_string(fps), 25.0f, (offset += 25.0f), 1.0f, color);
		renderer.draw("Tasks: " + std::to_string(rocktree.get_tasks()), 25.0f, (offset += 25.0f), 1.0f, color);
		renderer.draw("Downloads: " + std::to_string(rocktree.get_downloads()), 25.0f, (offset += 25.0f), 1.0f, color);
		renderer.draw("Buffering: " + std::to_string(buffer_queue), 25.0f, (offset += 25.0f), 1.0f, color);
		renderer.draw("Objects: " + std::to_string(rocktree.get_objects()), 25.0f, (offset += 25.0f), 1.0f, color);
		renderer.draw("Vertices: " + std::to_string(current_vertices), 25.0f, (offset += 25.0f), 1.0f, color);
		renderer.draw("Distance: " + std::to_string(RENDER_DISTANCE), 25.0f, (offset += 25.0f), 1.0f, color);

		/*for (size_t i = 0; i < task_manager::QUEUE_COUNT; ++i)
		{
			renderer.draw("Q " + std::to_string(i) + ": " + std::to_string(rocktree.get_tasks(i)), 25.0f,
			              (offset += 25.0f), 1.0f, color);
		}*/
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

	void bufferer(const std::stop_token& token, window& window,
	              utils::concurrency::container<std::queue<world_mesh*>>& meshes_to_buffer, rocktree& rocktree)
	{
		window.use_shared_context([&]
		{
			bool clean = false;
			auto last_cleanup_frame = frame_counter.load();
			while (!token.stop_requested())
			{
				rocktree.with<world>().get_bufferer().perform_cleanup();

				if (frame_counter > (last_cleanup_frame + 6))
				{
					clean = !clean;
					perform_cleanup(rocktree, clean);
					last_cleanup_frame = frame_counter.load();
				}

				if (!buffer_queue(meshes_to_buffer))
				{
					std::this_thread::sleep_for(10ms);
				}
			}
		});
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

		window window(1280, 800, "game");

		world game_world{};
		custom_rocktree<world, world_mesh> rocktree{"earth", game_world};

		utils::concurrency::container<std::queue<world_mesh*>> meshes_to_buffer{};

		const auto buffer_thread = utils::thread::create_named_jthread("Bufferer", [&](const std::stop_token& token)
		{
			bufferer(token, window, meshes_to_buffer, rocktree);
		});

		auto eye = lla_to_ecef(48.994556, 8.400166, 6364810.2166);
		glm::dvec3 direction{-0.295834, -0.662646, -0.688028};

		reactphysics3d::RigidBody* camera{};

		game_world.access_physics([&](reactphysics3d::PhysicsCommon& common, reactphysics3d::PhysicsWorld& world)
		{
			const auto quat = glm::angleAxis(0.0, direction);

			const reactphysics3d::Transform transform( //
				v(eye),
				reactphysics3d::Quaternion{
					quat.x, quat.y, quat.z, quat.w,
				}
			);

			camera = world.createRigidBody(transform);

			const reactphysics3d::Vector3 halfExtents(2.0, 3.0, 5.0);
			reactphysics3d::BoxShape* boxShape = common.createBoxShape(halfExtents);

			auto* collider = camera->addCollider(boxShape, {});
			collider->getMaterial().setMassDensity(4);
			collider->getMaterial().setBounciness(0.0);
			collider->getMaterial().setFrictionCoefficient(0.3);
			camera->updateMassPropertiesFromColliders();

			camera->setType(reactphysics3d::BodyType::DYNAMIC);
			camera->enableGravity(true);

			camera->setLinearDamping(0.2f);
			camera->setAngularDamping(0.2f);
		});

		auto fs = cmrc::bird::get_filesystem();
		auto opensans = fs.open("resources/font/OpenSans-Regular.ttf");

		text_renderer text_renderer({opensans.cbegin(), opensans.cend()}, 24);

		window.show([&](profiler& p)
		{
			p.silence();
			run_frame(p, window, rocktree, eye, direction, meshes_to_buffer, text_renderer, camera);
		});
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
