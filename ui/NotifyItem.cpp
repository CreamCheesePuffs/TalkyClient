#include "NotifyItem.h"

NotifyItem::NotifyItem(QWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    connect(ui.acceptButton, &QPushButton::clicked,
        this, &NotifyItem::onAcceptButtonClicked);
    connect(ui.rejectButton, &QPushButton::clicked,
        this, &NotifyItem::onRejectButtonClicked);
}

NotifyItem::~NotifyItem()
{}

void NotifyItem::setData(const UserInfo& user)
{
    _user = user;

    const QString username = QString::fromStdString(user.username);
    const QString text = QStringLiteral("%1 请求加你为好友").arg(username);

    ui.requestLabel->setText(text);
}

void NotifyItem::onAcceptButtonClicked()
{
    ui.acceptButton->setEnabled(false);
    ui.rejectButton->setEnabled(false);
    emit acceptRequested(_user);
}

void NotifyItem::onRejectButtonClicked()
{
    ui.acceptButton->setEnabled(false);
    ui.rejectButton->setEnabled(false);
    emit rejectRequested(_user);
}

