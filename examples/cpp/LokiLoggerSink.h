#ifndef LOKI_LOGGER_SINK_H
#define LOKI_LOGGER_SINK_H

#include <spdlog/sinks/base_sink.h>
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <mutex>
#include <string>
#include <map>

template<typename Mutex>
class loki_sink : public spdlog::sinks::base_sink<Mutex> {
public:
    loki_sink(const std::string& loki_url,
        const std::map<std::string, std::string>& labels = {},
        const std::string& tenant = "",
        const std::string& username = "",
        const std::string& password = "")
        : loki_url_(loki_url), labels_(labels), tenant_(tenant), username_(username), password_(password) {}

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override;

private:
    std::string loki_url_;
    std::map<std::string, std::string> labels_;
    std::string tenant_;
    std::string username_;
    std::string password_;
};

template<typename Mutex>
void loki_sink<Mutex>::sink_it_(const spdlog::details::log_msg& msg) {
    try {
        // Format the message
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);

        // Prepare the log entry with the correct timestamp
        auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            msg.time.time_since_epoch()).count();

        // Convert timestamp to string
        std::string timestamp_str = std::to_string(timestamp);

        // Construct the Loki-compatible JSON payload
        nlohmann::json log_entry = {
            {"streams", {{
                {"stream", labels_},
                {"values", nlohmann::json::array({
                    {timestamp_str, std::string(formatted.data(), formatted.size())}
                })}
            }}}
        };

        // Regular expression to extract the base URL and the path
        std::regex url_regex(R"((http[s]?:\/\/[^\/]+)(\/.*)?)");
        std::smatch url_match;
        std::string base_url, path;

        if (std::regex_search(loki_url_, url_match, url_regex)) {
            base_url = url_match[1].str(); // The base URL
            path = url_match[2].matched ? url_match[2].str() : "/loki/api/v1/push"; // The path or default to /loki/api/v1/push
        }
        else {
            throw std::runtime_error("Invalid Loki URL: " + loki_url_);
        }

        // Use the base URL (including port) directly in the client constructor
        httplib::Client client(base_url.c_str());

        httplib::Headers headers = { };
        // Add "X-Scope-OrgID" header only if tenant is not empty
        if (!tenant_.empty()) {
            headers.emplace("X-Scope-OrgID", tenant_);
        }

        // Basic authentication if needed
        if (!username_.empty() && !password_.empty()) {
            client.set_basic_auth(username_.c_str(), password_.c_str());
        }

        // Send the log entry to Loki using the extracted path
        auto res = client.Post(path.c_str(), headers, log_entry.dump(), "application/json");

        // Handle the error
        if (res.error() != httplib::Error::Success || res->status != 204) {
            // Log the error message to the console
            std::cerr << "Failed to send log to Loki. Status code: " << (res ? res->status : -1)
                << ", Error: " << httplib::to_string(res.error()) << std::endl;

            // Log the response body for further debugging
            if (res) {
                std::cerr << "Loki response body: " << res->body << std::endl;
            }
        }
    }
    catch (const std::exception& e) {
        // Catch and log any exceptions to prevent the application from crashing
        std::cerr << "Exception occurred while sending log to Loki: " << e.what() << std::endl;
    }
    catch (...) {
        // Catch all other exceptions
        std::cerr << "Unknown exception occurred while sending log to Loki." << std::endl;
    }
}

template<typename Mutex>
void loki_sink<Mutex>::flush_() {
    // No flushing needed for Loki
}

#include <mutex>
using loki_sink_mt = loki_sink<std::mutex>;
using loki_sink_st = loki_sink<spdlog::details::null_mutex>;

#endif // LOKI_LOGGER_SINK_H
