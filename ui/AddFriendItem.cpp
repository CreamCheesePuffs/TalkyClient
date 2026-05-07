#include "AddFriendItem.h"

AddFriendItem::AddFriendItem(QWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    connect(ui.addButton, &QPushButton::clicked,
        this, &AddFriendItem::onAddButtonClicked);
}

AddFriendItem::~AddFriendItem()
{

}

void AddFriendItem::setData(const UserInfo& user)
{
    _userInfo = user;

    const QString username = QString::fromStdString(user.username);
    const QString nickname = QString::fromStdString(user.nickname);
    const QString name = nickname.isEmpty()
        ? username
        : QString("%1 (%2)").arg(nickname, username);

    ui.nameLabel->setText(name);
}

void AddFriendItem::onAddButtonClicked()
{
    ui.addButton->setEnabled(false);
    emit addClicked(_userInfo);
}

