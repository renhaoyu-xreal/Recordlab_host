#include "recordlab_master/master_client.h"

#include <stdexcept>

namespace recordlab {

MasterClient::MasterClient(std::string endpoint, int timeout_ms)
    : endpoint_(std::move(endpoint)), timeout_ms_(timeout_ms) {
  resetSocket();
}

MasterClient::~MasterClient() {
  if (socket_) socket_->close();
  context_.close();
}

void MasterClient::resetSocket() {
  if (socket_) socket_->close();
  socket_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::req);
  socket_->set(zmq::sockopt::linger, 0);
  socket_->set(zmq::sockopt::rcvtimeo, timeout_ms_);
  socket_->set(zmq::sockopt::sndtimeo, timeout_ms_);
  socket_->connect(endpoint_);
}

json MasterClient::call(const std::string &op, const json &data) {
  return rawCall({{"op", op}, {"data", data}});
}

json MasterClient::rawCall(const json &request) {
  std::lock_guard<std::mutex> lock(mu_);
  try {
    const std::string text = request.dump();
    if (!socket_->send(zmq::buffer(text), zmq::send_flags::none)) {
      resetSocket();
      throw std::runtime_error("send failed");
    }
    zmq::message_t reply;
    auto ok = socket_->recv(reply, zmq::recv_flags::none);
    if (!ok) {
      resetSocket();
      throw std::runtime_error("request timeout");
    }
    return json::parse(std::string(static_cast<char *>(reply.data()), reply.size()));
  } catch (...) {
    resetSocket();
    throw;
  }
}

}  // namespace recordlab
