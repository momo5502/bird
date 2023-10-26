#pragma once

#include "generic_object.hpp"

class rocktree;

class rocktree_object : public generic_object
{
public:
	rocktree_object(rocktree& rocktree, const generic_object* parent);

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

	template <typename T, typename... Args>
	T* allocate_object(Args&&... args)
	{
		static_assert(std::is_base_of_v<rocktree_object, T>);

		auto obj = std::make_unique<T>(std::forward<Args>(args)...);
		auto* ptr = obj.get();

		this->store_object(std::move(obj));

		return ptr;
	}

private:
	rocktree* rocktree_{};

	void populate() override;
	void run_fetching();

	void store_object(std::unique_ptr<rocktree_object> object) const;
};
