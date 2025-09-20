#pragma once

class profiler
{
  public:
    using time_point = std::chrono::high_resolution_clock::time_point;
    using duration = std::chrono::high_resolution_clock::duration;

    profiler(std::string first_step = "Begin", duration limit = ((1000ms / 60) + 3ms))
        : active_step_(std::move(first_step)),
          limit_(std::move(limit))
    {
    }

    ~profiler()
    {
        if (this->silenced_)
        {
            return;
        }

        const auto now = std::chrono::high_resolution_clock::now();
        const auto total_duration = now - this->start_;
        if (total_duration <= this->limit_)
        {
            return;
        }

        this->step({});

        const auto to_ms_string = [](const duration& d) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
            return std::to_string(ms.count());
        };

        std::string output{};
        output.reserve(100);

        output.append("Total: " + to_ms_string(total_duration) + "\n");

        for (const auto& step : this->steps_)
        {
            output.append(step.first + ": " + to_ms_string(step.second) + "\n");
        }

        puts(output.data());
    }

    void step(std::string step)
    {
        const auto now = std::chrono::high_resolution_clock::now();
        const auto step_duration = now - step_start_;

        this->steps_[std::move(this->active_step_)] += step_duration;
        this->active_step_ = std::move(step);
        this->step_start_ = now;
    }

    void silence()
    {
        this->silenced_ = true;
    }

  private:
    std::atomic_bool silenced_{false};
    time_point start_{std::chrono::high_resolution_clock::now()};
    time_point step_start_{start_};
    std::string active_step_{"__begin"};
    std::unordered_map<std::string, duration> steps_{};
    duration limit_{};
};
