// NetWorker.cpp
#include "NetWorker.h"
#include "utils/IULog.h"
#include "utils/ProtocolStream.h"
#include "utils/Zlib.h"
#include "jsoncpp-1.9.0/json.h"


#include <chrono>
#include <algorithm>
#include <cstring>
#include <sstream>

#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

namespace {
constexpr int kConnectTimeoutMs = 3000;
constexpr int kLoopSleepMs = 10;
constexpr int kHeartbeatSec = 15;
constexpr size_t kRecvBufSize = 16 * 1024;
constexpr int kRetryDelayMs = 500;

void LogPacketQueued(const char* action,
    const std::string& body,
    const std::string& raw,
    const std::string& compressed,
    const MsgHeader& header,
    size_t pendingBytes)
{
    LOG_INFO("[NetWorker] queued packet action=%s bodyBytes=%u rawBytes=%u compressedBytes=%u compressFlag=%u pendingBytes=%u",
        action,
        static_cast<unsigned>(body.size()),
        static_cast<unsigned>(raw.size()),
        static_cast<unsigned>(compressed.size()),
        static_cast<unsigned>(header.compressflag),
        static_cast<unsigned>(pendingBytes));
}


void SkipWhitespace(const std::string& s, size_t& i)
{
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
}

bool ParseJsonStringLiteral(const std::string& s, size_t& i, std::string& out)
{
    if (i >= s.size() || s[i] != '"') {
        return false;
    }
    ++i;
    out.clear();

    while (i < s.size()) {
        const char ch = s[i++];
        if (ch == '"') {
            return true;
        }

        if (ch == '\\') {
            if (i >= s.size()) {
                return false;
            }
            const char esc = s[i++];
            switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default:
                return false;
            }
            continue;
        }

        out.push_back(ch);
    }

    return false;
}

bool ExtractJsonIntField(const std::string& json, const std::string& key, int32_t& value)
{
    size_t i = 0;
    while (i < json.size()) {
        SkipWhitespace(json, i);
        if (i >= json.size()) {
            break;
        }

        if (json[i] != '"') {
            ++i;
            continue;
        }

        std::string currentKey;
        if (!ParseJsonStringLiteral(json, i, currentKey)) {
            return false;
        }

        SkipWhitespace(json, i);
        if (i >= json.size() || json[i] != ':') {
            continue;
        }
        ++i;
        SkipWhitespace(json, i);
        if (i >= json.size()) {
            return false;
        }

        bool negative = false;
        if (json[i] == '-') {
            negative = true;
            ++i;
        }

        if (i >= json.size() || !std::isdigit(static_cast<unsigned char>(json[i]))) {
            continue;
        }

        int64_t parsed = 0;
        while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i]))) {
            parsed = parsed * 10 + (json[i] - '0');
            ++i;
        }
        if (negative) {
            parsed = -parsed;
        }

        if (currentKey == key) {
            value = static_cast<int32_t>(parsed);
            return true;
        }
    }

    return false;
}

bool ExtractJsonStringField(const std::string& json, const std::string& key, std::string& value)
{
    size_t i = 0;
    while (i < json.size()) {
        SkipWhitespace(json, i);
        if (i >= json.size()) {
            break;
        }

        if (json[i] != '"') {
            ++i;
            continue;
        }

        std::string currentKey;
        if (!ParseJsonStringLiteral(json, i, currentKey)) {
            return false;
        }

        SkipWhitespace(json, i);
        if (i >= json.size() || json[i] != ':') {
            continue;
        }
        ++i;
        SkipWhitespace(json, i);
        if (i >= json.size()) {
            return false;
        }

        if (json[i] != '"') {
            continue;
        }

        std::string currentValue;
        if (!ParseJsonStringLiteral(json, i, currentValue)) {
            return false;
        }

        if (currentKey == key) {
            value = currentValue;
            return true;
        }
    }

    return false;
}

}

NetWorker::NetWorker()
{

}

NetWorker::~NetWorker()
{
    Stop();
}

std::string EscapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size());

    for (char ch : s)
    {
        switch (ch)
        {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += ch;     break;
        }
    }
    return out;
}

std::string MakeRegisterJson(const std::string& username,
    const std::string& nickname,
    const std::string& password)
{
    return std::string("{\"username\":\"") + EscapeJson(username) +
        "\",\"nickname\":\"" + EscapeJson(nickname) +
        "\",\"password\":\"" + EscapeJson(password) + "\"}";
}

std::string MakeLoginJson(const std::string& username,
    const std::string& password,
    int32_t status)
{
    return std::string("{\"username\":\"") + EscapeJson(username) +
        "\",\"password\":\"" + EscapeJson(password) +
        "\",\"clienttype\":1,\"status\":" + std::to_string(status) + "}";
}

std::string MakeSearchFriendJson(const std::string& username)
{
    return std::string("{\"type\":1,\"username\":\"") + EscapeJson(username) + "\"}";
}

std::string MakeOperateFriendJson(int32_t targetUserId, int32_t operationType)
{
    return std::string("{\"targetUserId\":") + std::to_string(targetUserId) +
        ",\"type\":" + std::to_string(operationType) + "}";
}

void NetWorker::Register(const std::string& username, const std::string& nickname, const std::string& password)
{
    LOG_INFO("[NetWorker] Register requested username=%s nickname=%s passwordLen=%u connected=%d",
        username.c_str(),
        nickname.c_str(),
        static_cast<unsigned>(password.size()),
        _connected ? 1 : 0);
    std::string body = MakeRegisterJson(username, nickname, password);

    std::string raw;
    BinaryStreamWriter writer(&raw);
    writer.WriteInt32(static_cast<int32_t>(msg_type_register));
    writer.WriteInt32(0);
    writer.WriteString(body);
    writer.Flush();

    // 3) 可选压缩 + 传输头
    std::string compressed;
    if (!Compress(raw, compressed))
    {
        LOG_ERROR("[NetWorker] Register compress failed bodyBytes=%u rawBytes=%u", static_cast<unsigned>(body.size()), static_cast<unsigned>(raw.size()));
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
    LogPacketQueued("register", body, raw, compressed, header, _sendBuffer.size());
}

void NetWorker::Login(const std::string& username, const std::string& password, int32_t status)
{
    LOG_INFO("[NetWorker] Login requested username=%s passwordLen=%u status=%d connected=%d",
        username.c_str(),
        static_cast<unsigned>(password.size()),
        static_cast<int>(status),
        _connected ? 1 : 0);
    std::string body = MakeLoginJson(username, password, status);

    // 2) 协议码流: [packlen/checksum头由 BinaryStreamWriter 管理] + cmd + seq + body
    std::string raw;
    BinaryStreamWriter writer(&raw);
    writer.WriteInt32(static_cast<int32_t>(msg_type_login));
    writer.WriteInt32(0);
    writer.WriteString(body);
    writer.Flush();

    // 3) 可选压缩 + 传输头
    std::string compressed;
    if (!Compress(raw, compressed))
    {
        LOG_ERROR("[NetWorker] Login compress failed bodyBytes=%u rawBytes=%u", static_cast<unsigned>(body.size()), static_cast<unsigned>(raw.size()));
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
    LogPacketQueued("login", body, raw, compressed, header, _sendBuffer.size());
}

void NetWorker::SearchFriend(const std::string& username)
{
    LOG_INFO("[NetWorker] SearchFriend requested username=%s connected=%d", username.c_str(), _connected ? 1 : 0);
    std::string body = MakeSearchFriendJson(username);

    std::string raw;
    BinaryStreamWriter writer(&raw);
    writer.WriteInt32(static_cast<int32_t>(msg_type_search_friend));
    writer.WriteInt32(0);
    writer.WriteString(body);
    writer.Flush();

    std::string compressed;
    if (!Compress(raw, compressed))
    {
        LOG_ERROR("[NetWorker] SearchFriend compress failed bodyBytes=%u rawBytes=%u", static_cast<unsigned>(body.size()), static_cast<unsigned>(raw.size()));
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
    LogPacketQueued("search_friend", body, raw, compressed, header, _sendBuffer.size());
}

void NetWorker::AddFriend(const UserInfo& targetUser)
{
    LOG_INFO("[NetWorker] AddFriend requested userId=%d username=%s nickname=%s connected=%d",
        targetUser.userId,
        targetUser.username.c_str(),
        targetUser.nickname.c_str(),
        _connected ? 1 : 0);
    std::string body = MakeOperateFriendJson(targetUser.userId, OPERATE_FRIEND_TYPE_ADD_REQUEST);

    std::string raw;
    BinaryStreamWriter writer(&raw);
    writer.WriteInt32(static_cast<int32_t>(msg_type_operate_friend));
    writer.WriteInt32(0);
    writer.WriteString(body);
    writer.Flush();

    std::string compressed;
    if (!Compress(raw, compressed))
    {
        LOG_ERROR("[NetWorker] AddFriend compress failed userId=%d username=%s nickname=%s bodyBytes=%u rawBytes=%u",
            targetUser.userId,
            targetUser.username.c_str(),
            targetUser.nickname.c_str(),
            static_cast<int>(targetUser.userId),
            static_cast<unsigned>(body.size()),
            static_cast<unsigned>(raw.size()));
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
    LogPacketQueued("add_friend", body, raw, compressed, header, _sendBuffer.size());
}

void NetWorker::AcceptFriendRequest(const UserInfo& targetUser)
{
    LOG_INFO("[NetWorker] AcceptFriendRequest userId=%d username=%s nickname=%s",
        targetUser.userId,
        targetUser.username.c_str(),
        targetUser.nickname.c_str());
    
    std::string body = MakeOperateFriendJson(targetUser.userId, OPERATE_FRIEND_TYPE_ADD_RESPONSE);

    std::string raw;
    BinaryStreamWriter writer(&raw);
    writer.WriteInt32(static_cast<int32_t>(msg_type_operate_friend));
    writer.WriteInt32(0);
    writer.WriteString(body);
    writer.Flush();

    std::string compressed;
    if (!Compress(raw, compressed))
    {
        LOG_ERROR("[NetWorker] AcceptFriendRequest compress failed targetUserId=%d bodyBytes=%u rawBytes=%u",
            static_cast<int>(targetUser.userId),
            static_cast<unsigned>(body.size()),
            static_cast<unsigned>(raw.size()));
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
    LogPacketQueued("accept_friend_request", body, raw, compressed, header, _sendBuffer.size());
}

void NetWorker::RejectFriendRequest(const UserInfo& targetUser)
{
    /* 当前实现不向服务器发送拒绝请求 */
}

void NetWorker::DelFriend(int32_t targetUserId)
{
    LOG_INFO("[NetWorker] DelFriend requested targetUserId=%d connected=%d", static_cast<int>(targetUserId), _connected ? 1 : 0);
    std::string body = MakeOperateFriendJson(targetUserId, OPERATE_FRIEND_TYPE_DELETE_REQUEST);

    std::string raw;
    BinaryStreamWriter writer(&raw);
    writer.WriteInt32(static_cast<int32_t>(msg_type_operate_friend));
    writer.WriteInt32(0);
    writer.WriteString(body);
    writer.Flush();

    std::string compressed;
    if (!Compress(raw, compressed))
    {
        LOG_ERROR("[NetWorker] DelFriend compress failed targetUserId=%d bodyBytes=%u rawBytes=%u",
            static_cast<int>(targetUserId),
            static_cast<unsigned>(body.size()),
            static_cast<unsigned>(raw.size()));
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
    LogPacketQueued("del_friend", body, raw, compressed, header, _sendBuffer.size());
}

void NetWorker::SendText(int32_t userId, int32_t toUserId, const std::string& text)
{
    // 聊天消息的二进制包体顺序需要和服务端 ChatSession::processMessage 对齐：
    // cmd -> seq -> data(string/json) -> targetId(int32)
    // 其中 data 里只放业务字段，targetId 单独跟在 string 后面。
    const int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    LOG_INFO("[NetWorker] SendText requested userId=%d targetId=%d textLen=%u connected=%d",
        static_cast<int>(userId),
        static_cast<int>(toUserId),
        static_cast<unsigned>(text.size()),
        _connected ? 1 : 0);

    std::string body = std::string("{\"userid\":") + std::to_string(userId) +
        ",\"msg\":\"" + EscapeJson(text) + "\"" +
        ",\"timestamp\":" + std::to_string(timestamp) + "}";

    std::string raw;
    BinaryStreamWriter writer(&raw);
    writer.WriteInt32(static_cast<int32_t>(msg_type_chat_msg));
    writer.WriteInt32(0);
    writer.WriteString(body);
    writer.WriteInt32(toUserId);
    writer.Flush();

    std::string compressed;
    if (!Compress(raw, compressed))
    {
        LOG_ERROR("[NetWorker] SendText compress failed userId=%d targetId=%d bodyBytes=%u rawBytes=%u",
            static_cast<int>(userId),
            static_cast<int>(toUserId),
            static_cast<unsigned>(body.size()),
            static_cast<unsigned>(raw.size()));
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
    LogPacketQueued("send_text", body, raw, compressed, header, _sendBuffer.size());
}

void NetWorker::SetServer(std::string host, uint16_t port) 
{
    LOG_INFO("[NetWorker] SetServer oldHost=%s oldPort=%u newHost=%s newPort=%u",
        _host.c_str(),
        static_cast<unsigned>(_port),
        host.c_str(),
        static_cast<unsigned>(port));
    _host = std::move(host);
    _port = port;
}

void NetWorker::SetCallbacks(Callbacks callbacks) 
{
    // std::lock_guard<std::mutex> lock(_callbacksMutex);
    _callbacks = std::move(callbacks);
}

void NetWorker::Start() 
{
    LOG_INFO("[NetWorker] Start called running=%d wsaInited=%d host=%s port=%u",
        _running ? 1 : 0,
        _wsaInited ? 1 : 0,
        _host.c_str(),
        static_cast<unsigned>(_port));
    if (_running)
    {
        return;
    }
    if (!_wsaInited && !InitWinsock())
    {
        LOG_ERROR("[NetWorker] Start failed because InitWinsock failed");
        std::unique_lock<std::mutex> lock(_callbacksMutex);
        if (_callbacks.onNetworkError)
        {
            _callbacks.onNetworkError("WSAStartup failed");
        }
        return;
    }
    _running = true;
    _thread  = std::thread(&NetWorker::loop, this);
}

void NetWorker::Stop() 
{
    LOG_INFO("[NetWorker] Stop called running=%d connected=%d socket=%lld",
        _running ? 1 : 0,
        _connected ? 1 : 0,
        static_cast<long long>(_socket));
    if (!_running)
    {
        return;
    }
    _running = false;
    //_queueCond.notify_all();
    if (_thread.joinable()) 
    {
        _thread.join();
    }
    CloseSocket();
    CleanupWinsock();
    LOG_INFO("[NetWorker] Stop finished");
}

bool NetWorker::InitWinsock() 
{
    if (_wsaInited)
    {
        return true;
    }
    WSADATA wsaData{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) 
    {
        int err = ::WSAGetLastError();
        LOG_ERROR("[NetWorker] WSAStartup failed err=%d", err);
        return false;
    }
    _wsaInited = true;
    LOG_INFO("[NetWorker] WSAStartup succeeded version=%u.%u",
        static_cast<unsigned>(LOBYTE(wsaData.wVersion)),
        static_cast<unsigned>(HIBYTE(wsaData.wVersion)));
    return true;
}

void NetWorker::CleanupWinsock() 
{
    if (!_wsaInited)
    {
        return;
    }
    ::WSACleanup();
    _wsaInited = false;
    LOG_INFO("[NetWorker] WSACleanup finished");
}

bool NetWorker::ConnectSocket(int timeoutMs) 
{
    LOG_INFO("[NetWorker] ConnectSocket begin host=%s port=%u timeoutMs=%d connected=%d socket=%lld",
        _host.c_str(),
        static_cast<unsigned>(_port),
        timeoutMs,
        _connected ? 1 : 0,
        static_cast<long long>(_socket));

    // std::lock_guard<std::mutex> lk(_socketMtx);

    // if (_connected.load() && _socket != INVALID_SOCKET) {
    //     return true;
    // }
    // if (!_wsaInited && !InitWinsock()) {
    //     return false;
    // }

    _socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_socket == INVALID_SOCKET) 
    {
        _lastError = ::WSAGetLastError();
        LOG_ERROR("[NetWorker] socket creation failed err=%d", _lastError);
        return false;
    }

    int nodelay = 1;
    ::setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

    unsigned long nonBlocking = 1;
    if (::ioctlsocket(_socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) 
    {
        _lastError = ::WSAGetLastError();
        LOG_ERROR("[NetWorker] ioctlsocket(FIONBIO) failed err=%d socket=%lld",
            _lastError,
            static_cast<long long>(_socket));
        CloseSocket();
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    if (::inet_pton(AF_INET, _host.c_str(), &addr.sin_addr) != 1) 
    {
        _lastError = WSAEINVAL;
        LOG_ERROR("[NetWorker] inet_pton failed host=%s err=%d", _host.c_str(), _lastError);
        CloseSocket();
        return false;
    }

    int ret = ::connect(_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (ret == SOCKET_ERROR) 
    {
        int err = ::WSAGetLastError();
        LOG_WARNING("[NetWorker] connect returned SOCKET_ERROR err=%d socket=%lld", err, static_cast<long long>(_socket));
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEINVAL) 
        {
            _lastError = err;
            LOG_ERROR("[NetWorker] connect failed immediately err=%d", _lastError);
            CloseSocket();
            return false;
        }

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(_socket, &writeSet);

        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        int result = ::select(0, nullptr, &writeSet, nullptr, &tv);
        if (result <= 0 || !FD_ISSET(_socket, &writeSet)) 
        {
            _lastError = (result == 0 ? WSAETIMEDOUT : ::WSAGetLastError());
            LOG_ERROR("[NetWorker] connect select failed result=%d err=%d socketReady=%d",
                result,
                _lastError,
                FD_ISSET(_socket, &writeSet) ? 1 : 0);
            CloseSocket();
            return false;
        }

        int error = 0;
        int errorLen = sizeof(error);
        if (::getsockopt(_socket, SOL_SOCKET, SO_ERROR, (char*)(&error), &errorLen) == SOCKET_ERROR || error != 0) 
        {
            _lastError = (error != 0 ? error : ::WSAGetLastError());
            LOG_ERROR("[NetWorker] getsockopt(SO_ERROR) indicates connect failure err=%d", _lastError);
            CloseSocket();
            return false;
        }
    }

    _lastError = 0;
    _connected = true;
    LOG_INFO("[NetWorker] ConnectSocket succeeded host=%s port=%u socket=%lld",
        _host.c_str(),
        static_cast<unsigned>(_port),
        static_cast<long long>(_socket));
    return true;
}

void NetWorker::CloseSocket() 
{
    // std::lock_guard<std::mutex> lk(_socketMtx);
    if (_socket == INVALID_SOCKET) 
    {
        return;
    }
    LOG_INFO("[NetWorker] closing socket=%lld connected=%d", static_cast<long long>(_socket), _connected ? 1 : 0);
    ::shutdown(_socket, SD_BOTH);
    ::closesocket(_socket);
    _socket = INVALID_SOCKET;   
    _connected = false;
    LOG_INFO("[NetWorker] socket closed");
}

bool NetWorker::IsSocketConnected() const {
    return _connected;
}

int32_t NetWorker::NextSeq() {
    return _seq++;
}

void NetWorker::loop() 
{
    while (_running) 
    {
        if (!ensureConnected()) 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryDelayMs));
            continue;
        }

        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (nowSec - _lastHeartbeatSec >= kHeartbeatSec) 
        {
            // heartbeat
            // todo
        } 

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(_socket, &readSet);

        fd_set writeSet;
        FD_ZERO(&writeSet);
        bool needWrite = false;
        {
            std::lock_guard<std::mutex> lock(_sendBufferMutex);
            needWrite = !_sendBuffer.empty();
        }
        if (needWrite) 
        {
            FD_SET(_socket, &writeSet);
        }

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = kLoopSleepMs * 1000;

        int ret = ::select(0, &readSet, needWrite ? &writeSet : nullptr, nullptr, &tv); // 超时时间10毫秒
        if (ret == SOCKET_ERROR) 
        {
            LOG_ERROR("[NetWorker] select failed err=%d needWrite=%d socket=%lld",
                ::WSAGetLastError(),
                needWrite ? 1 : 0,
                static_cast<long long>(_socket));
            CloseSocket();
            continue;
        }
        if (ret == 0) // 超时
        {
            continue;
        }
        if (needWrite && FD_ISSET(_socket, &writeSet)) 
        {
            procSend();
        }
        if (FD_ISSET(_socket, &readSet)) 
        {
            procRecv();
        }

        if (!_recvBuffer.empty())
        {
            procPacket();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kLoopSleepMs));
    }
}

bool NetWorker::ensureConnected() {
    if (_connected)
    {
        return true;
    }
    LOG_INFO("[NetWorker] ensureConnected trying to connect host=%s port=%u", _host.c_str(), static_cast<unsigned>(_port));
    if (!ConnectSocket(kConnectTimeoutMs)) 
    {
        LOG_ERROR("[NetWorker] ensureConnected failed host=%s port=%u err=%d",
            _host.c_str(),
            static_cast<unsigned>(_port),
            _lastError);
        std::unique_lock<std::mutex> lock(_callbacksMutex);
        if (_callbacks.onNetworkError)
        {
            std::ostringstream oss;
            oss << "connect failed, winsock error=" << _lastError;
            _callbacks.onNetworkError(oss.str());
        }
        return false;
    }

    std::unique_lock<std::mutex> lock(_callbacksMutex);
    if (_callbacks.onConnectedChanged)
    {
        _callbacks.onConnectedChanged(true);
    }
    return true;
}

void NetWorker::procSend() 
{
    int ret = 0;
    while (true) 
    {
        std::string pending;
        {
            std::lock_guard<std::mutex> lock(_sendBufferMutex);
            if (_sendBuffer.empty())
            {
                return;
            }
            pending = _sendBuffer;
        }
        LOG_INFO("[NetWorker] sending bytes=%u socket=%lld", static_cast<unsigned>(pending.size()), static_cast<long long>(_socket));
        ret = ::send(_socket, pending.data(), static_cast<int>(pending.size()), 0);

        if (ret == SOCKET_ERROR)
        {
            int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
            {
                break;
            }
            else
            {
                LOG_ERROR("[NetWorker] send failed err=%d pendingBytes=%u", err, static_cast<unsigned>(pending.size()));
                CloseSocket();
                break;
            }
        }
        else if (ret < 1)
        {
            LOG_ERROR("[NetWorker] send returned invalid byte count=%d", ret);
            CloseSocket();
            break;
        }
        LOG_INFO("[NetWorker] send succeeded sentBytes=%d", ret);

        {
            std::lock_guard<std::mutex> lock(_sendBufferMutex);
            if (ret >= static_cast<int>(_sendBuffer.size()))
            {
                _sendBuffer.clear();
            }
            else
            {
                _sendBuffer.erase(0, ret);
            }
            if (_sendBuffer.empty())
            {
                break;
            }
        }
    }
}

void NetWorker::procRecv() 
{
    int  ret = 0;
    char buff[10 * 1024];
    while (true)
    {
        ret = ::recv(_socket, buff, sizeof(buff), 0);
        if (ret == SOCKET_ERROR)
        {
            int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
            {
                break;
            }
            else
            {
                LOG_ERROR("[NetWorker] recv failed err=%d", err);
                CloseSocket();
                break;
            }
        }
        else if (ret < 1)
        {
            LOG_WARNING("[NetWorker] recv indicates peer closed connection ret=%d", ret);
            CloseSocket();
            break;
        }
        _recvBuffer.append(buff, ret);
        LOG_INFO("[NetWorker] recv appended bytes=%d totalRecvBufferBytes=%u",
            ret,
            static_cast<unsigned>(_recvBuffer.size()));
        ::Sleep(1);
    }
    // decodePacket();
}

// void NetWorker::decodePacket() 
// {
//     while (true)
//     {   
//         if (_recvBuffer.size() < sizeof(MsgHeader))
//         {
//             break;
//         }
//         MsgHeader header;
//         memcpy(&header, _recvBuffer.data(), sizeof(MsgHeader));
//         if (header.compressflag == 1)
//         {
//             if (_recvBuffer.size() < sizeof(MsgHeader) + header.compresssize)
//             {
//                 break;
//             }
//             _recvBuffer.erase(0, sizeof(MsgHeader));

//             std::string compressedMsg;
//             compressedMsg.assign(_recvBuffer.data(), header.compresssize);
//             _recvBuffer.erase(0, header.compresssize);

//             std::string uncompressedMsg;
//             if (!Uncompress(compressedMsg, uncompressedMsg, header.compresssize))
//             {
//                 // log
//                 break;
//             }
//             // do something
//         }
//         else
//         {
//             // 默认压缩
//             // todo
//         }
//     } // end while
// }

void NetWorker::procPacket()
{
    while (true)
    {
        if (_recvBuffer.size() < sizeof(MsgHeader))
        {
            break;
        }
        MsgHeader header;
        memcpy(&header, _recvBuffer.data(), sizeof(MsgHeader));
        LOG_INFO("[NetWorker] packet header compressFlag=%u originSize=%u compressedSize=%u recvBufferBytes=%u",
            static_cast<unsigned>(header.compressflag),
            static_cast<unsigned>(header.originsize),
            static_cast<unsigned>(header.compresssize),
            static_cast<unsigned>(_recvBuffer.size()));
        if (header.compressflag == 1)
        {
            if (_recvBuffer.size() < sizeof(MsgHeader) + header.compresssize)
            {
                break;
            }
            _recvBuffer.erase(0, sizeof(MsgHeader));
            std::string compressedMsg;
            compressedMsg.assign(_recvBuffer.data(), header.compresssize);
            _recvBuffer.erase(0, header.compresssize);
            std::string uncompressedMsg;
            if (!Decompress(compressedMsg, uncompressedMsg, header.originsize))
            {
                LOG_ERROR("[NetWorker] packet decompress failed compressedBytes=%u originSize=%u",
                    static_cast<unsigned>(compressedMsg.size()),
                    static_cast<unsigned>(header.originsize));
                break;
            }
            LOG_INFO("[NetWorker] packet decompressed successfully uncompressedBytes=%u remainingRecvBytes=%u",
                static_cast<unsigned>(uncompressedMsg.size()),
                static_cast<unsigned>(_recvBuffer.size()));


            DispatchPacket(uncompressedMsg);
        }
        else
        {
            LOG_WARNING("[NetWorker] unsupported packet compressFlag=%u remainingRecvBytes=%u",
                static_cast<unsigned>(header.compressflag),
                static_cast<unsigned>(_recvBuffer.size()));
            break;
        }
    }
}

void NetWorker::DispatchPacket(const std::string& msg)
{
    int32_t cmd = 0;
    int32_t seq = 0;
    BinaryStreamReader reader(msg.data(), msg.size());

    if (!reader.ReadInt32(cmd))
    {
        LOG_ERROR("NetWorker::DispatchPacket read cmd failed");
        return;
    }
    if (!reader.ReadInt32(_seq))
    {
        LOG_ERROR("NetWorker::DispatchPacket read seq failed");
        return;
    }

    std::string json;
    size_t len = 0;
    if (!reader.ReadString(&json, 1024, len))
    {
        LOG_ERROR("NetWorker::DispatchPacket read data failed");
        return;
    }

    LOG_INFO("NetWorker::DispatchPacket parsed cmd=%d seq=%d json=%s", cmd, seq, json.c_str());
    switch (cmd)
    {
        case msg_type_register:
            DispatchRegisterResult(json);
            break;
        case msg_type_login:
            DispatchLoginResult(json);
            break;
        //case msg_type_logout:
        //    break;
        case msg_type_search_friend:
            DispatchSearchFriendResult(json);
            break;
        case msg_type_operate_friend:
            DispatchOperateFriend(json);
            break;
        case msg_type_chat_msg:
        {
            int32_t fromUserId = 0;
            if (!reader.ReadInt32(fromUserId))
            {
                LOG_ERROR("NetWorker::DispatchPacket read chat message sender failed, seq=%d data=%s",
                    _seq,
                    json.c_str());
                return;
            }
            DispatchChatMessage(json, fromUserId);
            break;
        }
        case msg_type_heartbeat:
            break;
        case msg_type_get_friendlist:
            DispatchGetFriendListResult(json);
            break;
        default:
            LOG_ERROR("");
            break;
    }
}

void NetWorker::DispatchRegisterResult(const std::string& json)
{
    int result = 0;
    std::string message;

    if (!ExtractJsonIntField(json, "result", result))
    {
        LOG_ERROR("NetWorker::DispatchRegisterResult parse result failed, data: %s", json.c_str());
        return;
    }

    if (!ExtractJsonStringField(json, "message", message))
    {
        LOG_ERROR("NetWorker::DispatchRegisterResult parse message failed, data: %s", json.c_str());
        return;
    }
    LOG_INFO("NetWorker::DispatchRegisterResult parse result=%d message=%s", result, message.c_str());
    _callbacks.onRegisterFinished(result, message);
}

void NetWorker::DispatchLoginResult(const std::string& json)
{
    int result = 0;
    std::string message;
    UserInfo    user;

    if (!ExtractJsonIntField(json, "result", result))
    {
        LOG_ERROR("NetWorker::DispatchRegisterResult parse result failed, data: %s", json.c_str());
        return;
    }

    if (result == 0)
    {
        int32_t     userId = 0;
        std::string username;
        std::string nickname;
        
        if (!ExtractJsonIntField(json, "userid", userId))
        {
            LOG_ERROR("NetWorker::DispatchLoginResult parse userId failed, data: %s", json.c_str());
            return;
        }
        if (!ExtractJsonStringField(json, "username", username))
        {
            LOG_ERROR("NetWorker::DispatchLoginResult parse username failed, data: %s", json.c_str());
            return;
        }
        if (!ExtractJsonStringField(json, "nickname", nickname))
        {
            LOG_ERROR("NetWorker::DispatchLoginResult parse nickname failed, data: %s", json.c_str());
            return;
        }
        LOG_INFO("NetWorker::DispatchLoginResult parse userId=%d username=%s nickname=%s", userId, username.c_str(), nickname.c_str());
        user = { userId, username, nickname };
        _callbacks.onLoginFinished(result, message, user);
    }

    else
    {
        if (!ExtractJsonStringField(json, "message", message))
        {
            LOG_ERROR("NetWorker::DispatchLoginResult parse message failed, data: %s", json.c_str());
            return;
        }
        LOG_INFO("NetWorker::DispatchLoginResult parse message=%s", message.c_str());
        _callbacks.onLoginFinished(result, message, user);
    }
}

void NetWorker::DispatchSearchFriendResult(const std::string& json)
{
    int      result = 0;
    UserInfo user{};

    if (!ExtractJsonIntField(json, "result", result))
    {
        LOG_ERROR("NetWorker::DispatchSearchFriendResult parse result failed, data: %s", json.c_str());
        return;
    }

    if (result == 0)
    {
        int32_t     userId = 0;
        std::string username;
        std::string nickname;

        if (!ExtractJsonIntField(json, "userid", userId))
        {
            LOG_ERROR("NetWorker::DispatchSearchFriendResult parse userId failed, data: %s", json.c_str());
            return;
        }
        if (!ExtractJsonStringField(json, "username", username))
        {
            LOG_ERROR("NetWorker::DispatchSearchFriendResult parse username failed, data: %s", json.c_str());
            return;
        }
        if (!ExtractJsonStringField(json, "nickname", nickname))
        {
            LOG_ERROR("NetWorker::DispatchSearchFriendResult parse nickname failed, data: %s", json.c_str());
            return;
        }

        user = { userId, username, nickname };
        LOG_INFO("NetWorker::DispatchSearchFriendResult parse result=%d userId=%d username=%s nickname=%s",
            result,
            userId,
            username.c_str(),
            nickname.c_str());
    }
    else
    {
        LOG_INFO("NetWorker::DispatchSearchFriendResult parse result=%d", result);
    }

    _callbacks.onSearchFriendFinished(result, user);
}

void NetWorker::DispatchGetFriendListResult(const std::string& json)
{
    // 好友列表是一个全量 friends 数组。这里逐项解析后复用现有“好友到达”回调链路，
    // 这样上层可以继续沿用去重、缓存和 UI 同步逻辑，不必再维护一套新入口。
    Json::CharReaderBuilder readerBuilder;
    Json::Value root;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
    if (!reader->parse(json.data(), json.data() + json.size(), &root, &errs))
    {
        LOG_ERROR("NetWorker::DispatchGetFriendListResult parse json failed, errs: %s, data: %s",
            errs.c_str(),
            json.c_str());
        return;
    }

    const Json::Value& friends = root["friends"];
    if (!friends.isArray())
    {
        LOG_ERROR("NetWorker::DispatchGetFriendListResult field friends is not array, data: %s",
            json.c_str());
        return;
    }

    LOG_INFO("NetWorker::DispatchGetFriendListResult friends count=%d",
        static_cast<int>(friends.size()));

    for (Json::ArrayIndex i = 0; i < friends.size(); ++i)
    {
        const Json::Value& friendItem = friends[i];
        if (!friendItem.isObject())
        {
            LOG_WARNING("NetWorker::DispatchGetFriendListResult skip non-object item index=%u",
                static_cast<unsigned>(i));
            continue;
        }

        const Json::Value& userIdValue = friendItem["userid"];
        const Json::Value& usernameValue = friendItem["username"];
        const Json::Value& nicknameValue = friendItem["nickname"];
        if (!userIdValue.isInt() || !usernameValue.isString() || !nicknameValue.isString())
        {
            LOG_WARNING("NetWorker::DispatchGetFriendListResult skip invalid friend item index=%u",
                static_cast<unsigned>(i));
            continue;
        }

        UserInfo user;
        user.userId = static_cast<int32_t>(userIdValue.asInt());
        user.username = usernameValue.asString();
        user.nickname = nicknameValue.asString();

        LOG_INFO("NetWorker::DispatchGetFriendListResult friend[%u] userId=%d username=%s nickname=%s",
            static_cast<unsigned>(i),
            user.userId,
            user.username.c_str(),
            user.nickname.c_str());

        // 复用现有“好友到达”回调链路，交给上层做去重与 UI 同步。
        _callbacks.onAddFriendResponseReceived(user);
    }
}

void NetWorker::DispatchChatMessage(const std::string& json, int32_t fromUserId)
{
    // 服务端 chat_msg 当前格式：
    // cmd -> seq -> data(string/json) -> sender(int32)
    // 其中 data 里主要承载消息正文和时间，sender 单独跟在 string 后面。
    Json::CharReaderBuilder readerBuilder;
    Json::Value root;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
    if (!reader->parse(json.data(), json.data() + json.size(), &root, &errs))
    {
        LOG_ERROR("NetWorker::DispatchChatMessage parse json failed, errs: %s, data: %s",
            errs.c_str(),
            json.c_str());
        return;
    }

    ChatMsg msg{};
    msg.fromUserId = fromUserId;

    const Json::Value& textValue = root["msg"];
    if (!textValue.isString())
    {
        LOG_ERROR("NetWorker::DispatchChatMessage parse msg failed, data: %s", json.c_str());
        return;
    }
    msg.text = textValue.asString();

    const Json::Value& timestampValue = root["timestamp"];
    if (timestampValue.isInt64() || timestampValue.isInt())
    {
        msg.timestamp = static_cast<int64_t>(timestampValue.asInt64());
    }

    const Json::Value& toUserIdValue = root["targetid"];
    if (toUserIdValue.isInt())
    {
        msg.toUserId = static_cast<int32_t>(toUserIdValue.asInt());
    }

    LOG_INFO("NetWorker::DispatchChatMessage parsed fromUserId=%d toUserId=%d textLen=%u timestamp=%lld",
        msg.fromUserId,
        msg.toUserId,
        static_cast<unsigned>(msg.text.size()),
        static_cast<long long>(msg.timestamp));

    if (_callbacks.onMessageReceived)
    {
        _callbacks.onMessageReceived(msg);
    }
}

void NetWorker::DispatchOperateFriend(const std::string& json)
{
    int32_t type = -1;

    if (!ExtractJsonIntField(json, "type", type))
    {
        LOG_ERROR("NetWorker::DispatchOperateFriendResult parse type failed, data: %s", json.c_str());
        return;
    }

    if (type == OPERATE_FRIEND_TYPE_ADD_REQUEST)
    {
        int32_t     peerUserId = 0;
        std::string peerUsername;
        std::string peerNickname;
        if (!ExtractJsonIntField(json, "userid", peerUserId))
        {
            LOG_ERROR("NetWorker::DispatchOperateFriendResult parse peerUserId failed, data: %s", json.c_str());
            return;
        }
        if (!ExtractJsonStringField(json, "username", peerUsername))
        {
            LOG_ERROR("NetWorker::DispatchOperateFriendResult parse peerUsername failed, data: %s", json.c_str());
            return;
        }
        if (!ExtractJsonStringField(json, "nickname", peerNickname))
        {
            LOG_ERROR("NetWorker::DispatchOperateFriendResult parse peerNickname failed, data: %s", json.c_str());
            return;
        }
        LOG_INFO("NetWorker::DispatchOperateFriendResult add friend request received, peerUserId=%d peerUsername=%s peerNickname=%s",
            peerUserId,
            peerUsername.c_str(),
            peerNickname.c_str());
        UserInfo peerUser = { peerUserId, peerUsername, peerNickname };
        _callbacks.onAddFriendRequestReceived(peerUser);
    }
    else if (type == OPERATE_FRIEND_TYPE_ADD_RESPONSE)
    {
        int32_t        peerUserId = 0;
        std::string    peerUsername;
        std::string    peerNickname;
        if (!ExtractJsonIntField(json, "userid", peerUserId))
        {
            LOG_ERROR("NetWorker::DispatchOperateFriendResult parse peerUserId failed, data: %s", json.c_str());
            return;
        }
        if (!ExtractJsonStringField(json, "username", peerUsername))
        {
            LOG_ERROR("NetWorker::DispatchOperateFriendResult parse peerUsername failed, data: %s", json.c_str());
            return;
        }
        if (!ExtractJsonStringField(json, "nickname", peerNickname))
        {
            LOG_ERROR("NetWorker::DispatchOperateFriendResult parse peerNickname failed, data: %s", json.c_str());
            return;
        }
        LOG_INFO("NetWorker::DispatchOperateFriendResult add friend response received, peerUserId=%d peerUsername=%s peerNickname=%s",
            peerUserId,
            peerUsername.c_str(),
            peerNickname.c_str());
        UserInfo peerUser = { peerUserId, peerUsername, peerNickname };
        _callbacks.onAddFriendResponseReceived(peerUser);
    }
    else if (type == OPERATE_FRIEND_TYPE_DELETE_REQUEST)
    {
        int32_t     peerUserId = 0;
        std::string peerUsername;
        std::string peerNickname;
        if (!ExtractJsonIntField(json, "userid", peerUserId))
        {
            LOG_ERROR("NetWorker::DispatchOperateFriendResult parse peerUserId failed, data: %s", json.c_str());
            return;
        }
        LOG_INFO("NetWorker::DispatchOperateFriendResult delete friend request received, peerUserId=%d", peerUserId);
        _callbacks.onDeleteFriendRequestReceived(peerUserId);
    }
}