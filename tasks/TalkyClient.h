#pragma once
#include "NetWorker.h"
#include "ui/Login.h"
#include "ui/Signin.h"
#include "ui/MainWindow.h"
#include "ui/SearchFriend.h"
#include <QObject>
#include <QMetaObject>
#include <QString>

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
    Q_INVOKABLE void addFriend(int targetUserId);
    Q_INVOKABLE void sendTextMessage(int toUserId, const QString& text);

signals:
    void connectedChanged(bool connected);
    void networkError(QString msg);
    void registerFinished(int code, QString msg);
    void loginFinished(int code, QString msg, int userId, QString username, QString nickname);
    void addFriendFinished(int code, QString msg, int targetUserId);
    void messageReceived(int fromUserId, int toUserId, QString text, qint64 ts);

private:
    void setupWorkerCallbacks();
    void setupSignalHandlers();

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
};