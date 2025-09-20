#include "http.hpp"
#include <curl/curl.h>

#include "thread.hpp"
#include "finally.hpp"

#ifdef max
#undef max
#endif

#pragma comment(lib, "ws2_32.lib")

namespace utils::http
{
    namespace
    {
        void setup_curl()
        {
            static struct x
            {
                x()
                {
                    curl_global_init(CURL_GLOBAL_DEFAULT);
                }

                ~x()
                {
                    curl_global_cleanup();
                }
            } _;

            (void)_;
        }

        struct progress_helper
        {
            const std::function<void(size_t)>* callback{};
            std::exception_ptr exception{};
        };

        int progress_callback(void* clientp, const curl_off_t /*dltotal*/, const curl_off_t dlnow, const curl_off_t /*ultotal*/,
                              const curl_off_t /*ulnow*/)
        {
            auto* helper = static_cast<progress_helper*>(clientp);

            try
            {
                if (*helper->callback)
                {
                    (*helper->callback)(dlnow);
                }
            }
            catch (...)
            {
                helper->exception = std::current_exception();
                return -1;
            }

            return 0;
        }

        size_t write_callback(void* contents, const size_t size, const size_t nmemb, void* userp)
        {
            auto* buffer = static_cast<std::string*>(userp);

            const auto total_size = size * nmemb;
            buffer->append(static_cast<char*>(contents), total_size);
            return total_size;
        }

        std::optional<std::string> perform_request(const std::string& url, const std::string* post_body, const headers& headers,
                                                   const std::function<void(size_t)>& callback, const uint32_t retries)
        {
            setup_curl();

            curl_slist* header_list = nullptr;
            auto* curl = curl_easy_init();
            if (!curl)
            {
                return {};
            }

            auto _ = utils::finally([&]() {
                curl_slist_free_all(header_list);
                curl_easy_cleanup(curl);
            });

            for (const auto& header : headers)
            {
                auto data = header.first + ": " + header.second;
                header_list = curl_slist_append(header_list, data.data());
            }

            std::string buffer{};
            progress_helper helper{};
            helper.callback = &callback;

            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
            curl_easy_setopt(curl, CURLOPT_URL, url.data());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &helper);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "bird-client/1.0");
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

            if (post_body)
            {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_body->size()));
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body->data());
            }

            for (auto i = 0u; i < retries + 1; ++i)
            {
                // Due to CURLOPT_FAILONERROR, CURLE_OK will not be met when the server returns 400 or 500
                if (curl_easy_perform(curl) == CURLE_OK)
                {
                    long http_code = 0;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                    if (http_code >= 200)
                    {
                        return {std::move(buffer)};
                    }

                    throw std::runtime_error("Bad status code " + std::to_string(http_code) + " met while trying to download file " + url);
                }

                if (helper.exception)
                {
                    std::rethrow_exception(helper.exception);
                }

                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                if (http_code > 0)
                {
                    break;
                }
            }

            return {};
        }

        class curl_easy_request
        {
          public:
            curl_easy_request() = default;

            curl_easy_request(const std::string& url, stoppable_result_callback callback, CURLM* multi_request = nullptr)
                : result_(std::make_unique<std::string>()),
                  result_function_(std::move(callback)),
                  multi_request_(multi_request),
                  request_(curl_easy_init())
            {
                curl_easy_setopt(this->request_, CURLOPT_URL, url.data());
                curl_easy_setopt(this->request_, CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(this->request_, CURLOPT_WRITEDATA, result_.get());
                curl_easy_setopt(this->request_, CURLOPT_NOPROGRESS, 1L);
                curl_easy_setopt(this->request_, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(this->request_, CURLOPT_USERAGENT, "bird-client/1.0");
                curl_easy_setopt(this->request_, CURLOPT_FAILONERROR, 1L);

                if (this->multi_request_)
                {
                    curl_multi_add_handle(this->multi_request_, this->request_);
                }
            }

            curl_easy_request(const curl_easy_request&) = delete;
            curl_easy_request& operator=(const curl_easy_request&) = delete;

            curl_easy_request(curl_easy_request&& obj) noexcept
                : curl_easy_request()
            {
                this->operator=(std::move(obj));
            }

            curl_easy_request& operator=(curl_easy_request&& obj) noexcept
            {
                if (this != &obj)
                {
                    this->clear();

                    this->result_ = std::move(obj.result_);
                    this->result_function_ = std::move(obj.result_function_);

                    this->multi_request_ = std::move(obj.multi_request_);
                    this->request_ = std::move(obj.request_);

                    obj.multi_request_ = nullptr;
                    obj.request_ = nullptr;
                }

                return *this;
            }

            ~curl_easy_request()
            {
                this->clear();
            }

            CURL* get_request() const
            {
                return this->request_;
            }

            void notify(const bool success)
            {
                result res{};
                if (success && this->result_ && this->has_success_code())
                {
                    res = std::move(*this->result_);
                    this->result_.reset();
                }

                this->result_function_(std::move(res));
            }

            bool is_cancelled() const
            {
                return this->result_function_.is_stopped();
            }

          private:
            std::unique_ptr<std::string> result_{};
            stoppable_result_callback result_function_;

            CURLM* multi_request_{};
            CURL* request_{};

            void clear() const
            {
                if (!this->request_)
                {
                    return;
                }

                if (this->multi_request_)
                {
                    curl_multi_remove_handle(this->multi_request_, this->request_);
                }

                curl_easy_cleanup(this->request_);
            }

            bool has_success_code() const
            {
                long http_code = 0;
                curl_easy_getinfo(this->request_, CURLINFO_RESPONSE_CODE, &http_code);
                return http_code >= 200;
            }
        };
    }

    class worker_thread::worker
    {
      public:
        worker(const size_t max_requests)
            : max_requests_(max_requests),
              request_(curl_multi_init())
        {
            setup_curl();
        }

        worker(const worker&) = delete;
        worker& operator=(const worker&) = delete;

        worker(worker&&) noexcept = default;
        worker& operator=(worker&&) noexcept = default;

        ~worker()
        {
            this->active_requests_.clear();

            if (this->request_)
            {
                curl_multi_cleanup(this->request_);
            }
        }

        size_t get_downloads() const
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            return this->active_requests_.size();
        }

        void wakeup() const
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            if (this->request_ && this->active_requests_.size() < this->max_requests_)
            {
                curl_multi_wakeup(this->request_);
            }
        }

        bool work(const std::chrono::milliseconds& timeout, concurrency::container<query_queue>& queue)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto end = now + timeout;

            while (std::chrono::steady_clock::now() < end)
            {
                this->clear_cancelled_requests();
                this->add_new_requests(queue);

                if (this->active_requests_.empty())
                {
                    return false;
                }

                const auto running_requests = this->perform_current_requests();
                if (!running_requests)
                {
                    return false;
                }

                if (*running_requests)
                {
                    const auto remaining_time = end - std::chrono::steady_clock::now();
                    const auto timeout_duration = std::chrono::duration_cast<std::chrono::milliseconds>(remaining_time);

                    (void)this->poll_current_requests(timeout_duration);
                }

                this->dispatch_results();
            }

            return !this->active_requests_.empty();
        }

      private:
        size_t max_requests_{};
        CURLM* request_{};
        std::unordered_map<void*, curl_easy_request> active_requests_{};

        void add_new_requests(concurrency::container<query_queue>& queue)
        {
            if (this->active_requests_.size() >= this->max_requests_)
            {
                return;
            }

            std::vector<stoppable_result_callback> deleted_callbacks{};
            queue.access([this, &deleted_callbacks](query_queue& queue) {
                while (!queue.empty() && this->active_requests_.size() < this->max_requests_)
                {
                    auto& query = queue.front();
                    if (!query.callback.is_stopped())
                    {
                        curl_easy_request request(query.url, std::move(query.callback), this->request_);
                        this->active_requests_[request.get_request()] = std::move(request);
                    }
                    else
                    {
                        deleted_callbacks.emplace_back(std::move(query.callback));
                    }

                    queue.pop_front();
                }
            });
        }

        std::optional<size_t> perform_current_requests() const
        {
            int still_running{};
            const auto error = curl_multi_perform(this->request_, &still_running);
            if (error || still_running < 0)
            {
                return {};
            }

            return static_cast<size_t>(still_running);
        }

        bool poll_current_requests(const std::chrono::milliseconds& timeout) const
        {
            return curl_multi_poll(this->request_, nullptr, 0, static_cast<int>(timeout.count()), nullptr) == 0;
        }

        void clear_cancelled_requests()
        {
            for (auto i = this->active_requests_.begin(); i != this->active_requests_.end();)
            {
                if (i->second.is_cancelled())
                {
                    i = this->active_requests_.erase(i);
                }
                else
                {
                    ++i;
                }
            }
        }

        void dispatch_results()
        {
            while (true)
            {
                int msg_in_queue{};
                const auto* msg = curl_multi_info_read(this->request_, &msg_in_queue);

                if (!msg)
                {
                    break;
                }

                if (msg->msg != CURLMSG_DONE)
                {
                    continue;
                }

                auto entry = this->active_requests_.find(msg->easy_handle);
                if (entry == this->active_requests_.end())
                {
                    throw std::runtime_error("Bad request entry!");
                }

                if (!entry->second.is_cancelled())
                {
                    entry->second.notify(msg->data.result == CURLE_OK);
                }

                this->active_requests_.erase(entry);
            }
        }
    };

    worker_thread::worker_thread(concurrency::container<query_queue>& queue, std::condition_variable& cv, const size_t max_requests)
        : queue_(&queue),
          cv_(&cv),
          worker_(std::make_unique<worker>(max_requests)),
          thread_(thread::create_named_jthread("HTTP Worker", [this](const utils::thread::stop_token& token) {
              while (!token.stop_requested())
              {
                  this->work(std::chrono::seconds(1));
              }
          }))
    {
    }

    worker_thread::~worker_thread() = default;

    void worker_thread::wakeup() const
    {
        this->worker_->wakeup();
    }

    void worker_thread::request_stop()
    {
        this->thread_.request_stop();
        this->wakeup();
    }

    void worker_thread::stop()
    {
        this->request_stop();

        if (this->thread_.joinable())
        {
            this->thread_.join();
        }
    }

    size_t worker_thread::get_downloads() const
    {
        if (!this->worker_)
        {
            return 0;
        }

        return this->worker_->get_downloads();
    }

    void worker_thread::work(const std::chrono::milliseconds& timeout) const
    {
        const auto end = std::chrono::steady_clock::now() + timeout;

        bool has_requests_to_process = true;

        auto should_wake_up = [&] { return !this->queue_->get_raw().empty() || has_requests_to_process; };

        while (true)
        {
            auto lock = this->queue_->acquire_lock();

            auto now = std::chrono::steady_clock::now();
            if (now > end)
            {
                break;
            }

            auto remaining_time = end - now;
            auto timeout_duration = std::chrono::duration_cast<std::chrono::milliseconds>(remaining_time);

            if (!should_wake_up())
            {
                this->cv_->wait_for(lock, timeout_duration, should_wake_up);
            }

            lock.unlock();

            now = std::chrono::steady_clock::now();
            if (now > end)
            {
                break;
            }

            remaining_time = end - now;
            timeout_duration = std::chrono::duration_cast<std::chrono::milliseconds>(remaining_time);

            has_requests_to_process = this->worker_->work(timeout_duration, *this->queue_);
        }
    }

    downloader::downloader(const size_t num_worker_threads, const size_t max_downloads)
    {
        constexpr auto min_per_thread = static_cast<size_t>(1);
        const auto requests_per_thread = std::max(min_per_thread, max_downloads / num_worker_threads);

        this->workers_.reserve(num_worker_threads);
        for (size_t i = 0; i < num_worker_threads; ++i)
        {
            this->workers_.emplace_back(std::make_unique<worker_thread>(this->queue_, this->cv_, requests_per_thread));
        }
    }

    downloader::~downloader() = default;

    std::future<result> downloader::download(url_string url, utils::thread::stop_token token, const bool high_priority)
    {
        auto promise = std::make_shared<std::promise<result>>();
        auto future = promise->get_future();

        this->download(
            std::move(url), [p = std::move(promise)](result result) { p->set_value(std::move(result)); }, std::move(token), high_priority);

        return future;
    }

    void downloader::download(url_string url, result_function function, utils::thread::stop_token token, const bool high_priority)
    {
        this->queue_.access([&](query_queue& queue) {
            query q{std::move(url), stoppable_result_callback{std::move(function), std::move(token)}};

            if (high_priority)
            {
                queue.emplace_front(std::move(q));
            }
            else
            {
                queue.emplace_back(std::move(q));
            }
        });

        std::atomic_thread_fence(std::memory_order_release);

        this->wakeup();
        this->cv_.notify_one();
    }

    void downloader::stop()
    {
        for (const auto& worker : this->workers_)
        {
            worker->request_stop();
        }

        for (const auto& worker : this->workers_)
        {
            worker->stop();
        }
    }

    size_t downloader::get_downloads() const
    {
        size_t downloads = this->queue_.access<size_t>([](const query_queue& q) { return q.size(); });

        for (const auto& w : this->workers_)
        {
            if (w)
            {
                downloads += w->get_downloads();
            }
        }

        return downloads;
    }

    void downloader::wakeup() const
    {
        for (const auto& w : this->workers_)
        {
            w->wakeup();
        }
    }

    std::optional<std::string> post_data(const std::string& url, const std::string& post_body, const headers& headers,
                                         const std::function<void(size_t)>& callback, const uint32_t retries)
    {
        return perform_request(url, &post_body, headers, callback, retries);
    }

    std::optional<std::string> get_data(const std::string& url, const headers& headers, const std::function<void(size_t)>& callback,
                                        const uint32_t retries)
    {
        return perform_request(url, {}, headers, callback, retries);
    }
}
