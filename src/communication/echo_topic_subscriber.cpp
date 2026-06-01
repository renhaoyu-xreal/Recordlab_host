#include "recordlab_host/communication/echo_topic_subscriber.h"

#include <stdexcept>
#include <string>

#include <zmq.h>

namespace recordlab::host {

namespace {

std::string recvString(void* socket) {
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    int rc = zmq_msg_recv(&msg, socket, 0);
    if (rc < 0) {
        zmq_msg_close(&msg);
        throw std::runtime_error("ZMQ receive failed");
    }
    std::string data(static_cast<char*>(zmq_msg_data(&msg)), zmq_msg_size(&msg));
    zmq_msg_close(&msg);
    return data;
}

}  // namespace

EchoTopicSubscriber::EchoTopicSubscriber(std::string host, int port, std::string topic, Callback callback)
    : topic_(std::move(topic)), callback_(std::move(callback)) {
    context_ = zmq_ctx_new();
    socket_ = zmq_socket(context_, ZMQ_SUB);
    int linger = 0;
    zmq_setsockopt(socket_, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(socket_, ZMQ_SUBSCRIBE, topic_.c_str(), topic_.size());
    const std::string endpoint = "tcp://" + host + ":" + std::to_string(port);
    if (zmq_connect(socket_, endpoint.c_str()) != 0) {
        throw std::runtime_error("Failed to connect subscriber: " + endpoint);
    }
}

EchoTopicSubscriber::~EchoTopicSubscriber() {
    if (socket_) zmq_close(socket_);
    if (context_) zmq_ctx_term(context_);
}

bool EchoTopicSubscriber::pollOnce(int timeout_ms) {
    zmq_pollitem_t items[] = {{socket_, 0, ZMQ_POLLIN, 0}};
    int rc = zmq_poll(items, 1, timeout_ms);
    if (rc <= 0) {
        return false;
    }
    std::string topic = recvString(socket_);
    int more = 0;
    size_t more_size = sizeof(more);
    zmq_getsockopt(socket_, ZMQ_RCVMORE, &more, &more_size);
    if (!more) {
        return false;
    }
    std::string payload = recvString(socket_);
    if (topic == topic_) {
        callback_(nlohmann::json::parse(payload));
        return true;
    }
    return false;
}

}  // namespace recordlab::host
