#include "MainWindow.h"
#include "ChatItem.h"
#include "DragWidgetFilter.h"
#include "MessageBubble.h"
#include <QDebug>

QVector<ChatSummary> mocks = {
    {
        "alice", QStringLiteral(u"Alice"),
        QStringLiteral(u"今晚开黑吗？我这边人齐了"),
        QDateTime::currentDateTime().addSecs(-3 * 60),
        2,
        ":/images/avatar_alice.png"
    },
    {
        "bob", QStringLiteral(u"Bob"),
        QStringLiteral(u"OK，文档我改完了，发你邮箱了"),
        QDateTime::currentDateTime().addSecs(-60 * 60 * 5),
        0,
        ":/images/avatar_bob.png"
    },
    {
        "design_group", QStringLiteral(u"Design Group"),
        QStringLiteral(u"新 UI 草图已上传：talky_main_v3.png"),
        QDateTime::currentDateTime().addDays(-1).addSecs(-30 * 60),
        18,
        ":/images/avatar_group.png"
    },
    {
        "charlie", QStringLiteral(u"Charlie"),
        QStringLiteral(u"下周一面试加油，记得复习 epoll/reactor"),
        QDateTime::currentDateTime().addDays(-6),
        105, // 演示 99+
        ":/images/avatar_charlie.png"
    }
};

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

    // 发送按钮
    connect(ui.sendButton, &QPushButton::clicked,
        this, &MainWindow::onSendClicked);

    // 回车发送（messageEdit 是 QLineEdit 才能这样）
    connect(ui.messageEdit, &QLineEdit::returnPressed,
        ui.sendButton, &QPushButton::click);

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

        // ✅ 联动 chatPanel 头部昵称
        _currentPeer = item->data(Qt::UserRole).toString();
        const QString displayName = item->data(Qt::UserRole + 1).toString();
        ui.peerNameEdit->setText(displayName);  // 你的头部 lineEdit objectName 改成 peerNameEdit
        });

    // 测试代码
    ui.chatList->clear();
    for (const auto& s : mocks) 
        addChatItem(s);

}

MainWindow::~MainWindow()
{

}

void MainWindow::onCloseButtonClicked()
{
    this->close();
}

// onChatSelected：点会话 → 顶部显示昵称 + 清空消息
void MainWindow::onChatSelected(QListWidgetItem* item)
{
    _currentPeer = item->text();
    ui.peerNameEdit->setText(_currentPeer);

    ui.messageList->clear();

    // 演示：对方来一条
    addBubble(QString("%1: Hi!").arg(_currentPeer), false);
}

// onSendClicked：发送并回显到 messageList（右侧气泡）
void MainWindow::onSendClicked()
{
    QString text = ui.messageEdit->text().trimmed();
    if (text.isEmpty())
        return;

    addBubble(text, true);      // 自己消息（右侧）
    ui.messageEdit->clear();

    // 可选：模拟对方回复
    addBubble("OK", false);
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

    // 4) 插入列表
    ui.chatList->addItem(chatList);
    ui.chatList->setItemWidget(chatList, chatItem);
    
    // 5) 确保 widget 能够正确展开到 item 的宽度
    // QListWidget 会自动调整 itemWidget 的宽度，但我们需要确保它能够正确响应
    //chatList->adjustSize();

    qDebug() << "itemWidget =" << ui.chatList->itemWidget(chatList);
    qDebug() << "ChatItem size:" << chatItem->size() << "chatList sizeHint:" << chatList->sizeHint();

}

void MainWindow::setCurrentUser(int userId, const QString& nickname)
{
    
}

void MainWindow::refreshFriendList()
{

}

void MainWindow::appendMessage(int fromUserId, int toUserId, const QString& text, qint64 timestamp)
{
    
}

void MainWindow::onAddFriendRequest()
{
    emit addFriendRequested();
}

