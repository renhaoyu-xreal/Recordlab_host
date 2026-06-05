#include "recordlab_host/data/topic_parser.h"

#include <algorithm>
#include <cmath>

namespace recordlab::host {
namespace {

double secondsFromTimestampValue(double timestamp) {
    if (timestamp <= 0.0) {
        return 0.0;
    }
    return timestamp > 1e12 ? timestamp / 1e9 : timestamp;
}

double timestampSeconds(const nlohmann::json& value,
                        std::chrono::steady_clock::time_point now) {
    if (value.is_object()) {
        const auto timestamp_ns = value.find("timestamp_ns");
        if (timestamp_ns != value.end() && timestamp_ns->is_number()) {
            const double seconds = timestamp_ns->get<double>() / 1e9;
            if (seconds > 0.0) return seconds;
        }
        const auto timestamp = value.find("timestamp");
        if (timestamp != value.end() && timestamp->is_number()) {
            const double seconds = secondsFromTimestampValue(timestamp->get<double>());
            if (seconds > 0.0) return seconds;
        }
    }
    return std::chrono::duration<double>(now.time_since_epoch()).count();
}

std::size_t estimateJsonBinaryBytes(const nlohmann::json& value) {
    if (value.is_object()) {
        const auto marker = value.find("__echo_bytes_base64__");
        if (marker != value.end() && marker->is_string()) {
            return marker->get<std::string>().size() * 3 / 4;
        }
    }
    if (value.is_string()) {
        return value.get<std::string>().size() * 3 / 4;
    }
    if (value.is_array()) {
        return value.size();
    }
    return 0;
}

std::size_t estimatePayloadBytes(const nlohmann::json& value) {
    if (value.is_object()) {
        const auto cam_data = value.find("cam_data");
        if (cam_data != value.end() && cam_data->is_object()) {
            std::size_t total = 0;
            for (const auto& [_, cam_info] : cam_data->items()) {
                if (!cam_info.is_object()) continue;
                const auto image = cam_info.find("image");
                if (image == cam_info.end() || !image->is_object()) continue;
                if (image->value("shm", false) || image->contains("shm_seq")) {
                    total += image->dump().size();
                } else if (image->contains("data")) {
                    total += estimateJsonBinaryBytes((*image)["data"]);
                } else {
                    total += static_cast<std::size_t>(image->value("bytes_per_line", 0))
                        * static_cast<std::size_t>(image->value("height", 0));
                }
            }
            return total;
        }
    }
    return value.dump().size();
}

class GenericJsonParser final : public TopicParser {
public:
    ParsedTopicSample parse(const std::string& topic_name,
                            const nlohmann::json& raw,
                            std::chrono::steady_clock::time_point now) const override {
        ParsedTopicSample sample;
        sample.topic_name = topic_name;
        sample.stream_key = topic_name;
        sample.value = raw;
        if (raw.is_object()) {
            const auto type = raw.find("type");
            if (type != raw.end() && type->is_number_integer()) {
                sample.stream_key += ":" + std::to_string(type->get<int>());
            }
            const auto data = raw.find("data");
            if (data != raw.end() && data->is_array()) {
                sample.display_values = nlohmann::json::array();
                for (const auto& item : *data) {
                    if (item.is_number()) sample.display_values.push_back(item);
                }
            }
        }
        sample.timestamp_seconds = timestampSeconds(raw, now);
        sample.bytes_estimate = estimatePayloadBytes(raw);
        return sample;
    }
};

class TypedVectorParser final : public TopicParser {
public:
    ParsedTopicSample parse(const std::string& topic_name,
                            const nlohmann::json& raw,
                            std::chrono::steady_clock::time_point now) const override {
        ParsedTopicSample sample;
        sample.topic_name = topic_name;
        sample.value = raw;
        sample.timestamp_seconds = timestampSeconds(raw, now);
        sample.bytes_estimate = estimatePayloadBytes(raw);
        sample.stream_key = topic_name;
        if (raw.is_object()) {
            const auto type = raw.find("type");
            if (type != raw.end() && type->is_number_integer()) {
                sample.stream_key += ":" + std::to_string(type->get<int>());
            }
            const auto data = raw.find("data");
            if (data != raw.end() && data->is_array()) {
                sample.display_values = nlohmann::json::array();
                for (const auto& item : *data) {
                    if (item.is_number()) sample.display_values.push_back(item);
                }
            }
        }
        return sample;
    }
};

}  // namespace

std::unique_ptr<TopicParser> createTopicParser(const std::string& parse_mode,
                                               const nlohmann::json& metadata) {
    (void)metadata;
    if (parse_mode == "type_vector6_fast" || parse_mode == "typed_vector") {
        return std::make_unique<TypedVectorParser>();
    }
    return std::make_unique<GenericJsonParser>();
}

}  // namespace recordlab::host
