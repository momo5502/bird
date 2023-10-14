#include "std_include.hpp"

#include "gl_object.hpp"

gl_object::gl_object(const GLuint object, std::function<void(GLuint)> destructor)
	: destructor_(std::move(destructor))
	  , object_(object)
{
}

gl_object::~gl_object()
{
	if (this->object_)
	{
		if (!this->destructor_)
		{
			std::abort();
		}

		this->destructor_(*this->object_);
	}
}

GLuint gl_object::get() const
{
	return *this->object_;
}

gl_object::operator unsigned() const
{
	return this->get();
}
