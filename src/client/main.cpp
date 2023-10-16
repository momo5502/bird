#include "std_include.hpp"
#include "window.hpp"
#include "rocktree.hpp"

#include <utils/nt.hpp>
#include <utils/concurrency.hpp>

namespace
{
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

	void run_frame(window& window, const rocktree& rocktree, glm::dvec3& eye, glm::dvec3& direction,
	               const shader_context& ctx,
	               utils::concurrency::container<std::unordered_set<node*>>& nodes_to_buffer)
	{
		++frame_counter;

		if (window.is_key_pressed(GLFW_KEY_ESCAPE))
		{
			window.close();
			return;
		}

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

		auto* current_bulk = planetoid->root_bulk.get();
		if (!current_bulk || !current_bulk->can_be_used()) return;

		const auto planet_radius = planetoid->radius;

		auto key_up_pressed = (window.is_key_pressed(GLFW_KEY_UP) || window.is_key_pressed(GLFW_KEY_W)) ? 1.0 : 0.0;
		auto key_left_pressed = (window.is_key_pressed(GLFW_KEY_LEFT) || window.is_key_pressed(GLFW_KEY_A)) ? 1.0 : 0.0;
		auto key_down_pressed = (window.is_key_pressed(GLFW_KEY_DOWN) || window.is_key_pressed(GLFW_KEY_S)) ? 1.0 : 0.0;
		auto key_right_pressed = (window.is_key_pressed(GLFW_KEY_RIGHT) || window.is_key_pressed(GLFW_KEY_D))
			                         ? 1.0
			                         : 0.0;
		auto key_boost_pressed = (window.is_key_pressed(GLFW_KEY_LEFT_SHIFT) || window.is_key_pressed(
			                         GLFW_KEY_RIGHT_SHIFT))
			                         ? 1.0
			                         : 0.0;

		double mouse_x{}, mouse_y{};
		if (!utils::nt::is_wine())
		{
			glfwGetCursorPos(window, &mouse_x, &mouse_y);
			glfwSetCursorPos(window, 0, 0);
		}

		GLFWgamepadstate state{};
		if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1) && glfwGetGamepadState(GLFW_JOYSTICK_1, &state))
		{
			auto left_x = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
			auto left_y = state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
			auto right_x = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
			auto right_y = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
			auto right_trigger = (state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) / 2.0f;

			if (state.buttons[GLFW_GAMEPAD_BUTTON_CIRCLE] == GLFW_PRESS)
			{
				window.close();
				return;
			}

			const auto limit_value = [](float& value, const float deadzone)
			{
				if (value >= deadzone)
				{
					value = (value - deadzone) / (1.0f - deadzone);
				}
				else if (value <= -deadzone)
				{
					value = (value + deadzone) / (1.0f - deadzone);
				}
				else
				{
					value = 0.0;
				}
			};

			constexpr auto limit = 0.1f;
			limit_value(left_x, limit);
			limit_value(left_y, limit);
			limit_value(right_x, limit);
			limit_value(right_y, limit);
			limit_value(right_trigger, limit);

			const auto assign_max = [](double& value, const double new_value)
			{
				value = std::max(value, new_value);
			};

			assign_max(key_right_pressed, std::max(left_x, 0.0f));
			assign_max(key_left_pressed, std::abs(std::min(left_x, 0.0f)));

			assign_max(key_down_pressed, std::max(left_y, 0.0f));
			assign_max(key_up_pressed, std::abs(std::min(left_y, 0.0f)));

			mouse_x += right_x * 10.0;
			mouse_y += right_y * 10.0;

			key_boost_pressed = right_trigger + key_boost_pressed;
		}

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
		auto near_val = horizon > 370000 ? altitude / 2 : 1;
		auto far_val = horizon;

		if (near_val >= far_val) near_val = far_val - 1;
		if (isnan(far_val) || far_val < near_val) far_val = near_val + 1;

		const glm::dmat4 projection = glm::perspective(fov, aspect_ratio, near_val, far_val);

		// rotation
		double yaw = mouse_x * 0.005;
		double pitch = -mouse_y * 0.005;
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
		auto mag = 10 * (static_cast<double>(window.get_last_frame_time()) / 17000.0) * (1 + key_boost_pressed * 40) *
			speed_amp;
		auto sideways = glm::normalize(glm::cross(direction, up));
		auto forwards = direction * mag;
		auto backwards = -direction * mag;
		auto left = -sideways * mag;
		auto right = sideways * mag;
		auto new_eye = eye
			+ key_up_pressed * forwards
			+ key_down_pressed * backwards
			+ key_left_pressed * left
			+ key_right_pressed * right;
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

		const auto start = std::chrono::high_resolution_clock::now();

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

				bulk = bulk_kv->second.get();
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

				auto* node = node_kv->second.get();

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
					auto wh = 768; // width < height ? width : height;
					auto r = (2.0 * (1.0 / s)) * wh;
					if (texels_per_meter > r) continue;
				}

				if (node->can_have_data && node->can_be_used())
				{
					potential_nodes[nxt] = node;
				}

				valid.emplace(std::move(nxt), bulk);
			}
		}

		std::unordered_set<node*> new_nodes_to_buffer{};
		const auto loop1_duration = std::chrono::high_resolution_clock::now() - start;

		// 8-bit octant mask flags of nodes
		std::map<octant_identifier<>, uint8_t> mask_map{};
		static std::unordered_set<mesh*> last_drawn_meshes{};
		std::unordered_set<mesh*> drawn_meshes{};
		drawn_meshes.reserve(last_drawn_meshes.size());

		const auto start2 = std::chrono::high_resolution_clock::now();
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
				new_nodes_to_buffer.emplace(node);
				continue;
			}

			// set octant mask of previous node
			auto octant = full_path[level - 1];
			auto prev = full_path.substr(0, level - 1);

			auto& prev_entry = mask_map[prev];
			prev_entry |= 1 << octant;

			const auto mask = mask_map[full_path];

			// skip if node is masked completely
			if (mask == 0xff) continue;

			glm::mat4 transform = viewprojection * node->matrix_globe_from_mesh;

			glUniformMatrix4fv(ctx.transform_loc, 1, GL_FALSE, &transform[0][0]);
			for (auto& mesh : node->meshes)
			{
				mesh.draw(ctx, mask);
			}
		}

		nodes_to_buffer.access([&](std::unordered_set<::node*>& nodes)
		{
			for (auto* n : new_nodes_to_buffer)
			{
				nodes.emplace(n);
			}
		});

		const auto loop2_duration = std::chrono::high_resolution_clock::now() - start2;

		//if (ms > 100 * 1000)
		{
			const auto diff1 = std::chrono::duration_cast<std::chrono::milliseconds>(loop1_duration).count();
			const auto diff2 = std::chrono::duration_cast<std::chrono::milliseconds>(loop2_duration).count();

			if (diff1 >= 5 || diff2 >= 5)
				printf(
					"loop1: %lld | loop2: %lld\n", diff1, diff2

				);
		}
	}

	void trigger_high_performance_gpu_switch()
	{
#ifdef _WIN32
		const auto key = utils::nt::open_or_create_registry_key(
			HKEY_CURRENT_USER, R"(Software\Microsoft\DirectX\UserGpuPreferences)");
		if (!key)
		{
			return;
		}

		const auto self = utils::nt::library::get_by_address(&trigger_high_performance_gpu_switch);
		const auto path = self.get_path().make_preferred().wstring();

		if (RegQueryValueExW(key, path.data(), nullptr, nullptr, nullptr, nullptr) != ERROR_FILE_NOT_FOUND)
		{
			return;
		}

		const std::wstring data = L"GpuPreference=2;";
		RegSetValueExW(key, self.get_path().make_preferred().wstring().data(), 0, REG_SZ,
		               reinterpret_cast<const BYTE*>(data.data()),
		               static_cast<DWORD>((data.size() + 1u) * 2));
#endif
	}

	void perform_cleanup(node& node)
	{
		if (node.try_perform_deletion())
		{
			return;
		}

		if (!node.was_used_within(30s))
		{
			node.mark_for_deletion();
		}
	}

	void perform_cleanup(bulk& bulk)
	{
		if (bulk.try_perform_deletion())
		{
			return;
		}

		if (!bulk.was_used_within(30s) && bulk.mark_for_deletion())
		{
			return;
		}

		for (auto& entry : bulk.nodes | std::views::values)
		{
			perform_cleanup(*entry);
		}

		for (auto& val : bulk.bulks | std::views::values)
		{
			perform_cleanup(*val);
		}
	}

	void perform_cleanup(const rocktree& rocktree)
	{
		const auto planetoid = rocktree.get_planetoid();
		if (!planetoid || !planetoid->is_ready()) return;

		const auto& current_bulk = planetoid->root_bulk;
		if (!current_bulk || !current_bulk->is_ready()) return;

		perform_cleanup(*current_bulk);
	}

	void bufferer(const std::stop_token& token, window& window,
	              utils::concurrency::container<std::unordered_set<node*>>& nodes_to_buffer, const rocktree& rocktree)
	{
		window.use_shared_context([&]
		{
			auto last_cleanup_frame = frame_counter.load();
			while (!token.stop_requested())
			{
				if (frame_counter > (last_cleanup_frame + 10))
				{
					perform_cleanup(rocktree);
					last_cleanup_frame = frame_counter.load();
				}

				auto* node_to_buffer = nodes_to_buffer.access<node*>([](std::unordered_set<node*>& nodes) -> node* {
					if (nodes.empty())
					{
						return nullptr;
					}

					const auto entry = nodes.begin();
					auto* n = *entry;
					nodes.erase(entry);

					return n;
				});

				if (node_to_buffer)
				{
					node_to_buffer->buffer_meshes();
				}
				else
				{
					std::this_thread::sleep_for(10ms);
				}
			}
		});
	}
}

int main(int /*argc*/, char** /*argv*/)
{
	if (utils::nt::is_wine())
	{
		ShowWindow(GetConsoleWindow(), SW_HIDE);
	}

	SetThreadPriority(GetCurrentThread(), ABOVE_NORMAL_PRIORITY_CLASS);

	trigger_high_performance_gpu_switch();

	window window(1280, 800, "game");

	const shader_context ctx{};

	const rocktree rocktree{"earth"};

	utils::concurrency::container<std::unordered_set<node*>> nodes_to_buffer{};

	std::jthread buffer_thread([&](const std::stop_token& token)
	{
		bufferer(token, window, nodes_to_buffer, rocktree);
	});

	glm::dvec3 eye{4134696.707, 611925.83, 4808504.534};
	glm::dvec3 direction{0.219862, -0.419329, 0.012226};

	window.show([&]
	{
		run_frame(window, rocktree, eye, direction, ctx, nodes_to_buffer);
	});

	return 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
	return main(__argc, __argv);
}
