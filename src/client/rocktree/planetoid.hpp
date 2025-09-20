#pragma once

#include "rocktree_object.hpp"

class bulk;

class planetoid final : public rocktree_object
{
  public:
    planetoid(rocktree& rocktree)
        : rocktree_object(rocktree, nullptr)
    {
    }

    float radius{};
    bulk* root_bulk{};

  private:
    bool is_high_priority() const override
    {
        return true;
    }

    std::string get_url() const override;
    std::filesystem::path get_filepath() const override;
    void populate(const std::optional<std::string>& data) override;
    void clear() override;
};
