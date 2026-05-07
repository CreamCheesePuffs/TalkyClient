#include "FriendItem.h"

FriendItem::FriendItem(QWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);
    connect(ui.toolButton, &QToolButton::clicked,
        this, &FriendItem::onDeleteButtonClicked);
}

FriendItem::~FriendItem()
{}

void FriendItem::setData(const UserInfo& user)
{
    _userId = user.userId;
    const QString username = QString::fromStdString(user.username);
    const QString nickname = QString::fromStdString(user.nickname);
    const QString displayName = nickname.isEmpty() ? username : nickname;

    ui.nicknamelabel->setText(displayName);
}

void FriendItem::onDeleteButtonClicked()
{
    ui.toolButton->setEnabled(false);
    emit deleteClicked(_userId);
}

