#include "std_include.hpp"

#include "window.hpp"

#include <utils/finally.hpp>

namespace
{
	void init_glfw()
	{
		if (glfwInit() != GLFW_TRUE)
		{
			throw std::runtime_error("Unable to initialize glfw");
		}
	}

	void init_glew()
	{
		glewExperimental = true;

		if (glewInit() != GLEW_OK)
		{
			throw std::runtime_error("Unable to initialize glew");
		}
	}
}

window::window(const int width, const int height, const std::string& title)
{
	init_glfw();
	this->create(width, height, title);
	init_glew();
}

window::~window()
{
	glfwTerminate();
}

window::operator GLFWwindow*() const
{
	return this->handle_;
}

void window::create(const int width, const int height, const std::string& title)
{
	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_DEPTH_BITS, 32);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);

	this->handle_ = glfwCreateWindow(width, height, title.data(), nullptr, nullptr);
	if (!this->handle_)
	{
		throw std::runtime_error("Unable to create window");
	}

	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

	this->shared_handle_ = glfwCreateWindow(640, 480, "", nullptr, this->handle_);

	glfwSetWindowUserPointer(this->handle_, this);
	glfwMakeContextCurrent(this->handle_);
	glfwSetWindowSizeCallback(this->handle_, window::size_callback_static);

	glfwSwapInterval(1);

	glViewport(0, 0, width, height);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	glfwSetInputMode(this->handle_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetCursorPos(this->handle_, 0, 0);
}

void window::size_callback(const int width, const int height)
{
	glViewport(0, 0, width, height);
}

void window::size_callback_static(GLFWwindow* _window, const int width, const int height)
{
	static_cast<window*>(glfwGetWindowUserPointer(_window))->size_callback(width, height);
}

void window::show(const std::function<void(profiler& profiler)>& frame_callback)
{
	while (this->handle_ && !glfwWindowShouldClose(this->handle_))
	{
		profiler p{"Pool"};

		glfwPollEvents();

		p.step("Draw");

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		frame_callback(p);

		p.step("Swap");

		glfwSwapBuffers(this->handle_);

		this->update_frame_times();
	}
}

void window::close()
{
	glfwSetWindowShouldClose(this->handle_, GLFW_TRUE);
}

bool window::is_key_pressed(const int key) const
{
	return glfwGetKey(*this, key) == GLFW_PRESS;
}

long long window::get_last_frame_time() const
{
	return this->last_frame_time_;
}

void window::use_shared_context(const std::function<void()>& callback)
{
	std::lock_guard<std::mutex> lock{this->shared_context_mutex_};
	auto old_context = glfwGetCurrentContext();
	const auto _ = utils::finally([&old_context]
	{
		glfwMakeContextCurrent(old_context);
	});

	glfwMakeContextCurrent(this->shared_handle_);

	callback();
}

void window::update_frame_times()
{
	const auto now = std::chrono::system_clock::now();
	this->last_frame_time_ = std::chrono::duration_cast<std::chrono::microseconds>(now - this->last_frame_).count();
	this->last_frame_ = now;
}
