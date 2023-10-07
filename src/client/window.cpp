#include "std_include.hpp"

#include "window.hpp"

window::window(const int width, const int height, const std::string& title)
{
	this->init_glfw();
	this->create(width, height, title);
	this->init_glew();
}

window::~window()
{
	glfwTerminate();
}

window::operator GLFWwindow*() const
{
	return this->handle_;
}

void window::init_glfw()
{
	if (glfwInit() != GLFW_TRUE)
	{
		throw std::runtime_error("Unable to initialize glfw");
	}
}

void window::init_glew()
{
	glewExperimental = true;

	if (glewInit() != GLEW_OK)
	{
		throw std::runtime_error("Unable to initialize glew");
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
}

void window::create(const int width, const int height, const std::string& title)
{
	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_DEPTH_BITS, 32);

	this->handle_ = glfwCreateWindow(width, height, title.data(), nullptr, nullptr);
	if (!this->handle_)
	{
		throw std::runtime_error("Unable to create window");
	}

	glfwSetWindowUserPointer(this->handle_, this);
	glfwMakeContextCurrent(this->handle_);
	glfwSetWindowSizeCallback(this->handle_, window::size_callback_static);

	glfwSwapInterval(1);

	glViewport(0, 0, width, height);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glClearDepth(1);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_POINT_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
	glHint(GL_POINT_SMOOTH_HINT, GL_FASTEST);

	glfwSetInputMode(this->handle_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void window::size_callback(const int width, const int height)
{
	glViewport(0, 0, width, height);
}

void window::size_callback_static(GLFWwindow* _window, const int width, const int height)
{
	static_cast<window*>(glfwGetWindowUserPointer(_window))->size_callback(width, height);
}

void window::show(const std::function<void()>& frame_callback)
{
	while (this->handle_ && !glfwWindowShouldClose(this->handle_))
	{
		glfwPollEvents();

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		frame_callback();

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

void window::update_frame_times()
{
	const auto now = std::chrono::system_clock::now();
	this->last_frame_time_ = std::chrono::duration_cast<std::chrono::microseconds>(now - this->last_frame_).count();
	this->last_frame_ = now;
}
