#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <future>
#include <random>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <condition_variable>
#include <format>

using namespace std::chrono_literals;

constexpr int SENSOR_COUNT = 5;
constexpr auto SENSOR_UPDATE_INTERVAL = 1500ms;
constexpr auto AGGREGATION_INTERVAL = 3000ms;
constexpr int CONGESTION_THRESHOLD = 80;

struct TrafficData {
    int sensor_id;
    int density;
    std::chrono::system_clock::time_point timestamp;
};

std::vector<TrafficData> sensor_data;
std::shared_mutex data_mutex; // Shared access for reading, exclusive for writing
std::condition_variable_any cv;
std::atomic<bool> shutdown_flag = false;

// Utility: Generate random density values
int generate_density(int seed) {
    static thread_local std::default_random_engine engine(seed);
    static thread_local std::uniform_int_distribution<int> distribution(10, 100);
    return distribution(engine);
}

// Sensor logic
void sensor_task(int sensor_id, std::function<void(TrafficData)> callback) {
    while (!shutdown_flag.load()) {
        std::this_thread::sleep_for(SENSOR_UPDATE_INTERVAL);
        TrafficData data{sensor_id, generate_density(sensor_id), std::chrono::system_clock::now()};
        callback(std::move(data));
    }
}

// Aggregator callback
auto aggregator_callback = [](TrafficData data) {
    {
        std::unique_lock lock(data_mutex);
        sensor_data.push_back(std::move(data));
    }
    cv.notify_all();
};

// Logger task
void logger_task() {
    std::ofstream log_file("traffic_log_modern.txt", std::ios::trunc);
    if (!log_file.is_open()) {
        std::cerr << "Failed to open log file.\n";
        return;
    }

    while (!shutdown_flag.load()) {
        {
            std::unique_lock lock(data_mutex);
            cv.wait_for(lock, AGGREGATION_INTERVAL);
        }

        std::vector<TrafficData> local_data;
        {
            std::unique_lock lock(data_mutex);
            local_data.swap(sensor_data);
        }

        if (!local_data.empty()) {
            int total = 0, min_val = 100, max_val = 0;
            for (const auto& d : local_data) {
                total += d.density;
                min_val = std::min(min_val, d.density);
                max_val = std::max(max_val, d.density);
                if (d.density > CONGESTION_THRESHOLD) {
                    std::cerr << "[ALERT] Sensor " << d.sensor_id 
                    << ": Congestion detected with density " << d.density << '\n';
          
                }
            }

            int avg = total / static_cast<int>(local_data.size());
            log_file << "Avg: " << avg << ", Min: " << min_val << ", Max: " << max_val << '\n';

        }
    }

    log_file.close();
}

// Entry point
int main() {
    std::cout << "Running Modern Traffic Aggregator...\n";

    std::vector<std::future<void>> sensor_futures;
    for (int i = 0; i < SENSOR_COUNT; ++i) {
        sensor_futures.emplace_back(std::async(std::launch::async, sensor_task, i, aggregator_callback));
    }

    std::thread logger(logger_task);

    std::this_thread::sleep_for(60s);
    shutdown_flag = true;
    cv.notify_all();

    for (auto& f : sensor_futures) f.wait();
    logger.join();

    std::cout << "Finished Modern Version.\n";
    return 0;
}
