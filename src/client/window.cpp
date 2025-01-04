#include "std_include.hpp"

#include "window.hpp"

#include <utils/nt.hpp>
#include <utils/finally.hpp>

namespace
{
	void init_sdl()
	{
		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK))
		{
			throw std::runtime_error("Unable to initialize SDL");
		}
	}
}

window::window(const int width, const int height, const std::string& title)
{
	init_sdl();
	this->create(width, height, title);
}

window::~window()
{
	SDL_Quit();
}

window::operator SDL_Window*() const
{
	return this->handle_;
}

void window::create(const int width, const int height, const std::string& title)
{
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	/*glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_DEPTH_BITS, 32);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);

	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);*/

	SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
	SDL_CreateWindowAndRenderer(title.c_str(), width, height, SDL_WINDOW_OPENGL, &this->handle_, &this->renderer_);

	SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

	this->shared_handle_ = SDL_CreateWindow("", 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
	this->shared_context_ = SDL_GL_CreateContext(this->handle_);

	SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);


	//glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

	//this->shared_handle_ = glfwCreateWindow(640, 480, "", nullptr, this->handle_);

	/*glfwSetWindowUserPointer(this->handle_, this);
	glfwMakeContextCurrent(this->handle_);
	glfwSetWindowSizeCallback(this->handle_, window::size_callback_static);

	glfwSwapInterval(-1);*/

	glViewport(0, 0, width, height);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	//glfwSetInputMode(this->handle_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	//glfwSetCursorPos(this->handle_, 0, 0);
}

void window::size_callback(const int width, const int height)
{
	glViewport(0, 0, width, height);
}

/*void window::size_callback_static(GLFWwindow* _window, const int width, const int height)
{
	static_cast<window*>(glfwGetWindowUserPointer(_window))->size_callback(width, height);
}*/

void window::show(const std::function<void(profiler& profiler)>& frame_callback)
{
	SDL_Event event;
	while (this->handle_)
	{
		profiler p{"Poll"};

		while (!SDL_PollEvent(&event))
		{
			break;
		}

		p.step("Draw");

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		frame_callback(p);

		p.step("Swap");

		SDL_RenderPresent(this->renderer_);

		this->update_frame_times();
	}
}

void window::close()
{
	//glfwSetWindowShouldClose(this->handle_, GLFW_TRUE);
}

bool window::is_key_pressed(const int key) const
{
	return false;//return glfwGetKey(*this, key) == GLFW_PRESS;
}

std::pair<double, double> window::get_mouse_position() const
{
	double mouse_x{0.0}, mouse_y{0.0};

#ifdef WIN32
	if (!utils::nt::is_wine())
#endif
	{
		//glfwGetCursorPos(*this, &mouse_x, &mouse_y);
		//glfwSetCursorPos(*this, 0, 0);
	}

	return {mouse_x, mouse_y};
}

long long window::get_last_frame_time() const
{
	return this->last_frame_time_;
}

double window::get_current_time() const
{
	const auto now = std::chrono::system_clock::now();
	const auto diff = now - this->start_time_;
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff);

	return static_cast<double>(ms.count());
}

void window::use_shared_context(const std::function<void()>& callback)
{
	std::lock_guard<std::mutex> lock{this->shared_context_mutex_};
	auto* old_context = SDL_GL_GetCurrentContext();
	auto* old_window = SDL_GL_GetCurrentWindow();
	const auto _ = utils::finally([&]
	{
		SDL_GL_MakeCurrent(old_window, old_context);
	});

	SDL_GL_MakeCurrent(this->shared_handle_, this->shared_context_);

	callback();
}

void window::update_frame_times()
{
	const auto now = std::chrono::system_clock::now();
	this->last_frame_time_ = std::chrono::duration_cast<std::chrono::microseconds>(now - this->last_frame_).count();
	this->last_frame_ = now;
}
