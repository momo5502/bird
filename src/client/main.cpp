#include "std_include.hpp"
#include "window.hpp"
#include "rocktree.hpp"
#include "input.hpp"

#include "text_renderer.hpp"

#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/thread.hpp>
#include <utils/concurrency.hpp>

namespace
{
	constexpr float ANIMATION_TIME = 350.0f;

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

			const auto planetoid = rocktree.get_planetoid();
			if (!planetoid || !planetoid->is_in_final_state()) return;

			const auto& current_bulk = planetoid->root_bulk;
			if (!current_bulk || !current_bulk->is_in_final_state()) return;

			perform_bulk_cleanup(*current_bulk);
		}
		else
		{
			profiler p("Dangling");

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

	void run_frame(profiler& p, window& window, const rocktree& rocktree, glm::dvec3& eye, glm::dvec3& direction,
	               const shader_context& ctx,
	               utils::concurrency::container<std::queue<node*>>& nodes_to_buffer, text_renderer& renderer)
	{
		const auto current_time = static_cast<float>(window.get_current_time());

		p.step("Input");

		const auto state = get_input_state(window);
		if (state.exit)
		{
			window.close();
			return;
		}

		p.step("Prepare");

		++frame_counter;

		static double prevTime = 0;
		auto crntTime = glfwGetTime();
		auto timeDiff = crntTime - prevTime;
		static unsigned int counter = 0;

		counter++;

		if (timeDiff >= 1.0 / 4)
		{
			// Creates new title
			std::string FPS = std::to_string(static_cast<int>((1.0 / timeDiff) * counter * 10) * 0.1);
			std::string ms = std::to_string(static_cast<int>((timeDiff / counter) * 1000 * 10) * 0.1);
			std::string newTitle = "game - " + FPS + "FPS / " + ms + "ms";
			glfwSetWindowTitle(window, newTitle.c_str());

			// Resets times and counter
			prevTime = crntTime;
			counter = 0;
		}

		const auto planetoid = rocktree.get_planetoid();
		if (!planetoid || !planetoid->can_be_used()) return;

		auto* current_bulk = planetoid->root_bulk;
		if (!current_bulk || !current_bulk->can_be_used()) return;

		const auto planet_radius = planetoid->radius;


		p.step("Calculate");

		GLint viewport[4]{};
		glGetIntegerv(GL_VIEWPORT, viewport);

		const auto width = viewport[2];
		const auto height = viewport[3];

		// up is the vec from the planetoid's center towards the sky
		const auto up = glm::normalize(eye);

		// projection
		const auto aspect_ratio = static_cast<double>(width) / static_cast<double>(height);
		constexpr auto fov = 0.25 * glm::pi<double>();
		const auto altitude = glm::length(eye) - planet_radius;

		paint_sky(altitude);

		const auto horizon = sqrt(altitude * (2 * planet_radius + altitude));
		auto near_val = 1.0;
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
		auto new_eye = eye
			+ state.up * forwards
			+ state.down * backwards
			+ state.left * left
			+ state.right * right;
		auto pot_altitude = glm::length(new_eye) - planet_radius;
		if (pot_altitude < 1000 * 1000 * 10)
		{
			eye = new_eye;
		}

		const auto view = glm::lookAt(eye, eye + direction, up);
		const auto viewprojection = projection * view;

		auto frustum_planes = get_frustum_planes(viewprojection);

		std::queue<std::pair<octant_identifier<>, bulk*>> valid{};
		valid.emplace(octant_identifier{}, current_bulk);

		std::map<octant_identifier<>, node*> potential_nodes;

		p.step("Loop1");

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
				if (obb_frustum_outside == classify_obb_frustum(node->obb, frustum_planes))
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
					auto r = (1.8 * (1.0 / s)) * wh;
					if (texels_per_meter > r)
					{
						continue;
					}
				}

				if (node->can_be_used() && node->can_have_data)
				{
					potential_nodes[nxt] = node;
				}

				valid.emplace(std::move(nxt), bulk);
			}
		}

		p.step("Between");

		std::queue<node*> new_nodes_to_buffer{};

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

			if (!node->is_buffered())
			{
				if (node->mark_for_buffering())
				{
					new_nodes_to_buffer.push(node);
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

			mask_entry.times[octant] = node->draw(ctx, current_time, mask.times, mask.masks);

			p.step("Loop 2");
		}

		p.step("Push buffer");

		size_t buffer_queue{0};

		nodes_to_buffer.access([&](std::queue<::node*>& nodes)
		{
			buffer_queue = nodes.size() + new_nodes_to_buffer.size();

			if (nodes.empty())
			{
				nodes = std::move(new_nodes_to_buffer);
				return;
			}

			while (!new_nodes_to_buffer.empty())
			{
				auto* node = new_nodes_to_buffer.front();
				new_nodes_to_buffer.pop();

				nodes.push(node);
			}
		});

		p.step("Draw Text");

		constexpr auto color = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
		renderer.draw("Tasks: " + std::to_string(rocktree.get_tasks()), 25.0f, 60.0f, 1.0f, color);
		renderer.draw("Downloads: " + std::to_string(rocktree.get_downloads()), 25.0f, 85.0f, 1.0f, color);
		renderer.draw("Buffering: " + std::to_string(buffer_queue), 25.0f, 110.0f, 1.0f, color);
		renderer.draw("Objects: " + std::to_string(rocktree.get_objects()), 25.0f, 135.0f, 1.0f, color);

		for (size_t i = 0; i < task_manager::QUEUE_COUNT; ++i)
		{
			renderer.draw("Q " + std::to_string(i) + ": " + std::to_string(rocktree.get_tasks(i)), 25.0f,
			              160.0f + 25.0f * i, 1.0f,
			              color);
		}
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

	bool buffer_queue(utils::concurrency::container<std::queue<node*>>& nodes_to_buffer)
	{
		std::queue<node*> node_queue{};

		nodes_to_buffer.access([&node_queue](std::queue<node*>& nodes)
		{
			if (nodes.empty())
			{
				return;
			}

			node_queue = std::move(nodes);
			nodes = {};
		});

		if (node_queue.empty())
		{
			return false;
		}

		node::buffer_queue(node_queue);
		return true;
	}

	void bufferer(const std::stop_token& token, window& window,
	              utils::concurrency::container<std::queue<node*>>& nodes_to_buffer, rocktree& rocktree)
	{
		window.use_shared_context([&]
		{
			bool clean = false;
			auto last_cleanup_frame = frame_counter.load();
			while (!token.stop_requested())
			{
				rocktree.get_bufferer().perform_cleanup();

				if (frame_counter > (last_cleanup_frame + 10))
				{
					clean = !clean;
					perform_cleanup(rocktree, clean);
					last_cleanup_frame = frame_counter.load();
				}

				if (!buffer_queue(nodes_to_buffer))
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

		utils::thread::set_priority(utils::thread::priority::high);

		window window(1280, 800, "game");

		const shader_context ctx{};

		rocktree rocktree{"earth"};

		utils::concurrency::container<std::queue<node*>> nodes_to_buffer{};

		const auto buffer_thread = utils::thread::create_named_jthread("Bufferer", [&](const std::stop_token& token)
		{
			bufferer(token, window, nodes_to_buffer, rocktree);
		});

		auto eye = lla_to_ecef(48.583374, 7.745776, 6364810.2166); // {4134696.707, 611925.83, 4808504.534};
		glm::dvec3 direction{0.219862, -0.419329, 0.012226};

		const auto font = utils::io::read_file("segoeui.ttf");
		text_renderer text_renderer(font, 24);

		window.show([&](profiler& p)
		{
			p.silence();
			run_frame(p, window, rocktree, eye, direction, ctx, nodes_to_buffer, text_renderer);
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
		MessageBoxA(nullptr, e.what(), "ERROR", MB_ICONERROR);
	}

	return 1;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
	return main(__argc, __argv);
}
#endif
