#pragma once

#include <cstdint>
#include <string>

#pragma pack(push, 1)
//协议头
typedef struct MsgHeader {
    uint32_t  compressflag;     //压缩标志，如果为1，则启用压缩，反之不启用压缩
    uint32_t  originsize;       //包体压缩前大小
    uint32_t  compresssize;     //包体压缩后大小
    char      reserved[16];
} MsgHeader;
#pragma pack(pop)

enum msg_type : int32_t {
    msg_type_register       = 1001,
    msg_type_login          = 1002,
    msg_type_logout         = 1003,
    msg_type_search_friend  = 1004,
    msg_type_operate_friend = 1005,
    msg_type_chat_msg       = 1006,
    msg_type_heartbeat      = 1007,
    msg_type_get_friendlist = 1008,
};

enum OPERATE_FRIEND_TYPE : int32_t
{
    OPERATE_FRIEND_TYPE_ADD_REQUEST = 0,
    OPERATE_FRIEND_TYPE_ADD_RESPONSE = 1,
    OPERATE_FRIEND_TYPE_DELETE_REQUEST = 2,
    OPERATE_FRIEND_TYPE_DELETE_RESPONSE = 3,
};

struct UserInfo {
    int32_t     userId{0};
    std::string username;
    std::string nickname;
};

struct ChatMsg {
    int32_t fromUserId{0};
    int32_t toUserId{0};
    std::string text;
    int64_t timestamp{0};
};

struct RegisterReq {
    std::string username, nickname, password;
};

struct LoginReq {
    std::string username, password;
    int32_t status{1};
};

struct AddFriendReq {
    int32_t targetUserId{0};
};

struct SendTextReq {
    int32_t toUserId{0};
    std::string text;
};
