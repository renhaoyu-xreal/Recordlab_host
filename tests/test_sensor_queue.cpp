#include "recordlab_host/data/sensor_queue.h"

#include <cassert>
#include <iostream>

int main() {
    recordlab::host::SensorQueue queue;
    queue.setSubscribedNames({"imu_data", "camera_data"});
    assert(queue.subscribedNames().size() == 2);

    queue.update("imu_data", nlohmann::json{{"type", 1}}, 100.0);
    auto latest = queue.latest("imu_data");
    assert(latest.has_value());
    assert(latest->frequency_hz == 100.0);

    queue.appendCurveSample("imu_data:1", nlohmann::json::array({1.0, 2.0, 3.0}), 10.0, 2);
    queue.appendCurveSample("imu_data:1", nlohmann::json::array({2.0, 3.0, 4.0}), 11.0, 2);
    queue.appendCurveSample("imu_data:1", nlohmann::json::array({3.0, 4.0, 5.0}), 12.0, 2);
    auto curve = queue.curveBuffer("imu_data:1");
    assert(curve.size() == 2);
    assert(curve.front()[0] == 11.0);
    assert(curve.back()[1] == 3.0);

    queue.remove("imu_data");
    assert(!queue.latest("imu_data").has_value());
    queue.clear();
    assert(queue.subscribedNames().empty());

    std::cout << "sensor queue ok\n";
    return 0;
}
