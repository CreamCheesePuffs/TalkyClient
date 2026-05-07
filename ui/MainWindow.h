#pragma once

#include <QMainWindow>
#include "ui_MainWindow.h"
#include <QDateTime>
#include <QHash>
#include <QVector>
#include "tasks/Msg.h"

class NotifyItem;
class FriendItem;

// ChatSummary：会话列表里一条 item 需要的数据
struct ChatSummary {
    QString peerId;        // 唯一标识（可用名字/账号）
    QString displayName;   // 昵称（nameLabel）
    QString lastPreview;   // 最后一条消息预览（previewLabel）
    QDateTime lastTime;    // 时间（timeLabel）
    int unreadCount = 0;   // 未读数（unreadBadge）
    QString avatarRes;     // 头像资源路径（:/images/xxx.png）
};

// SessionMessage 是右侧消息区的最小持久化单元。
// 不直接保存气泡 widget，而是保存业务字段，切换会话时再重新渲染 UI。
struct SessionMessage {
    int fromUserId = 0;
    int toUserId = 0;
    QString text;
    qint64 timestamp = 0;
};

// MainWindow 负责维护三类左侧列表（会话 / 好友 / 通知），并把右侧 chatPanel
// 作为“当前会话的渲染结果”来刷新。真正的会话消息状态保存在 _sessionMessages 中，
// 因此切换会话时只需要更新当前会话标识，再按保存的数据重新绘制右侧即可。
class MainWindow : public QMainWindow
{
    Q_OBJECT

    // 左侧列表处于哪种模式，决定 chatList 当前渲染的是会话摘要、好友还是通知。
    enum class ListMode {
        Session,
        Friend,
        Notify
    };

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void refreshFriendList();
    void setCurrentUser(const UserInfo& user);
    void addFriendItem(const UserInfo& user);
    void addNotifyItem(const UserInfo& user);
    bool acceptFriendRequestLocally(const QString& username);
    bool rejectFriendRequestLocally(int);
    void appendMessage(int fromUserId,
                    int toUserId,
                    const QString& text,
                    qint64 timestamp);

signals:
    void addFriendRequested();
    void deleteFriendRequested(int userId);
    void acceptFriendRequest(const UserInfo& user);
    void rejectFriendRequest(const UserInfo& user);
    void sendTextRequested(int userId, int toUserId, const QString& text);

private slots:
    void onChatSelected(QListWidgetItem* item);
    void onSendClicked();
    void onAddFriendRequest();
    void onCloseButtonClicked();

private:
    QString _currentPeer; // 当前聊天对象昵称
    int     _currentPeerUserId = 0;

    void addBubble(const QString& text, bool outgoing);
    // 根据当前选中的会话，把保存下来的消息重新渲染到右侧 messageList。
    void renderCurrentSessionMessages();

    static QString formatTimeForList(const QDateTime& dt);
    void   addChatItem(const ChatSummary& s);
    void   renderFriendItem(const UserInfo& user);
    void   renderNotifyItem(const UserInfo& user);
    void   renderCurrentList();
    void   switchListMode(ListMode mode);
    void   removeFriendItem(int userId);

private:
    Ui::MainWindowClass ui;
    UserInfo             _user;
    ListMode             _currentListMode = ListMode::Session;
    QVector<ChatSummary> _sessionList;
    QVector<UserInfo>    _friendList;
    QVector<UserInfo>    _notifyList;
    // key = 对端 userId；value = 该会话下的所有消息，用它作为右侧 chatPanel 的真实数据源。
    QHash<int, QVector<SessionMessage>> _sessionMessages;
};

