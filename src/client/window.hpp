#pragma once

class window
{
public:
	window(int width, int height, const std::string& title);
	~window();

	operator GLFWwindow*() const;

	void show();

	bool is_key_pressed(int key) const;

	long long get_last_frame_time() const;

private:
	GLFWwindow* handle_ = nullptr;

	long long last_frame_time_{};
	std::chrono::system_clock::time_point last_frame_ = std::chrono::system_clock::now();

	void update_frame_times();

	void create(int width, int height, const std::string& title);

	void init_glfw();
	void init_glew();

	static void size_callback(int width, int height);
	static void size_callback_static(GLFWwindow* window, int width, int height);
};
