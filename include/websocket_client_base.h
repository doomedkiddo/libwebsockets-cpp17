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
        virtual ~IClientHandler() = default;
        virtual void onMessage(const std::string& payload) = 0;
        virtual void onUpdate() = 0;
        virtual std::string genSubscribePayload(const std::string& name, bool isUnsubscribe) = 0;
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
        virtual void reconnect(std::string_view reason) = 0;

        // 直接发送消息
        virtual void send(std::string_view payload) const = 0;

        // 发送订阅消息
        // 订阅的维持和重连，都由本层保证
        // 一旦subscribe接口被调用，该订阅请求就会被记录为unfinished
        // 只有收到包含successKey的一条消息时，才会将其标记为finished
        // 否则会根据用户指定的间隔进行重试
        // 当重连事件发生时，会将所有订阅请求按照顺序依次重新尝试
        virtual void subscribe(std::string_view name,
                               std::string_view payload,
                               std::string_view successKey,
                               bool waitOk) = 0;

        virtual void unSubscribe(std::string_view name,
                                 std::string_view payload,
                                 std::string_view successKey,
                                 bool waitOk) = 0;

        // 另一种订阅方式。payload动态生成
        virtual void subscribeDynamic(std::string_view name,
                                      std::string_view successKey,
                                      bool waitOk) = 0;

        virtual void unSubscribeDynamic(std::string_view name,
                                        std::string_view successKey,
                                        bool waitOk) = 0;

        // 查询某个频道是否订阅/反订阅ok
        virtual bool isSubscribeOk(std::string_view name) = 0;
        virtual bool isUnsubscribeOk(std::string_view name) = 0;

    protected:
        // 外部消息处理者（一般就是使用者）
        IClientHandler *handler;
    };
}

