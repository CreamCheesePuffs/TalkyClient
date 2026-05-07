#pragma once
#include "NetWorker.h"
#include "ui/Login.h"
#include "ui/Signin.h"
#include "ui/MainWindow.h"
#include "ui/SearchFriend.h"
#include <QObject>
#include <QMetaObject>
#include <QString>
#include <vector>

class TalkyClient : public QObject {
    Q_OBJECT
public:
    explicit TalkyClient(QObject* parent = nullptr);
    ~TalkyClient();

    Q_INVOKABLE void start(const QString& host, quint16 port);
    Q_INVOKABLE void stop(); 
    Q_INVOKABLE void launch();

    Q_INVOKABLE void registerUser(const QString& username, const QString& nickname, const QString& password);
    Q_INVOKABLE void login(const QString& username, const QString& password, int status);
    Q_INVOKABLE void searchFriend(const QString& username);
    Q_INVOKABLE void addFriend(const UserInfo& user);
    Q_INVOKABLE void deleteFriend(int32_t userId);
    Q_INVOKABLE void acceptFriendRequest(const UserInfo& user);
    Q_INVOKABLE void rejectFriendRequest(const UserInfo& user);
    Q_INVOKABLE void sendTextMessage(int userId, int toUserId, const QString& text);

signals:
    void connectedChanged(bool connected);
    void networkError(QString msg);
    void registerFinished(int code, QString msg);
    void loginFinished(int code, const QString& msg, const UserInfo&);
    void searchFriendFinished(int code, const UserInfo& user);
    void addFriendFinished(int code, const UserInfo& user);
    void deleteFriendFinished(int code, const UserInfo& user);
    void addFriendRequestReceived(const UserInfo& user);
    void messageReceived(int fromUserId, int toUserId, QString text, qint64 ts);

private:
    void setupWorkerCallbacks();
    void setupSignalHandlers();
    void upsertFriend(const UserInfo& user);
    void removeFriend(int userId);
    void syncFriendListToMainWindow();

    void showLogin();
    void showSignin();
    void showMainWindow();
    void showSearchWindow();

    void onLoginRequested(const QString& username, const QString& password, int status);

private:
    NetWorker _worker;
    std::unique_ptr<Login> _login;
    std::unique_ptr<Signin> _signin;
    std::unique_ptr<MainWindow> _mainWindow;
    std::unique_ptr<SearchFriend> _searchFriend;
    std::vector<UserInfo> _friendList;
};