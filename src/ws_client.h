#pragma once

#include <string>
#include <functional>
#include <vector>

class WsClient {
private:
    std::function<void(const std::string&, bool)> subscriptionCallback_;
    std::function<void(const std::string&, bool)> unsubscriptionCallback_;
    std::vector<std::pair<std::string, bool>> pendingCallbacks_; // Tracks callbacks to be processed

public:
    void setSubscriptionCallback(std::function<void(const std::string&, bool)> callback);
    void setUnsubscriptionCallback(std::function<void(const std::string&, bool)> callback);
    void processEvents();
}; 