#pragma once

#include <mutex>

namespace utils
{
    class priority_mutex
    {
      public:
        std::recursive_mutex& high_priority()
        {
            return this->data_;
        }

        void lock()
        {
            std::unique_lock low_lock{this->low_};
            this->data_.lock();
        }

        void unlock() noexcept
        {
            this->data_.unlock();
        }

      private:
        std::recursive_mutex data_{};
        std::mutex low_{};
    };
}
