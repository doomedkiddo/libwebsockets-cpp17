/*
 * @Author: aztec
 * @Date: 2025-02-11 10:45:10
 * @Description: websocket客户端逻辑的接口类（基类）
 */

#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace cexpp::util::wss
{
    extern bool wsLogEnabled;
    void setWsLogEnabled(bool enabled);

    // Handler
    class IClientHandler
    {
    public:
        virtual void onUpdate() = 0;
        virtual void onMessage(const nlohmann::json &payload) = 0;
        virtual void onMessage(const std::string &payload) = 0;
        virtual std::string genSubscribePayload(const std::string &name, bool unSub) = 0;
    };

    class ClientBase
    {
    public:
        ClientBase(IClientHandler *handler)
        {
            this->handler = handler;
        }

    public:
        // 主动重新连接
        virtual void reconnect(const std::string reason) = 0;

        // 直接发送消息
        virtual void send(const std::string &payload) const = 0;

        // 发送订阅消息
        // 订阅的维持和重连，都由本层保证
        // 一旦subscribe接口被调用，该订阅请求就会被记录为unfinished
        // 只有收到包含successKey的一条消息时，才会将其标记为finished
        // 否则会根据用户指定的间隔进行重试
        // 当重连事件发生时，会将所有订阅请求按照顺序依次重新尝试
        virtual void subscribe(const std::string &name,
                               const std::string &payload,
                               const std::string successKey,
                               bool waitOk) = 0;

        virtual void unSubscribe(const std::string &name,
                                 const std::string &payload,
                                 const std::string successKey,
                                 bool waitOk) = 0;

        // 另一种订阅方式。payload动态生成
        virtual void subscribeDynamic(const std::string &name,
                                      const std::string successKey,
                                      bool waitOk) = 0;

        virtual void unSubscribeDynamic(const std::string &name,
                                        const std::string successKey,
                                        bool waitOk) = 0;

        // 查询某个频道是否订阅/反订阅ok
        virtual bool isSubscribeOk(const std::string &name) = 0;
        virtual bool isUnsubscribeOk(const std::string &name) = 0;

    protected:
        // 外部消息处理者（一般就是使用者）
        IClientHandler *handler;
    };
}

