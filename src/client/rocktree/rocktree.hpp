#pragma once

#include "rocktree_object.hpp"

#include "node.hpp"
#include "bulk.hpp"
#include "planetoid.hpp"

#include "../task_manager.hpp"

#include <utils/http.hpp>

class planetoid;

template <typename RocktreeData>
class typed_rocktree;

class rocktree
{
  public:
    friend rocktree_object;

    rocktree(std::string planet);
    virtual ~rocktree();

    const std::string& get_planet() const
    {
        return this->planet_;
    }

    planetoid* get_planetoid() const
    {
        return this->planetoid_.get();
    }

    task_manager& get_task_manager()
    {
        return this->task_manager_;
    }

    virtual node* allocate_node(bulk& parent, static_node_data&& data)
    {
        auto obj = std::make_unique<node>(*this, parent, std::move(data));
        auto* ptr = obj.get();

        this->store_object(std::move(obj));

        return ptr;
    }

    void cleanup_dangling_objects(const std::chrono::milliseconds& timeout);

    size_t get_tasks() const;
    size_t get_tasks(size_t i) const;
    size_t get_downloads() const;
    size_t get_objects() const;

    template <typename RocktreeData>
    typed_rocktree<RocktreeData>& as()
    {
        return static_cast<typed_rocktree<RocktreeData>&>(*this);
    }

    template <typename RocktreeData>
    const typed_rocktree<RocktreeData>& as() const
    {
        return static_cast<const typed_rocktree<RocktreeData>&>(*this);
    }

    template <typename RocktreeData>
    RocktreeData& with()
    {
        return this->as<RocktreeData>().get();
    }

    template <typename RocktreeData>
    const RocktreeData& as() const
    {
        return this->as<RocktreeData>().get();
    }

  private:
    std::string planet_{};

    using object_list = std::list<std::unique_ptr<generic_object>>;
    utils::concurrency::container<object_list> objects_{};
    utils::concurrency::container<object_list> new_objects_{};
    object_list::iterator object_iterator_ = objects_.get_raw().end();

    std::unique_ptr<planetoid> planetoid_{};
    utils::http::downloader downloader_{};
    task_manager task_manager_{};

  protected:
    void store_object(std::unique_ptr<generic_object> object);
};

template <typename RocktreeData>
class typed_rocktree : public rocktree
{
  public:
    typed_rocktree(std::string planet, RocktreeData& data)
        : rocktree(std::move(planet)),
          data_(&data)
    {
    }

    RocktreeData& get()
    {
        return *this->data_;
    }

    const RocktreeData& get() const
    {
        return *this->data_;
    }

  private:
    RocktreeData* data_{};
};

template <typename RocktreeData, typename NodeData>
class custom_rocktree : public typed_rocktree<RocktreeData>
{
  public:
    using typed_rocktree<RocktreeData>::typed_rocktree;

    node* allocate_node(bulk& parent, static_node_data&& data) override
    {
        auto obj = std::make_unique<typed_node<NodeData>>(*this, parent, std::move(data));
        auto* ptr = obj.get();

        this->store_object(std::move(obj));

        return ptr;
    }
};
