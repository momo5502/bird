#pragma once

#include "rocktree_object.hpp"
#include "octant_identifier.hpp"

class node;

struct static_bulk_data
{
    uint32_t epoch{};
    octant_identifier<> path{};
};

class bulk final : public rocktree_object
{
  public:
    bulk(rocktree& rocktree, const generic_object& parent, static_bulk_data&& sdata);

    glm::dvec3 head_node_center{};

    std::unordered_map<octant_identifier<>, node*> nodes{};
    std::unordered_map<octant_identifier<>, bulk*> bulks{};

    const octant_identifier<>& get_path() const;

  private:
    static_bulk_data sdata_{};

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
