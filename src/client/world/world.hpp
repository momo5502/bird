#pragma once

#include "../gl_objects.hpp"
#include "../shader_context.hpp"

class world
{
public:
	gl_bufferer& get_bufferer()
	{
		return this->bufferer_;
	}

	const shader_context& get_shader_context() const
	{
		return this->context_;
	}

private:
	shader_context context_{};
	gl_bufferer bufferer_{};
};
