#pragma once

#include <QMainWindow>
#include "ui_MainWindow.h"
#include <QDateTime>

// ChatSummary：会话列表里一条 item 需要的数据
struct ChatSummary {
    QString peerId;        // 唯一标识（可用名字/账号）
    QString displayName;   // 昵称（nameLabel）
    QString lastPreview;   // 最后一条消息预览（previewLabel）
    QDateTime lastTime;    // 时间（timeLabel）
    int unreadCount = 0;   // 未读数（unreadBadge）
    QString avatarRes;     // 头像资源路径（:/images/xxx.png）
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void refreshFriendList();
    void setCurrentUser(int userId, const QString& nickname);
    void appendMessage(int fromUserId,
                    int toUserId,
                    const QString& text,
                    qint64 timestamp);

signals:
    void addFriendRequested();

private slots:
    void onChatSelected(QListWidgetItem* item);
    void onSendClicked();
    void onAddFriendRequest();
    void onCloseButtonClicked();

private:
    QString _currentPeer; // 当前聊天对象昵称

    void addBubble(const QString& text, bool outgoing);

    static QString formatTimeForList(const QDateTime& dt);
    void addChatItem(const ChatSummary& s);

    
    Ui::MainWindowClass ui;
};

