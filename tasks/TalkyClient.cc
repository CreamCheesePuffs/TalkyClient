#include "TalkyClient.h"
#include "NetWorker.h"
#include "utils/IULog.h"
#include <QMessageBox>
#include <utility>

inline QString ToQString(const std::string& str)
{
    return QString::fromStdString(str);
}

TalkyClient::TalkyClient(QObject* parent)
    : QObject(parent)
{
    setupWorkerCallbacks(); 
    setupSignalHandlers();
}

TalkyClient::~TalkyClient()
{
    _worker.Stop();
}

void TalkyClient::setupWorkerCallbacks()
{
    NetWorker::Callbacks cbs;

    cbs.onConnectedChanged = [this](bool connected) {
        auto notifyConnectedChanged = [this, connected]() {
            emit connectedChanged(connected);
            };

        QMetaObject::invokeMethod(this, notifyConnectedChanged, 
            Qt::QueuedConnection);
        };

    cbs.onNetworkError = [this](const std::string& msg) {
        auto notifyNetworkError = [this, msg]() {
            emit networkError(ToQString(msg));
            };

        QMetaObject::invokeMethod(this, notifyNetworkError, Qt::QueuedConnection);
        };

    cbs.onRegisterFinished = [this](int code, const std::string& msg) {
        auto notifyRegisterFinished = [this, code, msg]() {
            emit registerFinished(code, ToQString(msg));
            };

        QMetaObject::invokeMethod(this, notifyRegisterFinished, Qt::QueuedConnection);
        };

    cbs.onLoginFinished = [this](int code, const std::string& msg, const UserInfo& user) {
        auto notifyLoginFinished = [this, code, msg, user]() {
            emit loginFinished(
                code,
                ToQString(msg),
                user.userId,
                ToQString(user.username),
                ToQString(user.nickname));
            };

        QMetaObject::invokeMethod(this, notifyLoginFinished, Qt::QueuedConnection);
        };

    cbs.onAddFriendFinished = [this](int code, const std::string& msg, int targetUserId) {
        auto notifyAddFriendFinished = [this, code, msg, targetUserId]() {
            emit addFriendFinished(code, ToQString(msg), targetUserId);
            };

        QMetaObject::invokeMethod(this, notifyAddFriendFinished, Qt::QueuedConnection);
        };

    cbs.onMessageReceived = [this](const ChatMsg& m) {
        auto notifyMessageReceived = [this, m]() {
            emit messageReceived(
                m.fromUserId,
                m.toUserId,
                ToQString(m.text),
                m.timestamp);
            };

        QMetaObject::invokeMethod(this, notifyMessageReceived, Qt::QueuedConnection);
        };

    _worker.SetCallbacks(std::move(cbs));
}

void TalkyClient::setupSignalHandlers()
{
    connect(this, &TalkyClient::connectedChanged, this,
        [this](bool connected) {
            if (!connected)
            {
                if (_login)
                    _login->setSubmitting(false);

                if (_signin)
                    _signin->setSubmitting(false);

                QMessageBox::warning(nullptr, QStringLiteral("连接断开"),
                    QStringLiteral("与服务器的连接已断开"));
            }
        });

    connect(this, &TalkyClient::networkError, this,
        [this](const QString& msg) {

            QMessageBox::warning(nullptr, QStringLiteral("网络错误"), msg);

            if (_login)
                _login->setSubmitting(false);

            if (_signin)
                _signin->setSubmitting(false);
        });

    connect(this, &TalkyClient::registerFinished, this,
        [this](int code, const QString& msg) {

            if (_signin)
                _signin->setSubmitting(false);

            if (code == 0)
            {
                QMessageBox::information(nullptr, QStringLiteral("注册成功"), msg);
                showLogin();
            }
            else
            {
                QMessageBox::warning(nullptr, QStringLiteral("注册失败"), msg);
            }
        });

    connect(this, &TalkyClient::loginFinished, this,
        [this](int code,
            const QString& msg,
            int userId,
            const QString& username,
            const QString& nickname) {

                Q_UNUSED(username);

                if (_login)
                    _login->setSubmitting(false);

                if (code == 0)
                {
                    QMessageBox::information(nullptr, QStringLiteral("登录成功"), msg);

                    showMainWindow();

                    if (_mainWindow)
                    {
                        _mainWindow->setCurrentUser(userId, nickname);
                    }
                }
                else
                {
                    QMessageBox::warning(nullptr, QStringLiteral("登录失败"), msg);
                }
        });

    connect(this, &TalkyClient::addFriendFinished, this,
        [this](int code, const QString& msg, int targetUserId) {

            Q_UNUSED(targetUserId);

            if (code == 0)
            {
                QMessageBox::information(nullptr, QStringLiteral("添加好友成功"), msg);

                if (_mainWindow)
                {
                    _mainWindow->refreshFriendList();
                }
            }
            else
            {
                QMessageBox::warning(nullptr, QStringLiteral("添加好友失败"), msg);
            }
        });

    connect(this, &TalkyClient::messageReceived, this,
        [this](int fromUserId,
            int toUserId,
            const QString& text,
            qint64 timestamp) {

                if (_mainWindow)
                {
                    _mainWindow->appendMessage(
                        fromUserId,
                        toUserId,
                        text,
                        timestamp);
                }
        });
}


void TalkyClient::start(const QString& host, quint16 port)
{
    _worker.SetServer(host.toUtf8().toStdString(), static_cast<uint16_t>(port));
    _worker.Start();
    launch();
}

void TalkyClient::stop()
{
    _worker.Stop();
}

void TalkyClient::registerUser(const QString& username, const QString& nickname, const QString& password)
{
    // todo  worker
    _worker.Register(username.toStdString(), nickname.toStdString(), password.toStdString());
}

void TalkyClient::login(const QString& username, const QString& password, int status)
{
    _worker.Login(username.toStdString(), password.toStdString(), status);
}

void TalkyClient::addFriend(int targetUserId)
{
    _worker.AddFriend(targetUserId);
}

void TalkyClient::sendTextMessage(int toUserId, const QString& text)
{
    _worker.SendText(toUserId, text.toStdString());
}

void TalkyClient::launch()
{
    showLogin();
}

void TalkyClient::showLogin()
{
    if (!_login)
    {
        _login = std::make_unique<Login>();
        connect(_login.get(), &Login::registerRequested, this, &TalkyClient::showSignin); // 点击注册，切换到注册界面
    }

    _login->show();
    _login->raise();
    _login->activateWindow();
}

void TalkyClient::onLoginRequested(const QString& username, const QString& password, int status)
{
    _worker.Login(username.toStdString(), password.toStdString(), status);
}

void TalkyClient::showSignin()
{

    if (!_signin)
    {
        _signin = std::make_unique<Signin>();
        //connect(_signin.get(), &Signin::closeRequested, this, &TalkyClient::showLogin);
        connect(_signin.get(), &Signin::registerSubmitted,
            this,
            [this](const QString& username, const QString& nickname, const QString& password) {
                _signin->setSubmitting(true);
                registerUser(username, nickname, password);
            });
    }

    //if (_login)
    //{
    //    _login->hide();
    //}

    _signin->show();
    _signin->raise();
    _signin->activateWindow();
}

void TalkyClient::showMainWindow()
{
    if (!_mainWindow)
    {
        _mainWindow = std::make_unique<MainWindow>();
        
        connect(_mainWindow.get(), &MainWindow::addFriendRequested, this, &TalkyClient::showSearchWindow); // 点击注册，切换到注册界面
    }

    if (_login)
    {
        _login->hide();
    }

    _mainWindow->show();
    _mainWindow->raise();
    _mainWindow->activateWindow();
}

void TalkyClient::showSearchWindow() 
{
    _searchFriend = std::make_unique<SearchFriend>();

    _searchFriend->show();
    _searchFriend->raise();
    _searchFriend->activateWindow();
}
