#pragma once

#include "profiler.hpp"

class window
{
public:
	window(int width, int height, const std::string& title);
	~window();

	operator SDL_Window*() const;

	void show(const std::function<void(profiler& profiler)>& frame_callback);
	void close();

	bool is_key_pressed(int key) const;
	std::pair<double, double> get_mouse_position() const;

	long long get_last_frame_time() const;
	double get_current_time() const;

	void use_shared_context(const std::function<void()>& callback);

private:
	std::mutex shared_context_mutex_{};
	SDL_Renderer* renderer_ = nullptr;
	SDL_Window* handle_ = nullptr;

	SDL_Window* shared_handle_ = nullptr;
	SDL_GLContext shared_context_ = {};

	long long last_frame_time_{};
	std::chrono::system_clock::time_point last_frame_ = std::chrono::system_clock::now();
	std::chrono::system_clock::time_point start_time_ = std::chrono::system_clock::now();

	void update_frame_times();

	void create(int width, int height, const std::string& title);

	static void size_callback(int width, int height);
	static void size_callback_static(SDL_Window* window, int width, int height);
};
