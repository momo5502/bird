#include "std_include.hpp"

#include "gl_objects.hpp"
#include "crosshair.hpp"

namespace
{
	std::string_view get_vertex_shader()
	{
		return R"code(
attribute vec2 position;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
}
			)code";
	}

	std::string_view get_fragment_shader()
	{
		return R"code(
uniform vec2 screen_size;

void main() {
    vec2 center = screen_size / 2.0;
    vec2 pos = gl_FragCoord.xy;

    float thickness = 2.0;
    float length = 10.0;

    bool isVertical = abs(pos.x - center.x) < thickness && abs(pos.y - center.y) < length;
    bool isHorizontal = abs(pos.y - center.y) < thickness && abs(pos.x - center.x) < length;

    if (isVertical || isHorizontal) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
    } else {
        discard;
    }
}
			)code";
	}
}

crosshair::crosshair()
	: shader_(get_vertex_shader(), get_fragment_shader())
	  , vao_(create_vertex_array_object())
{
	scoped_vao _1{this->vao_};
	const auto _2 = this->shader_.use();

	this->position_loc_ = this->shader_.attribute("position");
	this->screen_size_loc_ = this->shader_.uniform("screen_size");

	this->vertex_buffer_ = create_buffer();

	constexpr float quadVertices[] = {

		-1.0f, 1.0f,
		-1.0f, -1.0f,
		1.0f, -1.0f,
		1.0f, 1.0f
	};

	glBindBuffer(GL_ARRAY_BUFFER, this->vertex_buffer_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);

	glVertexAttribPointer(this->position_loc_, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	glEnableVertexAttribArray(this->position_loc_);
}

void crosshair::draw() const
{
	scoped_vao _1{this->vao_};
	const auto _2 = this->shader_.use();

	glClear(GL_DEPTH_BUFFER_BIT);

	GLint viewport[4]{};
	glGetIntegerv(GL_VIEWPORT, viewport);

	const auto width = static_cast<float>(viewport[2]);
	const auto height = static_cast<float>(viewport[3]);

	glUniform2f(this->screen_size_loc_, width, height);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}
