#pragma once

class gl_object
{
public:
	gl_object() = default;
	gl_object(GLuint object, std::function<void(GLuint)> destructor);

	~gl_object();

	gl_object(const gl_object&) = delete;
	gl_object& operator=(const gl_object&) = delete;

	gl_object(gl_object&& obj) noexcept = default;
	gl_object& operator=(gl_object&& obj) noexcept = default;

	GLuint get() const;
	operator GLuint() const;

private:
	std::function<void(GLuint)> destructor_{};
	std::optional<GLuint> object_{};
};
