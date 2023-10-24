#pragma once

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4127)
#pragma warning(disable: 4244)
#pragma warning(disable: 4458)
#pragma warning(disable: 4702)
#pragma warning(disable: 4996)
#pragma warning(disable: 5054)
#pragma warning(disable: 6011)
#pragma warning(disable: 6297)
#pragma warning(disable: 6385)
#pragma warning(disable: 6386)
#pragma warning(disable: 6387)
#pragma warning(disable: 26110)
#pragma warning(disable: 26451)
#pragma warning(disable: 26444)
#pragma warning(disable: 26451)
#pragma warning(disable: 26489)
#pragma warning(disable: 26495)
#pragma warning(disable: 26498)
#pragma warning(disable: 26812)
#pragma warning(disable: 28020)

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <ShlObj.h>
#include <d3d11.h>
#include <shellscalingapi.h>
#include <winternl.h>

// min and max is required by gdi, therefore NOMINMAX won't work
#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif
#endif

#include <map>
#include <set>
#include <list>
#include <array>
#include <deque>
#include <queue>
#include <thread>
#include <ranges>
#include <atomic>
#include <vector>
#include <mutex>
#include <string>
#include <chrono>
#include <memory>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <condition_variable>

#include <cassert>

#define GLM_FORCE_SILENT_WARNINGS 1
#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/vector_angle.hpp>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#ifdef _WIN32
#pragma warning(pop)
#endif

using namespace std::literals;
