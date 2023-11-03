#pragma once

#include "rocktree_object.hpp"
#include "octant_identifier.hpp"

class node;

class bulk final : public rocktree_object
{
public:
	bulk(rocktree& rocktree, const generic_object& parent, uint32_t epoch, std::string path = {});

	glm::dvec3 head_node_center{};
	std::map<octant_identifier<>, node*> nodes{};
	std::map<octant_identifier<>, bulk*> bulks{};

	const std::string& get_path() const;

private:
	uint32_t epoch_{};
	std::string path_{};

	bool is_high_priority() const override
	{
		return true;
	}

	std::string get_filename() const;
	std::string get_url() const override;
	std::filesystem::path get_filepath() const override;
	void populate(const std::optional<std::string>& data) override;
	void clear() override;
};
