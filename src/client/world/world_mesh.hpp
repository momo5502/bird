#pragma once

#include "physics_node.hpp"

class world_mesh : public node_data
{
public:
	world_mesh(node& node);

	void buffer_meshes();
	bool is_buffered() const;
	bool is_buffering() const;
	bool mark_for_buffering();

	float draw(const shader_context& ctx, float current_time, const std::array<float, 8>& child_draw_time,
	           const std::array<int, 8>& octant_mask);

	static void buffer_queue(std::queue<world_mesh*> meshes);

private:
	enum class buffer_state
	{
		unbuffered,
		buffering,
		buffered,
	};

	std::vector<mesh> meshes_{};
	std::optional<float> draw_time_{};
	std::atomic<buffer_state> buffer_state_{buffer_state::unbuffered};
	std::optional<physics_node> physics_node_{};

	bool buffer_meshes_internal();
	void mark_as_buffered();

	bool can_be_deleted() const override
	{
		return this->buffer_state_ != buffer_state::buffering;
	}
};
