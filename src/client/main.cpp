#include "std_include.hpp"
#include "window.hpp"
#include "rocktree.hpp"

#undef near
#undef far

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

	obb_frustum classifyObbFrustum(const oriented_bounding_box& obb, const std::array<glm::dvec4, 6>& planes)
	{
		auto result = obb_frustum_inside;
		const auto obb_orientation_t = glm::transpose(obb.orientation);

		for (int i = 0; i < 6; i++)
		{
			auto plane4 = planes[i];
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


	struct gl_ctx_t
	{
		GLuint program;
		GLint transform_loc;
		GLint uv_offset_loc;
		GLint uv_scale_loc;
		GLint octant_mask_loc;
		GLint texture_loc;
		GLint position_loc;
		GLint octant_loc;
		GLint texcoords_loc;
	};

	void meshTexImage2d(const mesh& mesh)
	{
		switch (mesh.format)
		{
		case texture_format::rgb:
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, mesh.texture_width, mesh.texture_height, 0, GL_RGB, GL_UNSIGNED_BYTE,
			             mesh.texture.data());
			break;
		case texture_format::dxt1:
			glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, mesh.texture_width,
			                       mesh.texture_height, 0, mesh.texture.size(), mesh.texture.data());
			break;
		}
	}

	void bufferMesh(mesh& mesh)
	{
		if (mesh.buffered) fprintf(stderr, "mesh already buffered\n"), abort();

		glGenBuffers(1, &mesh.vertex_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, mesh.vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(unsigned char), mesh.vertices.data(),
		             GL_STATIC_DRAW);
		glGenBuffers(1, &mesh.index_buffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.index_buffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned short), mesh.indices.data(),
		             GL_STATIC_DRAW);

		glGenTextures(1, &mesh.texture_buffer);
		glBindTexture(GL_TEXTURE_2D, mesh.texture_buffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // GL_NEAREST
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		meshTexImage2d(mesh);

		mesh.buffered = true;
	}

	void bindAndDrawMesh(const mesh& mesh, uint8_t octant_mask, const gl_ctx_t& ctx)
	{
		float uv_offset[2]{};
		uv_offset[0] = mesh.uv_offset[0];
		uv_offset[1] = mesh.uv_offset[1];

		float uv_scale[2]{};
		uv_scale[0] = mesh.uv_scale[0];
		uv_scale[1] = mesh.uv_scale[1];

		glUniform2fv(ctx.uv_offset_loc, 1, uv_offset);
		glUniform2fv(ctx.uv_scale_loc, 1, uv_scale);
		int v[8] = {
			(octant_mask >> 0) & 1, (octant_mask >> 1) & 1, (octant_mask >> 2) & 1, (octant_mask >> 3) & 1,
			(octant_mask >> 4) & 1, (octant_mask >> 5) & 1, (octant_mask >> 6) & 1, (octant_mask >> 7) & 1
		};
		glUniform1iv(ctx.octant_mask_loc, 8, v);
		glUniform1i(ctx.texture_loc, 0);
		glBindTexture(GL_TEXTURE_2D, mesh.texture_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, mesh.vertex_buffer);

		glVertexAttribPointer(ctx.position_loc, 3, GL_UNSIGNED_BYTE, GL_FALSE, 8, (void*)0);
		glVertexAttribPointer(ctx.octant_loc, 1, GL_UNSIGNED_BYTE, GL_FALSE, 8, (void*)3);
		glVertexAttribPointer(ctx.texcoords_loc, 2, GL_UNSIGNED_SHORT, GL_FALSE, 8, (void*)4);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.index_buffer);
		glDrawElements(GL_TRIANGLE_STRIP, mesh.indices.size(), GL_UNSIGNED_SHORT, NULL);
	}

	void unbufferMesh(mesh& mesh)
	{
		if (!mesh.buffered) fprintf(stderr, "mesh isn't buffered\n"), abort();

		mesh.buffered = false;

		glDeleteTextures(1, &mesh.texture_buffer); // auto: glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteBuffers(1, &mesh.index_buffer); // auto: glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDeleteBuffers(1, &mesh.vertex_buffer); // auto: glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	void checkCompileShaderError(GLuint shader)
	{
		GLint is_compiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &is_compiled);
		if (is_compiled == GL_TRUE) return;
		GLint max_len = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &max_len);
		std::vector<GLchar> error_log(max_len);
		glGetShaderInfoLog(shader, max_len, &max_len, &error_log[0]);
		puts(error_log.data());
		glDeleteShader(shader);
		abort();
	}

	GLuint makeShader(const char* vert_src, const char* frag_src)
	{
		GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vert_shader, 1, &vert_src, NULL);
		glCompileShader(vert_shader);
		checkCompileShaderError(vert_shader);
		GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(frag_shader, 1, &frag_src, NULL);
		glCompileShader(frag_shader);
		checkCompileShaderError(frag_shader);
		GLuint program = glCreateProgram();
		glAttachShader(program, vert_shader);
		glAttachShader(program, frag_shader);
		glLinkProgram(program);
		glDetachShader(program, vert_shader);
		glDetachShader(program, frag_shader);
		glDeleteShader(vert_shader);
		glDeleteShader(frag_shader);
		return program;
	}

	void initGL(gl_ctx_t& ctx)
	{
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		ctx.program = makeShader(
			"uniform mat4 transform;"
			"uniform vec2 uv_offset;"
			"uniform vec2 uv_scale;"
			"uniform bool octant_mask[8];"
			"attribute vec3 position;"
			"attribute float octant;"
			"attribute vec2 texcoords;"
			"varying vec2 v_texcoords;"
			"void main() {"
			"	float mask = octant_mask[int(octant)] ? 0.0 : 1.0;"
			"	v_texcoords = (texcoords + uv_offset) * uv_scale * mask;"
			"	gl_Position = transform * vec4(position, 1.0) * mask;"
			"}",

			"#ifdef GL_ES\n"
			"precision mediump float;\n"
			"#endif\n"
			"uniform sampler2D texture;"
			"varying vec2 v_texcoords;"
			"void main() {"
			"	gl_FragColor = vec4(texture2D(texture, v_texcoords).rgb, 1.0);"
			"}"
		);
		glUseProgram(ctx.program);
		ctx.transform_loc = glGetUniformLocation(ctx.program, "transform");
		ctx.uv_offset_loc = glGetUniformLocation(ctx.program, "uv_offset");
		ctx.uv_scale_loc = glGetUniformLocation(ctx.program, "uv_scale");
		ctx.octant_mask_loc = glGetUniformLocation(ctx.program, "octant_mask");
		ctx.texture_loc = glGetUniformLocation(ctx.program, "texture");
		ctx.position_loc = glGetAttribLocation(ctx.program, "position");
		ctx.octant_loc = glGetAttribLocation(ctx.program, "octant");
		ctx.texcoords_loc = glGetAttribLocation(ctx.program, "texcoords");

		glEnableVertexAttribArray(ctx.position_loc);
		glEnableVertexAttribArray(ctx.octant_loc);
		glEnableVertexAttribArray(ctx.texcoords_loc);
	}


	void run_frame(window& window, const rocktree& rocktree, glm::dvec3& eye, glm::dvec3& direction, gl_ctx_t& ctx)
	{
		if (window.is_key_pressed(GLFW_KEY_ESCAPE))
		{
			window.close();
			return;
		}

		const auto planetoid = rocktree.get_planetoid();
		if (!planetoid || !planetoid->is_ready()) return;

		auto* current_bulk = planetoid->root_bulk.get();
		if (!current_bulk || !current_bulk->is_ready()) return;

		const auto planet_radius = planetoid->radius;

		const auto key_up_pressed = window.is_key_pressed(GLFW_KEY_UP) || window.is_key_pressed(GLFW_KEY_W);
		const auto key_left_pressed = window.is_key_pressed(GLFW_KEY_LEFT) || window.is_key_pressed(GLFW_KEY_A);
		const auto key_down_pressed = window.is_key_pressed(GLFW_KEY_DOWN) || window.is_key_pressed(GLFW_KEY_S);
		const auto key_right_pressed = window.is_key_pressed(GLFW_KEY_RIGHT) || window.is_key_pressed(GLFW_KEY_D);
		const auto key_boost_pressed = window.is_key_pressed(GLFW_KEY_LEFT_SHIFT) || window.is_key_pressed(
			GLFW_KEY_RIGHT_SHIFT);

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
		auto near = horizon > 370000 ? altitude / 2 : 1;
		auto far = horizon;
		if (near >= far) near = far - 1;
		if (isnan(far) || far < near) far = near + 1;
		const glm::dmat4 projection = glm::perspective(fov, aspect_ratio, near, far);

		// rotation
		double mouse_x, mouse_y;
		glfwGetCursorPos(window, &mouse_x, &mouse_y);
		glfwSetCursorPos(window, 0, 0);
		double yaw = mouse_x * 0.005;
		double pitch = -mouse_y * 0.005;
		const auto overhead = glm::dot(direction, -up);

		if ((overhead > 0.99 && pitch < 0) || (overhead < -0.99 && pitch > 0))
			pitch = 0;

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
		auto mag = 10 * (window.get_last_frame_time() / 17000.0) * (1 + (key_boost_pressed ? 40 : 0)) *
			speed_amp;
		auto sideways = glm::normalize(glm::cross(direction, up));
		auto forwards = direction * mag;
		auto backwards = -direction * mag;
		auto left = -sideways * mag;
		auto right = sideways * mag;
		auto new_eye = eye
			+ (key_up_pressed ? 1.0 : 0) * forwards
			+ (key_down_pressed ? 1.0 : 0) * backwards
			+ (key_left_pressed ? 1.0 : 0) * left
			+ (key_right_pressed ? 1.0 : 0) * right;
		auto pot_altitude = glm::length(new_eye) - planet_radius;
		if (pot_altitude < 1000 * 1000 * 10)
		{
			eye = new_eye;
		}

		const auto view = glm::lookAt(eye, eye + direction, up);
		const auto viewprojection = projection * view;


		auto frustum_planes = get_frustum_planes(viewprojection); // for obb culling

		const std::string octs[] = {"0", "1", "2", "3", "4", "5", "6", "7"};
		std::vector<std::pair<std::string, bulk*>> valid{{"", current_bulk}};
		std::vector<std::pair<std::string, bulk*>> next_valid{};

		std::map<std::string, node*> potential_nodes;
		std::map<std::string, bulk*> potential_bulks;

		// node culling and level of detail using breadth-first search
		for (;;)
		{
			for (const auto& entry : valid)
			{
				const auto& cur = entry.first;
				auto* bulk = entry.second;

				if (!cur.empty() && cur.size() % 4 == 0)
				{
					auto rel = cur.substr(floor((cur.size() - 1) / 4) * 4, 4);
					auto bulk_kv = bulk->bulks.find(rel);
					auto has_bulk = bulk_kv != bulk->bulks.end();
					if (!has_bulk) continue;
					auto b = bulk_kv->second.get();
					potential_bulks[cur] = b;

					if (!b->can_be_used()) continue;
					bulk = b;
				}

				potential_bulks[cur] = bulk;

				for (const auto& o : octs)
				{
					auto nxt = cur + o;
					auto nxt_rel = nxt.substr(floor((nxt.size() - 1) / 4) * 4, 4);
					auto node_kv = bulk->nodes.find(nxt_rel);
					if (node_kv == bulk->nodes.end()) // node at "nxt" doesn't exist
						continue;
					auto node = node_kv->second.get();

					// cull outside frustum using obb
					// todo: check if it could cull more
					if (obb_frustum_outside == classifyObbFrustum(node->obb, frustum_planes))
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

					next_valid.emplace_back(nxt, bulk);

					if (node->can_have_data)
					{
						potential_nodes[nxt] = node;
					}
				}
			}

			if (next_valid.empty()) break;
			valid = next_valid;
			next_valid.clear();
		}

		for (const auto& n : potential_nodes)
		{
			n.second->fetch();
		}


		// 8-bit octant mask flags of nodes
		std::map<std::string, uint8_t> mask_map;

		for (auto kv = potential_nodes.rbegin(); kv != potential_nodes.rend(); ++kv)
		{
			// reverse order
			auto full_path = kv->first;
			auto node = kv->second;
			auto level = strlen(full_path.c_str());
			assert(level > 0);
			assert(node->can_have_data);
			if (!node->is_ready()) continue;

			// set octant mask of previous node
			auto octant = (int)(full_path[level - 1] - '0');
			auto prev = full_path.substr(0, level - 1);
			mask_map[prev] |= 1 << octant;

			// skip if node is masked completely
			if (mask_map[full_path] == 0xff) continue;

			// float transform matrix
			const auto transform = viewprojection * node->matrix_globe_from_mesh;
			glm::mat4 transform_float = transform;

			// buffer, bind, draw
			glUniformMatrix4fv(ctx.transform_loc, 1, GL_FALSE, &transform_float[0][0]);
			for (auto& mesh : node->meshes)
			{
				if (!mesh.buffered) bufferMesh(mesh);
				bindAndDrawMesh(mesh, mask_map[full_path], ctx);
			}
		}
	}
}

int main(int argc, char** argv)
{
	window window(800, 600, "game");

	const rocktree rocktree{"earth"};

	glm::dvec3 eye{4134696.707, 611925.83, 4808504.534};
	glm::dvec3 direction{0.219862, -0.419329, 0.012226};

	gl_ctx_t ctx{};
	initGL(ctx);

	window.show([&]
	{
		run_frame(window, rocktree, eye, direction, ctx);
	});

	return 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
	return main(__argc, __argv);
}
