#include "TalkyClient.h"
#include "NetWorker.h"
#include "utils/IULog.h"
#include <QMessageBox>
#include <utility>

inline QString ToQString(const std::string& str)
{
    return QString::fromStdString(str);
}

inline std::string PreviewQString(const QString& text, int maxLen = 80)
{
    QString preview = text;
    if (preview.size() > maxLen)
    {
        preview = preview.left(maxLen) + QStringLiteral("...(truncated)");
    }
    return preview.toUtf8().toStdString();
}

TalkyClient::TalkyClient(QObject* parent)
    : QObject(parent)
{
    LOG_INFO("[TalkyClient] constructed parent=%p", parent);
    setupWorkerCallbacks(); 
    setupSignalHandlers();
}

TalkyClient::~TalkyClient()
{
    LOG_INFO("[TalkyClient] destructing login=%p signin=%p mainWindow=%p searchFriend=%p",
        _login.get(),
        _signin.get(),
        _mainWindow.get(),
        _searchFriend.get());
    _worker.Stop();
}

void TalkyClient::upsertFriend(const UserInfo& user)
{
    // TalkyClient 自己维护一份好友缓存，避免 UI 是否已创建影响好友状态。
    // 这样即使 MainWindow 还没显示，好友关系也能先落到内存中，之后再同步到界面。
    bool found = false;
    for (auto& item : _friendList)
    {
        if (item.userId != user.userId)
        {
            continue;
        }

        item = user;
        found = true;
        break;
    }

    if (!found)
    {
        _friendList.push_back(user);
    }

    if (_mainWindow)
    {
        _mainWindow->addFriendItem(user);
    }
}

void TalkyClient::removeFriend(int userId)
{
    // 和 upsertFriend 对应，删除好友时先改本地缓存；
    // UI 是否已经显示，由上层时机决定是否同步刷新。
    for (auto it = _friendList.begin(); it != _friendList.end(); ++it)
    {
        if (it->userId != userId)
        {
            continue;
        }

        _friendList.erase(it);
        break;
    }
}

void TalkyClient::syncFriendListToMainWindow()
{
    if (!_mainWindow)
    {
        return;
    }

    // MainWindow 只负责渲染当前已有的好友状态，不保存全量来源。
    // 因此主窗口首次创建或重新显示时，需要把 TalkyClient 缓存的好友列表整批补进去。
    for (const auto& user : _friendList)
    {
        _mainWindow->addFriendItem(user);
    }
}

void TalkyClient::setupWorkerCallbacks()
{
    NetWorker::Callbacks cbs;

    // NetWorker 在工作线程里收发网络数据；UI 必须在主线程更新。
    // 因此这里统一把网络回调封装成 QueuedConnection，再转成 Qt signal / UI 操作。
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

    cbs.onLoginFinished = [this](int result, const std::string& msg, const UserInfo& user) {
        auto notifyLoginFinished = [this, result, msg, user]() {
            emit loginFinished(
                result,
                ToQString(msg),
                user);
            };

        QMetaObject::invokeMethod(this, notifyLoginFinished, Qt::QueuedConnection);
        };

    cbs.onSearchFriendFinished = [this](int result, const UserInfo& user) {
        auto notifySearchFriendFinished = [this, result, user]() {
            emit searchFriendFinished(result, user);
        };
        QMetaObject::invokeMethod(this, notifySearchFriendFinished, Qt::QueuedConnection);
    };

    cbs.onAddFriendFinished = [this](int code, const UserInfo& user) {
        auto notifyAddFriendFinished = [this, code, user]() {
            emit addFriendFinished(code, user);
            };

        QMetaObject::invokeMethod(this, notifyAddFriendFinished, Qt::QueuedConnection);
        };

    cbs.onDeleteFriendFinished = [this](int code, const UserInfo& user) {
        auto notifyDeleteFriendFinished = [this, code, user]() {
            emit deleteFriendFinished(code, user);
            };

        QMetaObject::invokeMethod(this, notifyDeleteFriendFinished, Qt::QueuedConnection);
        };

    cbs.onAddFriendRequestReceived = [this](const UserInfo& user) {
        auto notifyAddFriendRequestReceived = [this, user]() {
            emit addFriendRequestReceived(user);
            };

        QMetaObject::invokeMethod(this, notifyAddFriendRequestReceived, Qt::QueuedConnection);
        };

    cbs.onAddFriendResponseReceived = [this](const UserInfo& user) {
        auto notifyAddFriendResponseReceived = [this, user]() {
            upsertFriend(user);
        };

        QMetaObject::invokeMethod(this, notifyAddFriendResponseReceived, Qt::QueuedConnection);
    };

    cbs.onDeleteFriendRequestReceived = [this](int userId) {
        auto notifyDeleteFriendRequestReceived = [this, userId]() {
            removeFriend(userId);
        };

        QMetaObject::invokeMethod(this, notifyDeleteFriendRequestReceived, Qt::QueuedConnection);
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
    // 这一层专门处理“UI 语义”：
    // NetWorker 回调经由 signal 发到这里后，再决定怎么更新 Login / MainWindow / SearchFriend。
    // 也就是说：
    // - NetWorker 只关心网络结果
    // - TalkyClient 负责把网络结果翻译成界面行为
    connect(this, &TalkyClient::connectedChanged, this,
        [this](bool connected) {

        });

    connect(this, &TalkyClient::networkError, this,
        [this](const QString& msg) {

        });

    connect(this, &TalkyClient::registerFinished, this,
        [this](int result, const QString& msg) {
            LOG_INFO("[TalkyClient] signal registerFinished received result=%d msg=%s",
                result,
                msg.toUtf8().constData());

            if (result == 0) // 注册成功
            {
                _signin->setSubmitting(true);
                _signin->showResultMessage(msg, true);
                //showLogin();
            }
            else // 注册失败
            {
                _signin->setSubmitting(false);
                _signin->showResultMessage(msg, false);
            }
        });

    connect(this, &TalkyClient::loginFinished, this,
        [this](int              result,
               const QString&   msg,
               const UserInfo&  user) {

                if (result == 0) // 登录成功
                {
                    if (_login)
                        _login->setSubmitting(false);

                    showMainWindow();
                    if (_mainWindow)
                    {
                        _mainWindow->setCurrentUser(user);
                    }
                }
                else  // 登录失败
                {
                    _login->showErrorMessage(msg);
                }
        });

    connect(this, &TalkyClient::searchFriendFinished, this,
        [this](int result,
               const UserInfo& user) {
            if (_searchFriend)
            {
                _searchFriend->setSearching(false);
                _searchFriend->addItem(result, user);
            }
        });

    connect(this, &TalkyClient::addFriendFinished, this,
        [this](int code, const UserInfo& user) {

        });

    connect(this, &TalkyClient::deleteFriendFinished, this,
        [this](int code, const UserInfo& user) {

        });

    connect(this, &TalkyClient::addFriendRequestReceived, this,
        [this](const UserInfo& user) {
            if (!_mainWindow)
            {
                showMainWindow();
            }

            if (_mainWindow)
            {
                _mainWindow->addNotifyItem(user);
            }
        });

    connect(this, &TalkyClient::messageReceived, this,
        [this](int fromUserId,
            int toUserId,
            const QString& text,
            qint64 timestamp) {
                LOG_INFO("[TalkyClient] signal messageReceived received from=%d to=%d ts=%lld textPreview=%s mainWindow=%p",
                    fromUserId,
                    toUserId,
                    static_cast<long long>(timestamp),
                    PreviewQString(text).c_str(),
                    _mainWindow.get());

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
    LOG_INFO("[TalkyClient] start host=%s port=%u",
        host.toUtf8().constData(),
        static_cast<unsigned>(port));
    _worker.SetServer(host.toUtf8().toStdString(), static_cast<uint16_t>(port));
    _worker.Start();
    launch();
}

void TalkyClient::stop()
{
    LOG_INFO("[TalkyClient] stop called");
    _worker.Stop();
}

void TalkyClient::registerUser(const QString& username, const QString& nickname, const QString& password)
{
    LOG_INFO("[TalkyClient] registerUser username=%s nickname=%s passwordLen=%u",
        username.toUtf8().constData(),
        nickname.toUtf8().constData(),
        static_cast<unsigned>(password.size()));

    _worker.Register(username.toStdString(), nickname.toStdString(), password.toStdString());
}

void TalkyClient::login(const QString& username, const QString& password, int status)
{
    LOG_INFO("[TalkyClient] login username=%s status=%d passwordLen=%u",
        username.toUtf8().constData(),
        status,
        static_cast<unsigned>(password.size()));
    _worker.Login(username.toStdString(), password.toStdString(), status);
}

void TalkyClient::searchFriend(const QString& username)
{
    LOG_INFO("[TalkyClient] searchFriend username=%s", username.toUtf8().constData());
    _worker.SearchFriend(username.toStdString());
}

void TalkyClient::addFriend(const UserInfo& user)
{
    LOG_INFO("[TalkyClient] addFriend userId=%d username=%s nickname=%s",
        user.userId,
        user.username.c_str(),
        user.nickname.c_str());
    _worker.AddFriend(user);
}

void TalkyClient::deleteFriend(int32_t userId)
{
    LOG_INFO("[TalkyClient] deleteFriend userId=%d", userId);
    removeFriend(userId);
    _worker.DelFriend(userId);
}

void TalkyClient::acceptFriendRequest(const UserInfo& user)
{
    LOG_INFO("[TalkyClient] acceptFriendRequest userId=%d username=%s nickname=%s",
        user.userId,
        user.username.c_str(),
        user.nickname.c_str());

    if (_mainWindow)
    {
        _mainWindow->acceptFriendRequestLocally(QString::fromStdString(user.username));
    }

    _worker.AcceptFriendRequest(user);
}

void TalkyClient::rejectFriendRequest(const UserInfo& user)
{
    LOG_INFO("[TalkyClient] rejectFriendRequest userId=%d username=%s nickname=%s",
        user.userId,
        user.username.c_str(),
        user.nickname.c_str());

    if (_mainWindow)
    {
        _mainWindow->rejectFriendRequestLocally(user.userId);
    }


    _worker.RejectFriendRequest(user);
}

void TalkyClient::sendTextMessage(int userId, int toUserId, const QString& text)
{
    LOG_INFO("[TalkyClient] sendTextMessage userId=%d toUserId=%d textLen=%u textPreview=%s",
        userId,
        toUserId,
        static_cast<unsigned>(text.size()),
        PreviewQString(text).c_str());
    _worker.SendText(userId, toUserId, text.toStdString());
}

void TalkyClient::launch()
{
    LOG_INFO("[TalkyClient] launch called");
    showLogin();
}

void TalkyClient::showLogin()
{
    if (!_login)
    {
        _login = std::make_unique<Login>();
        connect(_login.get(), &Login::registerRequested, this, &TalkyClient::showSignin); // 点击注册，切换到注册界面
        connect(_login.get(), &Login::loginRequested, this, &TalkyClient::onLoginRequested);
    }

    _login->show();
    _login->raise();
    _login->activateWindow();
}

void TalkyClient::onLoginRequested(const QString& username, const QString& password, int status)
{
    LOG_INFO("[TalkyClient] onLoginRequested username=%s status=%d passwordLen=%u",
        username.toUtf8().constData(),
        status,
        static_cast<unsigned>(password.size()));
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
                LOG_INFO("[TalkyClient] Signin registerSubmitted username=%s nickname=%s passwordLen=%u",
                    username.toUtf8().constData(),
                    nickname.toUtf8().constData(),
                    static_cast<unsigned>(password.size()));
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

        // MainWindow 只发“用户意图”信号，不直接碰网络层；
        // TalkyClient 负责把这些 UI 意图转换成具体业务动作。
        connect(_mainWindow.get(), &MainWindow::addFriendRequested, this, &TalkyClient::showSearchWindow); // 点击注册，切换到注册界面
        connect(_mainWindow.get(), &MainWindow::deleteFriendRequested, this, &TalkyClient::deleteFriend);
        connect(_mainWindow.get(), &MainWindow::acceptFriendRequest, this, &TalkyClient::acceptFriendRequest);
        connect(_mainWindow.get(), &MainWindow::rejectFriendRequest, this, &TalkyClient::rejectFriendRequest);
        connect(_mainWindow.get(), &MainWindow::sendTextRequested, this, &TalkyClient::sendTextMessage);
    }

    syncFriendListToMainWindow();

    // 登录窗口和主窗口采用“隐藏/显示”切换，而不是每次重建。
    // 这样主窗口中已经缓存的会话列表、消息列表等 UI 状态可以继续保留。
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

    connect(_searchFriend.get(), &SearchFriend::searchRequested,
        this, &TalkyClient::searchFriend);
    connect(_searchFriend.get(), &SearchFriend::addFriendRequested,
        this, &TalkyClient::addFriend);

    _searchFriend->show();
    _searchFriend->raise();
    _searchFriend->activateWindow();
}
