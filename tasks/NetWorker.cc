// NetWorker.cpp
#include "NetWorker.h"
#include "utils/IULog.h"
#include "utils/ProtocolStream.h"
#include "utils/Zlib.h"
#include "jsoncpp-1.9.0/json.h"


#include <chrono>
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

}

NetWorker::NetWorker()
{

}

NetWorker::~NetWorker() {
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
    return std::string("{\"userid\":") + std::to_string(targetUserId) +
        ",\"type\":" + std::to_string(operationType) + "}";
}

void NetWorker::Register(const std::string& username, const std::string& nickname, const std::string& password)
{
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
        // log
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
}

void NetWorker::Login(const std::string& username, const std::string& password, int32_t status)
{
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
        // log
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
}

void NetWorker::SearchFriend(const std::string& username)
{
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
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
}

void NetWorker::AddFriend(int32_t targetUserId)
{
    std::string body = MakeOperateFriendJson(targetUserId, 1);

    std::string raw;
    BinaryStreamWriter writer(&raw);
    writer.WriteInt32(static_cast<int32_t>(msg_type_operate_friend));
    writer.WriteInt32(0);
    writer.WriteString(body);
    writer.Flush();

    std::string compressed;
    if (!Compress(raw, compressed))
    {
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
}

void NetWorker::DelFriend(int32_t targetUserId)
{
    std::string body = MakeOperateFriendJson(targetUserId, 4);

    std::string raw;
    BinaryStreamWriter writer(&raw);
    writer.WriteInt32(static_cast<int32_t>(msg_type_operate_friend));
    writer.WriteInt32(0);
    writer.WriteString(body);
    writer.Flush();

    std::string compressed;
    if (!Compress(raw, compressed))
    {
        return;
    }

    MsgHeader header{};
    header.compressflag = 1;
    header.originsize = raw.size();
    header.compresssize = compressed.size();

    std::lock_guard<std::mutex> lock(_sendBufferMutex);
    _sendBuffer.append((char*)(&header), sizeof(header));
    _sendBuffer.append(compressed);
}

void NetWorker::SendText(int32_t toUserId, const std::string& text)
{
    (void)toUserId;
    (void)text;
}

void NetWorker::SetServer(std::string host, uint16_t port) 
{
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
    if (_running)
    {
        return;
    }
    if (!_wsaInited && !InitWinsock())
    {
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
        return false;
    }
    _wsaInited = true;
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
}

bool NetWorker::ConnectSocket(int timeoutMs) 
{

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
        // log err
        return false;
    }

    int nodelay = 1;
    ::setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

    unsigned long nonBlocking = 1;
    if (::ioctlsocket(_socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) 
    {
        // log err
        CloseSocket();
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    if (::inet_pton(AF_INET, _host.c_str(), &addr.sin_addr) != 1) 
    {
        CloseSocket();
        return false;
    }

    int ret = ::connect(_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (ret == SOCKET_ERROR) 
    {
        int err = ::WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEINVAL) 
        {
            // log err
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
            CloseSocket();
            return false;
        }

        int error = 0;
        int errorLen = sizeof(error);
        if (::getsockopt(_socket, SOL_SOCKET, SO_ERROR, (char*)(&error), &errorLen) == SOCKET_ERROR || error != 0) 
        {
            // log err
            CloseSocket();
            return false;
        }
    }

    _connected = true;
    return true;
}

void NetWorker::CloseSocket() 
{
    // std::lock_guard<std::mutex> lk(_socketMtx);
    if (_socket == INVALID_SOCKET) 
    {
        return;
    }
    ::shutdown(_socket, SD_BOTH);
    ::closesocket(_socket);
    _socket = INVALID_SOCKET;   
    _connected = false;
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
    if (!ConnectSocket(kConnectTimeoutMs)) 
    {
        std::unique_lock<std::mutex> lock(_callbacksMutex);
        if (_callbacks.onNetworkError)
        {
            _callbacks.onNetworkError("connect failed");
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
        ret = ::send(_socket, pending.data(), static_cast<int>(pending.size()), 0);

        if (ret == SOCKET_ERROR)
        {
            if (::WSAGetLastError() == WSAEWOULDBLOCK)
            {
                break;
            }
            else
            {
                // log
                CloseSocket();
                break;
            }
        }
        else if (ret < 1)
        {
            // log
            CloseSocket();
            break;
        }

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
            if (::WSAGetLastError() == WSAEWOULDBLOCK)
            {
                break;
            }
            else
            {
                // log
                CloseSocket();
                break;
            }
        }
        else if (ret < 1)
        {
            // log
            CloseSocket();
            break;
        }
        _recvBuffer.append(buff, ret);
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
                // log
                break;
            }
            // do something
            
        }
    }
}

// std::string NetWorker::Codec::Encode(const Packet& p) const {
//     uint32_t bodyLen = static_cast<uint32_t>(sizeof(int32_t) + sizeof(int32_t) + p.payload.size());
//     std::string out(sizeof(uint32_t) + bodyLen, '\0');

//     size_t off = 0;
//     std::memcpy(out.data() + off, &bodyLen, sizeof(uint32_t)); off += sizeof(uint32_t);
//     std::memcpy(out.data() + off, &p.cmd, sizeof(int32_t)); off += sizeof(int32_t);
//     std::memcpy(out.data() + off, &p.seq, sizeof(int32_t)); off += sizeof(int32_t);
//     if (!p.payload.empty()) {
//         std::memcpy(out.data() + off, p.payload.data(), p.payload.size());
//     }
//     return out;
// }

// void NetWorker::Codec::Feed(const char* data, size_t size) {
//     if (!data || size == 0) return;
//     _buffer.append(data, size);
// }

// std::vector<Packet> NetWorker::Codec::DecodeAll() {
//     std::vector<Packet> out;
//     constexpr size_t kHeader = sizeof(uint32_t) + sizeof(int32_t) + sizeof(int32_t);

//     while (true) {
//         if (_buffer.size() < kHeader) break;

//         uint32_t bodyLen = 0;
//         std::memcpy(&bodyLen, _buffer.data(), sizeof(uint32_t));
//         if (bodyLen < sizeof(int32_t) + sizeof(int32_t)) { _buffer.clear(); break; }
//         if (_buffer.size() < sizeof(uint32_t) + bodyLen) break;//         Packet p;
//         size_t off = sizeof(uint32_t);
//         std::memcpy(&p.cmd, _buffer.data() + off, sizeof(int32_t)); off += sizeof(int32_t);
//         std::memcpy(&p.seq, _buffer.data() + off, sizeof(int32_t)); off += sizeof(int32_t);//         size_t payloadSize = bodyLen - sizeof(int32_t) - sizeof(int32_t);
//         if (payloadSize > 0) p.payload.assign(_buffer.data() + off, payloadSize);//         out.push_back(std::move(p));
//         _buffer.erase(0, sizeof(uint32_t) + bodyLen);
//     }
//     return out;
// }