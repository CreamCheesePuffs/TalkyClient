#include "MainWindow.h"
#include "ChatItem.h"
#include "DragWidgetFilter.h"
#include "FriendItem.h"
#include "MessageBubble.h"
#include "NotifyItem.h"
#include <QDebug>

static void refreshWidgetStyle(QWidget* w)
{
    if (!w) return;
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    // 当前窗口是“左侧多列表 + 右侧聊天面板”的壳。
    // 这里有一个比较重要的设计：
    // - 左侧 chatList 只是当前模式下的可视列表
    // - 右侧 chatPanel 只是当前会话的渲染结果
    // - 真正的状态分别保存在 _sessionList / _friendList / _notifyList / _sessionMessages
    //
    // 因此构造函数里主要完成三件事：
    // 1. 初始化 chatList / messageList 的展示行为
    // 2. 绑定左侧列表、顶部按钮和发送按钮的交互
    // 3. 建立从好友/通知进入会话的跳转逻辑
    setWindowFlag(Qt::FramelessWindowHint);
    this->installEventFilter(new DragWidgetFilter(this));

    // 顶部昵称显示：只读
    ui.peerNameEdit->setReadOnly(true);
    ui.peerNameEdit->setFocusPolicy(Qt::NoFocus);    // 不可获得焦点
    ui.peerNameEdit->setCursor(Qt::ArrowCursor);     // 鼠标不要变输入态

    ui.chatList->setMouseTracking(true);              // 必须
    ui.chatList->viewport()->setMouseTracking(true);  // 必须
    ui.chatList->setSpacing(2);

    ui.chatList->setFrameShape(QFrame::NoFrame);
    ui.chatList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.chatList->setSelectionMode(QAbstractItemView::SingleSelection);

    // 消息列表：不允许选中
    ui.messageList->setSelectionMode(QAbstractItemView::NoSelection);
    ui.messageList->setFrameShape(QFrame::NoFrame);
    ui.messageList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    connect(ui.closeButton, &QToolButton::clicked,
        this, &MainWindow::onCloseButtonClicked);

    // 点击左侧会话列表
    connect(ui.chatList, &QListWidget::itemClicked,
        this, &MainWindow::onChatSelected);

    // 点击添加好友
    connect(ui.addFriend_toolButton, &QToolButton::clicked,
        this, &MainWindow::onAddFriendRequest);

    connect(ui.chatSession_toolButton, &QToolButton::clicked,
        this, [this]() { switchListMode(ListMode::Session); });
    connect(ui.friendsList_toolButton, &QToolButton::clicked,
        this, [this]() { switchListMode(ListMode::Friend); });
    connect(ui.notify_toolButton, &QToolButton::clicked,
        this, [this]() { switchListMode(ListMode::Notify); });

    // 发送按钮
    connect(ui.sendButton, &QPushButton::clicked,
        this, &MainWindow::onSendClicked);

    // 回车发送（messageEdit 是 QLineEdit 才能这样）
    connect(ui.messageEdit, &QLineEdit::returnPressed,
        ui.sendButton, &QPushButton::click);

    // hover / selected 样式都交给 itemWidget 的动态属性控制，
    // 这样三类列表项（会话、好友、通知）可以共用统一的选中交互。
    connect(ui.chatList, &QListWidget::itemEntered, this, [this](QListWidgetItem* item) {
        for (int i = 0; i < ui.chatList->count(); ++i) {
            auto* it = ui.chatList->item(i);
            if (auto* w = ui.chatList->itemWidget(it)) {
                w->setProperty("hover", it == item);
                refreshWidgetStyle(w);
            }
        }
        });

    connect(ui.chatList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        for (int i = 0; i < ui.chatList->count(); ++i) {
            auto* it = ui.chatList->item(i);
            if (auto* w = ui.chatList->itemWidget(it)) {
                w->setProperty("selected", it == item);
                refreshWidgetStyle(w);
            }
        }

        const QString itemType = item->data(Qt::UserRole + 10).toString();
        if (itemType != QStringLiteral("session"))
        {
            // 好友/通知项只允许选中样式，不展示也不记录当前会话对象
            ui.peerNameEdit->clear();
            return;
        }

        const QString displayName = item->data(Qt::UserRole + 1).toString();
        ui.peerNameEdit->setText(displayName);  // 你的头部 lineEdit objectName 改成 peerNameEdit
        });

    // 双击好友项时，不直接操作右侧 UI，而是先确保会话数据存在，
    // 再切到 session 列表并复用 onChatSelected() 完成整块右侧面板刷新。
    connect(ui.chatList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item)
        {
            return;
        }

        // 只处理 friend 类型；双击会话/通知不触发该跳转逻辑
        const QString itemType = item->data(Qt::UserRole + 10).toString();
        if (itemType != QStringLiteral("friend"))
        {
            return;
        }

        // 从好友项里取出用户标识，后续用于查找/创建对应会话
        const int friendUserId = item->data(Qt::UserRole).toInt();
        const QString peerId = QString::number(friendUserId);
        const QString displayName = item->data(Qt::UserRole + 1).toString();

        // 先找该好友是否已经存在会话（存在就要“置顶”）
        int existingIndex = -1;
        for (int i = 0; i < _sessionList.size(); ++i)
        {
            if (_sessionList[i].peerId == peerId)
            {
                existingIndex = i;
                break;
            }
        }

        ChatSummary session;
        if (existingIndex >= 0)
        {
            // 已存在会话：取出旧数据并从原位置移除，待会 push_front 到顶部
            session = _sessionList[existingIndex];
            _sessionList.removeAt(existingIndex);
        }
        // 不存在会话时 session 是默认值；统一填入本次好友信息
        session.peerId = peerId;
        session.displayName = displayName;
        if (!session.lastTime.isValid())
        {
            // 首次创建会话给一个初始时间，保证列表时间字段可显示
            session.lastTime = QDateTime::currentDateTime();
        }
        // 刚主动进入会话，未读应清零
        session.unreadCount = 0;
        // 把会话放到会话列表顶部（最近交互优先）
        _sessionList.push_front(session);

        // 切到“会话列表”模式，触发左侧列表重绘
        switchListMode(ListMode::Session);

        // 在重绘后的 session 列表里定位刚才的会话并选中
        for (int i = 0; i < ui.chatList->count(); ++i)
        {
            auto* sessionItem = ui.chatList->item(i);
            if (!sessionItem)
            {
                continue;
            }

            if (sessionItem->data(Qt::UserRole + 10).toString() == QStringLiteral("session")
                && sessionItem->data(Qt::UserRole).toString() == peerId)
            {
                ui.chatList->setCurrentItem(sessionItem);
                // 复用现有点击处理：同步右侧 peerName / 消息区状态
                onChatSelected(sessionItem);
                break;
            }
        }
    });


    // 测试数据：启动后切不同列表都能直接看到效果
    // UserInfo friendA;
    // friendA.userId = 10001;
    // friendA.username = "alice";
    // friendA.nickname = "Alice";
    // addFriendItem(friendA);

    // UserInfo friendB;
    // friendB.userId = 10002;
    // friendB.username = "bob";
    // friendB.nickname = "Bob";
    // addFriendItem(friendB);

    // UserInfo friendC;
    // friendC.userId = 10003;
    // friendC.username = "charlie";
    // friendC.nickname = "Charlie";
    // addFriendItem(friendC);

    // UserInfo notifyA;
    // notifyA.userId = 20001;
    // notifyA.username = "david";
    // notifyA.nickname = "David";
    // addNotifyItem(notifyA);

    // UserInfo notifyB;
    // notifyB.userId = 20002;
    // notifyB.username = "eva";
    // notifyB.nickname = "Eva";
    // addNotifyItem(notifyB);

    // ChatSummary chatA;
    // chatA.peerId = QString::number(friendA.userId);
    // chatA.displayName = friendA.nickname.empty()
    //     ? QString::fromStdString(friendA.username)
    //     : QString::fromStdString(friendA.nickname);
    // chatA.lastPreview = QString::fromUtf16(u"今天晚上一起联调一下客户端？");
    // chatA.lastTime = QDateTime::currentDateTime().addSecs(-3 * 60);
    // chatA.unreadCount = 2;
    // _sessionList.push_back(chatA);

    // ChatSummary chatB;
    // chatB.peerId = QString::number(friendB.userId);
    // chatB.displayName = friendB.nickname.empty()
    //     ? QString::fromStdString(friendB.username)
    //     : QString::fromStdString(friendB.nickname);
    // chatB.lastPreview = QString::fromUtf16(u"新的好友列表切换已经做好了");
    // chatB.lastTime = QDateTime::currentDateTime().addSecs(-18 * 60);
    // chatB.unreadCount = 0;
    // _sessionList.push_back(chatB);

    // ChatSummary chatC;
    // chatC.peerId = QString::number(friendC.userId);
    // chatC.displayName = friendC.nickname.empty()
    //     ? QString::fromStdString(friendC.username)
    //     : QString::fromStdString(friendC.nickname);
    // chatC.lastPreview = QString::fromUtf16(u"通知页也可以点开看看效果");
    // chatC.lastTime = QDateTime::currentDateTime().addSecs(-60 * 60);
    // chatC.unreadCount = 5;
    // _sessionList.push_back(chatC);

    switchListMode(ListMode::Session);
}

MainWindow::~MainWindow()
{

}

void MainWindow::onCloseButtonClicked()
{
    this->close();
}

// 点击会话项后，右侧 chatPanel 的内容全部按“当前会话状态”重建：
// 1. 记录当前会话 userId / 昵称
// 2. 清零该会话未读数
// 3. 从 _sessionMessages 里取出历史消息重新渲染到 messageList
// 4. 如当前正显示 session 列表，再把左侧摘要同步刷新一次
void MainWindow::onChatSelected(QListWidgetItem* item)
{
    const QString itemType = item->data(Qt::UserRole + 10).toString();

    /* 好友/通知项不展示 peerName，也不记录当前会话 */
    if (itemType != QStringLiteral("session"))
    {
        _currentPeer.clear();
        _currentPeerUserId = 0;
        ui.peerNameEdit->clear();
        ui.messageList->clear();
        return;
    }

    _currentPeerUserId = item->data(Qt::UserRole).toInt();
    const QString displayName = item->data(Qt::UserRole + 1).toString();
    _currentPeer = displayName;
    ui.peerNameEdit->setText(_currentPeer);
    for (auto& session : _sessionList)
    {
        if (session.peerId == QString::number(_currentPeerUserId))
        {
            session.unreadCount = 0;
            break;
        }
    }
    renderCurrentSessionMessages();
    if (_currentListMode == ListMode::Session)
    {
        // itemClicked 还连着另一个更新选中样式的 lambda。
        // 如果这里同步 renderCurrentList()，ui.chatList->clear() 会把当前点击的 item 提前销毁，
        // 后续槽继续访问这个 item 时就可能出现悬空指针，导致点击会话项后崩溃。
        QMetaObject::invokeMethod(this, [this]() {
            renderCurrentList();
        }, Qt::QueuedConnection);
    }
}

// 发送按钮点击后，先把消息走业务层发出去，再统一复用 appendMessage()
// 把本地消息落到会话状态里。这样切换会话后，右侧消息能完整恢复。
void MainWindow::onSendClicked()
{
    QString text = ui.messageEdit->text().trimmed();
    if (text.isEmpty() || _currentPeerUserId <= 0)
        return;

    emit sendTextRequested(_user.userId, _currentPeerUserId, text);
    appendMessage(
        _user.userId,
        _currentPeerUserId,
        text,
        QDateTime::currentMSecsSinceEpoch());
    ui.messageEdit->clear();
}

// addBubble：把气泡 widget 塞进 messageList
void MainWindow::addBubble(const QString& text, bool outgoing)
{
    auto* item = new QListWidgetItem();
    auto* bubble = new MessageBubble(text, outgoing);

    // 关键：先给一个合理的最大宽度（决定换行）
    int w = ui.messageList->viewport()->width();
    if (w <= 0) w = 600;

    bubble->setMaximumWidth(w);
    bubble->adjustSize();                 // 让布局计算真实大小
    QSize sz = bubble->sizeHint();
    if (sz.height() < 28) 
        sz.setHeight(28);

    item->setSizeHint(QSize(0, sz.height() + 6));  // +6 给一点上下余量

    ui.messageList->addItem(item);
    ui.messageList->setItemWidget(item, bubble);
    ui.messageList->scrollToBottom();

    qDebug() << "[addBubble] count:" << ui.messageList->count()
        << "itemHeight:" << item->sizeHint().height()
        << "widgetVisible:" << bubble->isVisible()
        << "widgetGeom:" << bubble->geometry();

}

void MainWindow::renderCurrentSessionMessages()
{
    // 右侧 messageList 不是数据源，而是当前会话消息的渲染结果，
    // 因此每次切会话都直接清空后按保存的数据重绘。
    ui.messageList->clear();
    if (_currentPeerUserId <= 0)
    {
        return;
    }

    const auto messages = _sessionMessages.constFind(_currentPeerUserId);
    if (messages == _sessionMessages.constEnd())
    {
        return;
    }

    for (const auto& message : messages.value())
    {
        addBubble(message.text, message.fromUserId == _user.userId);
    }
}

QString MainWindow::formatTimeForList(const QDateTime& dt)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = dt.date();
    const QDate today = now.date();

    if (d == today) return dt.toString("HH:mm");
    if (d == today.addDays(-1)) return "Yesterday";
    if (d.year() == today.year()) return dt.toString("MM/dd");
    return dt.toString("yyyy/MM/dd");
}

void MainWindow::addChatItem(const ChatSummary& s)
{
    // 会话列表每一项都来自 ChatSummary。
    // ChatSummary 只保存“左侧摘要所需信息”，不保存完整消息历史；
    // 真正的消息历史在 _sessionMessages 里，点击 item 后再拿出来渲染右侧。
    // 1) 壳 item
    auto* chatList = new QListWidgetItem();
    // 宽度设为 0 让 widget 自动填充，高度固定为 68
    chatList->setSizeHint(QSize(0, 68));

    // 2) 真实显示控件
    auto* chatItem = new ChatItem(); // 你的类名若叫 ChatItemWidget/ChatItemClass 都行
    //w->setObjectName("ChatItemClass"); // ✅ 确保 QSS 命中（如果 ui 里根本就叫 ChatItemClass 可不加）
    // 水平方向扩展，垂直方向固定
    chatItem->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    chatItem->setFixedHeight(68);
    // 确保 ChatItem 能够填充整个 QListWidgetItem 的宽度
    //w->setMinimumWidth(0);

    chatItem->setData(s.displayName, s.lastPreview, formatTimeForList(s.lastTime), s.unreadCount, QPixmap(s.avatarRes));

    // 3) 把 peerId 存进 item，后续点击能知道选的是谁
    chatList->setData(Qt::UserRole, s.peerId);
    chatList->setData(Qt::UserRole + 1, s.displayName);
    chatList->setData(Qt::UserRole + 10, QStringLiteral("session"));

    // 4) 插入列表
    ui.chatList->addItem(chatList);
    ui.chatList->setItemWidget(chatList, chatItem);
    
    // 5) 确保 widget 能够正确展开到 item 的宽度
    // QListWidget 会自动调整 itemWidget 的宽度，但我们需要确保它能够正确响应
    //chatList->adjustSize();

    qDebug() << "itemWidget =" << ui.chatList->itemWidget(chatList);
    qDebug() << "ChatItem size:" << chatItem->size() << "chatList sizeHint:" << chatList->sizeHint();

}

void MainWindow::setCurrentUser(const UserInfo &user)
{
    _user = user;
} 

void MainWindow::addFriendItem(const UserInfo& user)
{
    const QString peerId = QString::number(user.userId);
    const QString nickname = QString::fromStdString(user.nickname);
    const QString displayName = nickname;

    // 好友列表和会话摘要共享 displayName。
    // 因此这里既要维护 _friendList，也要把已存在会话的显示名同步更新掉。
    for (auto& item : _friendList)
    {
        if (item.userId == user.userId)
        {
            item = user;
            for (auto& sessionItem : _sessionList)
            {
                if (sessionItem.peerId == peerId)
                {
                    sessionItem.displayName = displayName;
                }
            }
            if (_currentListMode == ListMode::Friend)
            {
                renderCurrentList();
            }
            return;
        }
    }

    _friendList.push_back(user);
    for (auto& sessionItem : _sessionList)
    {
        if (sessionItem.peerId == peerId)
        {
            sessionItem.displayName = displayName;
        }
    }
    if (_currentListMode == ListMode::Friend)
    {
        renderCurrentList();
    }
}

void MainWindow::addNotifyItem(const UserInfo& user)
{
    // 通知列表只去重不排序，保持“最近收到的通知插到最前面”的简单策略。
    for (const auto& item : _notifyList)
    {
        if (item.userId == user.userId)
        {
            return;
        }
    }

    _notifyList.push_back(user);
    if (_currentListMode == ListMode::Notify)
    {
        renderCurrentList();
    }
}

bool MainWindow::acceptFriendRequestLocally(const QString& username)
{
    // “本地接受”只负责 UI 和内存状态：
    // - 从通知列表移除
    // - 如有需要清空当前右侧上下文
    // - 把对方加入好友列表
    // 真正的网络请求由上层 TalkyClient / NetWorker 负责。
    for (int i = 0; i < _notifyList.size(); ++i)
    {
        if (QString::fromStdString(_notifyList[i].username) != username)
        {
            continue;
        }

        const UserInfo user = _notifyList[i];
        const QString nickname = QString::fromStdString(user.nickname);
        const QString displayName = nickname;

        _notifyList.removeAt(i);

        if (_currentPeer == displayName)
        {
            _currentPeer.clear();
            _currentPeerUserId = 0;
            ui.peerNameEdit->clear();
            ui.messageList->clear();
        }

        if (_currentListMode == ListMode::Notify)
        {
            renderCurrentList();
        }

        addFriendItem(user);
        return true;
    }

    return false;
}

bool MainWindow::rejectFriendRequestLocally(int peerUserId)
{
    // “本地拒绝”只做通知页状态清理，不涉及网络协议。
    for (int i = 0; i < _notifyList.size(); ++i)
    {
        if (_notifyList[i].userId != peerUserId)
        {
            continue;
        }

        const UserInfo user = _notifyList[i];

        _notifyList.removeAt(i);

        if (_currentPeerUserId == peerUserId)
        {
            _currentPeer.clear();
            _currentPeerUserId = 0;
            ui.peerNameEdit->clear();
            ui.messageList->clear();
        }

        if (_currentListMode == ListMode::Notify)
        {
            renderCurrentList();
        }
        return true;
    }

    return false;
}

void MainWindow::renderFriendItem(const UserInfo& user)
{
    // 好友项主要承载两类交互：
    // - 双击好友进入会话（在构造函数里统一处理）
    // - 点击删除按钮发出 deleteFriendRequested 信号
    auto* friendListItem = new QListWidgetItem();
    friendListItem->setSizeHint(QSize(0, 68));
    friendListItem->setData(Qt::UserRole, user.userId);

    const QString nickname = QString::fromStdString(user.nickname);
    const QString displayName = nickname;

    friendListItem->setData(Qt::UserRole + 1, displayName);
    friendListItem->setData(Qt::UserRole + 10, QStringLiteral("friend"));

    auto* friendItem = new FriendItem();
    friendItem->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    friendItem->setFixedHeight(68);
    friendItem->setData(user);
    connect(friendItem, &FriendItem::deleteClicked, this,
        [this](int userId) {
            removeFriendItem(userId);
            emit deleteFriendRequested(userId);
        });

    ui.chatList->addItem(friendListItem);
    ui.chatList->setItemWidget(friendListItem, friendItem);
}

void MainWindow::renderNotifyItem(const UserInfo& user)
{
    // 通知项本身只负责把 accept / reject 按钮事件往外抛。
    // 具体是走本地 UI 处理还是网络请求，由 MainWindow / TalkyClient 再决定。
    auto* notifyListItem = new QListWidgetItem();
    notifyListItem->setSizeHint(QSize(0, 68));
    notifyListItem->setData(Qt::UserRole, user.userId);
    notifyListItem->setData(Qt::UserRole + 1, QString::fromStdString(user.nickname));
    notifyListItem->setData(Qt::UserRole + 10, QStringLiteral("notify"));

    auto* notifyItem = new NotifyItem();
    notifyItem->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    notifyItem->setFixedHeight(68);
    notifyItem->setData(user);

    connect(notifyItem, &NotifyItem::acceptRequested,
        this, &MainWindow::acceptFriendRequest);
    connect(notifyItem, &NotifyItem::rejectRequested,
        this, &MainWindow::rejectFriendRequest);

    ui.chatList->insertItem(0, notifyListItem);
    ui.chatList->setItemWidget(notifyListItem, notifyItem);
}

void MainWindow::renderCurrentList()
{
    // chatList 的三种模式都走这里统一重绘。
    // 先记住当前选中项，再按当前模式整表重建，最后尽量恢复选中状态。
    const QListWidgetItem* selectedItem = ui.chatList->currentItem();
    const QString selectedId = selectedItem
        ? selectedItem->data(Qt::UserRole).toString()
        : QString();
    const QString selectedType = selectedItem
        ? selectedItem->data(Qt::UserRole + 10).toString()
        : QString();

    ui.chatList->clear();

    switch (_currentListMode)
    {
    case ListMode::Session:
        for (const auto& item : _sessionList)
        {
            addChatItem(item);
        }
        break;
    case ListMode::Friend:
        for (const auto& item : _friendList)
        {
            renderFriendItem(item);
        }
        break;
    case ListMode::Notify:
        for (const auto& item : _notifyList)
        {
            renderNotifyItem(item);
        }
        break;
    }

    if (!selectedId.isEmpty())
    {
        for (int i = 0; i < ui.chatList->count(); ++i)
        {
            auto* item = ui.chatList->item(i);
            if (item->data(Qt::UserRole).toString() == selectedId
                && item->data(Qt::UserRole + 10).toString() == selectedType)
            {
                ui.chatList->setCurrentItem(item);
                if (auto* widget = ui.chatList->itemWidget(item))
                {
                    widget->setProperty("selected", true);
                    refreshWidgetStyle(widget);
                }
                break;
            }
        }
    }
}

void MainWindow::switchListMode(ListMode mode)
{
    // 切换左侧模式时，右侧当前会话上下文立即失效：
    // - 当前 peer 清空
    // - 右侧 chatPanel 只在 session 模式下可用
    // - 左侧列表整表重绘
    _currentListMode = mode;
    _currentPeer.clear();
    _currentPeerUserId = 0;
    const bool enableChatPanel = (mode == ListMode::Session);
    ui.chatPanel->setEnabled(enableChatPanel);
    ui.addFriend_toolButton->setVisible(true);
    ui.chatList->clearSelection();
    ui.peerNameEdit->clear();
    ui.messageList->clear();
    renderCurrentList();
}

void MainWindow::removeFriendItem(int userId)
{
    // 删除好友时不仅要删好友列表本身，
    // 还要在“当前正打开这个好友会话”的场景下清空右侧上下文。
    for (int i = 0; i < _friendList.size(); ++i)
    {
        if (_friendList[i].userId != userId)
        {
            continue;
        }

        const QString nickname = QString::fromStdString(_friendList[i].nickname);
        const QString displayName = nickname;
        const QString peerId = QString::number(userId);

        _friendList.removeAt(i);

        if (_currentPeer == displayName || _currentPeer == peerId)
        {
            _currentPeer.clear();
            _currentPeerUserId = 0;
            ui.peerNameEdit->clear();
            ui.messageList->clear();
        }

        if (_currentListMode == ListMode::Friend)
        {
            renderCurrentList();
        }
        return;
    }
}


void MainWindow::refreshFriendList()
{
    if (_currentListMode == ListMode::Friend)
    {
        renderCurrentList();
    }
}

void MainWindow::appendMessage(int fromUserId, int toUserId, const QString& text, qint64 timestamp)
{
    // appendMessage 是消息进入 UI 状态层的统一入口：
    // - 自己发送的消息从 onSendClicked() 走进来
    // - 网络收到的消息从 TalkyClient::messageReceived 走进来
    //
    // 这样无论消息来源如何，都会统一更新：
    // 1. _sessionMessages 中的历史消息
    // 2. _sessionList 中的左侧会话摘要
    // 3. 当前右侧 chatPanel（如果此刻正打开这个会话）
    const bool outgoing = fromUserId == _user.userId;
    const int peerUserId = outgoing ? toUserId : fromUserId;
    const QString peerId = QString::number(peerUserId);
    const qint64 normalizedTimestamp = timestamp > 0
        ? timestamp
        : QDateTime::currentMSecsSinceEpoch();

    // 先把消息写入会话状态，后面的左侧摘要刷新和右侧气泡重绘都基于这份数据。
    _sessionMessages[peerUserId].push_back(
        SessionMessage{ fromUserId, toUserId, text, normalizedTimestamp });

    QString displayName;
    for (const auto& friendUser : _friendList)
    {
        if (friendUser.userId == peerUserId)
        {
            const QString nickname = QString::fromStdString(friendUser.nickname);
            displayName = nickname;
            break;
        }
    }

    if (displayName.isEmpty())
    {
        displayName = _currentPeer;
    }

    const QDateTime messageTime = normalizedTimestamp > 1000000000000LL
        ? QDateTime::fromMSecsSinceEpoch(normalizedTimestamp)
        : QDateTime::fromSecsSinceEpoch(normalizedTimestamp);
    const bool isCurrentPeer = (_currentPeerUserId == peerUserId);

    int existingIndex = -1;
    for (int i = 0; i < _sessionList.size(); ++i)
    {
        if (_sessionList[i].peerId == peerId)
        {
            existingIndex = i;
            break;
        }
    }

    // 会话摘要列表只保留“最后一条消息 + 时间 + 未读数”，
    // 因此每来一条消息都要把对应摘要提到顶部并刷新内容。
    ChatSummary summary;
    if (existingIndex >= 0)
    {
        summary = _sessionList[existingIndex];
        _sessionList.removeAt(existingIndex);
    }

    summary.peerId = peerId;
    summary.displayName = displayName;
    summary.lastPreview = text;
    summary.lastTime = messageTime.isValid() ? messageTime : QDateTime::currentDateTime();
    if (!outgoing && !isCurrentPeer)
    {
        summary.unreadCount += 1;
    }
    else
    {
        summary.unreadCount = 0;
    }

    _sessionList.push_front(summary);

    if (isCurrentPeer)
    {
        // 当前就在这个会话里时，直接整块重绘右侧，保证显示与保存状态一致。
        renderCurrentSessionMessages();
    }

    if (_currentListMode == ListMode::Session)
    {
        renderCurrentList();
    }
}

void MainWindow::onAddFriendRequest()
{
    emit addFriendRequested();
}

