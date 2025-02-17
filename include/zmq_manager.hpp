#pragma once
#include <zmq.hpp>
#include <functional>
#include <memory>
#include <spdlog/spdlog.h>
#include <libwebsockets.h>

#pragma pack(push, 1)  // Ensure struct packing matches Python
struct TradeSignal {
    char symbol[32];
    char side[8];
    char type[16];
    double quantity;
    double price;
    bool new_signal;
    char padding[7];
};
#pragma pack(pop)

class ZMQManager {
public:
    ZMQManager(struct lws_context* lws_ctx, const std::string& address)
        : context_(1), 
          socket_(context_, zmq::socket_type::sub),
          lws_ctx_(lws_ctx) {
        
        // Initialize the sul_ struct
        memset(&sul_, 0, sizeof(sul_));
        
        try {
            socket_.connect(address);
            socket_.set(zmq::sockopt::subscribe, "");
            spdlog::info("ZMQ SUB socket connected to {}", address);
            
            // Register with libwebsockets event loop
            lws_sul_schedule(lws_ctx_, 0, &sul_, zmq_callback, 1);
        } catch (const zmq::error_t& e) {
            spdlog::error("ZMQ connection error: {}", e.what());
            throw;
        }
    }

    ~ZMQManager() {
        try {
            // Cancel any pending callbacks
            lws_sul_schedule(lws_ctx_, 0, &sul_, nullptr, LWS_SET_TIMER_USEC_CANCEL);
            
            // Close socket and context
            socket_.close();
            context_.close();
        } catch (const zmq::error_t& e) {
            spdlog::error("ZMQ cleanup error: {}", e.what());
        }
    }

    void set_signal_handler(std::function<void(const TradeSignal&)> handler) {
        handler_ = std::move(handler);
    }

private:
    static void zmq_callback(struct lws_sorted_usec_list *sul) {
        ZMQManager* self = lws_container_of(sul, ZMQManager, sul_);
        
        try {
            zmq::message_t msg;
            while(self->socket_.recv(msg, zmq::recv_flags::dontwait)) {
                if (msg.size() != sizeof(TradeSignal)) {
                    spdlog::warn("Received message with unexpected size: {} (expected {})", 
                                msg.size(), sizeof(TradeSignal));
                    continue;
                }
                
                TradeSignal signal;
                std::memcpy(&signal, msg.data(), sizeof(TradeSignal));
                
                if (self->handler_) {
                    self->handler_(signal);
                }
            }
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN) {  // Ignore EAGAIN errors (no messages available)
                spdlog::error("ZMQ receive error: {}", e.what());
            }
        }

        // Reschedule timer (check every 10ms)
        lws_sul_schedule(self->lws_ctx_, 0, sul, zmq_callback, 10 * 1000);
    }

    zmq::context_t context_;
    zmq::socket_t socket_;
    struct lws_context* lws_ctx_;
    struct lws_sorted_usec_list sul_;
    std::function<void(const TradeSignal&)> handler_;
};
