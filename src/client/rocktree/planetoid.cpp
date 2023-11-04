#include "../std_include.hpp"

#include "planetoid.hpp"
#include "bulk.hpp"

#include "rocktree_proto.hpp"

std::string planetoid::get_url() const
{
	return "PlanetoidMetadata";
}

std::filesystem::path planetoid::get_filepath() const
{
	return this->get_url();
}

void planetoid::populate(const std::optional<std::string>& data)
{
	PlanetoidMetadata planetoid{};
	if (!data || !planetoid.ParseFromString(*data))
	{
		throw std::runtime_error{"Failed to fetch planetoid"};
	}

	this->radius = planetoid.radius();
	this->root_bulk = this->allocate_object<bulk>(this->get_rocktree(), *this,
	                                              static_bulk_data{
		                                              planetoid.root_node_metadata().epoch(),
	                                              });
}

void planetoid::clear()
{
	if (this->root_bulk)
	{
		this->root_bulk->unlink_from(*this);
		this->root_bulk = {};
	}
}
