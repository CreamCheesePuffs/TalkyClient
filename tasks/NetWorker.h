// NetWorker.h
#pragma once

#include "Msg.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <winsock2.h>

//using RequestPayload = std::variant<RegisterReq, LoginReq, AddFriendReq, SendTextReq>;
//
//struct Request {
//    Cmd cmd = Cmd::Heartbeat;
//    int32_t seq = 0;
//    RequestPayload payload = SendTextReq{};
//};
//
//struct Packet {
//    int32_t cmd = 0;
//    int32_t seq = 0;
//    std::string payload;
//};

class NetWorker {
public:
    struct Callbacks {
        std::function<void(bool)> onConnectedChanged;
        std::function<void(const std::string&)> onNetworkError;
        std::function<void(int, const std::string&)> onRegisterFinished;
        std::function<void(int, const std::string&, const UserInfo&)> onLoginFinished;
        std::function<void(int, const UserInfo&)> onSearchFriendFinished;
        std::function<void(int, const UserInfo&)> onAddFriendFinished;
        std::function<void(int, const UserInfo&)> onDeleteFriendFinished;
        std::function<void(const UserInfo&)> onAddFriendRequestReceived;
        std::function<void(const UserInfo&)> onAddFriendResponseReceived;
        std::function<void(int)> onDeleteFriendRequestReceived;
        std::function<void(int)> onDeleteFriendResponseReceived;
        std::function<void(const ChatMsg&)> onMessageReceived;
    };

    NetWorker();
    ~NetWorker();

    void SetServer(std::string host, uint16_t port);
    void SetCallbacks(Callbacks cbs);

    void Start();
    void Stop();

    void Register(const std::string& username, const std::string& nickname, const std::string& password);
    void Login(const std::string& username, const std::string& password, int32_t status);
    void SearchFriend(const std::string& username);
    void AddFriend(const UserInfo& user);
    void AcceptFriendRequest(const UserInfo& targetUser);
    void RejectFriendRequest(const UserInfo& targetUser);
    void DelFriend(int32_t targetUserId);
    void SendText(int32_t userId, int32_t toUserId, const std::string& text);

private:
    bool InitWinsock();
    void CleanupWinsock();
    bool ConnectSocket(int timeoutMs);
    void CloseSocket();
    bool IsSocketConnected() const;

    int32_t NextSeq();
    void loop();
    bool ensureConnected();
    void procSend();
    void procRecv();
    void procPacket();

    void DispatchPacket(const std::string& msg);
    void DispatchRegisterResult(const std::string& json);
    void DispatchLoginResult(const std::string& json);
    void DispatchSearchFriendResult(const std::string& json);
    void DispatchGetFriendListResult(const std::string& json);
    void DispatchChatMessage(const std::string& json, int32_t fromUserId);
    void DispatchOperateFriend(const std::string& json);

private:
    std::string _host;
    uint16_t    _port = 0;

    bool _running = false;
    std::thread _thread;

    // mutable std::mutex _socketMtx;
    SOCKET _socket    = INVALID_SOCKET;
    bool   _wsaInited = false;
    bool   _connected = false;

    std::mutex _callbacksMutex;
    Callbacks  _callbacks;

    std::string _sendBuffer;
    std::mutex  _sendBufferMutex;
    std::string _recvBuffer;
    // std::mutex  _recvBufferMutex;
    
    int32_t _seq = 1;
    int64_t _lastHeartbeatSec = 0;

    int     _lastError;
};