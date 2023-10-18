#pragma once

#include "generic_object.hpp"

class rocktree;

class rocktree_object : public generic_object
{
public:
	rocktree_object(rocktree& rocktree);

	rocktree& get_rocktree() const
	{
		return *this->rocktree_;
	}

protected:
	virtual std::string get_url() const = 0;
	virtual void populate(const std::optional<std::string>& data) = 0;

	virtual bool is_high_priority() const
	{
		return false;
	}

	virtual bool prefer_cache() const
	{
		return true;
	}

private:
	rocktree* rocktree_{};

	void populate() override;
	void run_fetching();
};
