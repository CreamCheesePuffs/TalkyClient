#include "SearchFriend.h"
#include "AddFriendItem.h"
#include "DragWidgetFilter.h"
#include <QListWidgetItem>
#include <QMessageBox>
#include <QStringList>

SearchFriend::SearchFriend(QWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    setWindowFlag(Qt::FramelessWindowHint);
    this->installEventFilter(new DragWidgetFilter(this));

    connect(ui.closeButton, &QToolButton::clicked,
        this, &SearchFriend::on_closeButton_clicked);

    connect(ui.searchButton, &QPushButton::clicked,
        this, &SearchFriend::on_searchButton_clicked);

    connect(ui.searchEdit, &QLineEdit::returnPressed,
        this, &SearchFriend::on_searchButton_clicked);

}

SearchFriend::~SearchFriend()
{}

void SearchFriend::on_closeButton_clicked()
{
    this->close();
}

void SearchFriend::setSearching(bool searching)
{
    ui.searchEdit->setEnabled(!searching);
    ui.searchButton->setEnabled(!searching);
    ui.searchButton->setText(searching ? QStringLiteral("Search...") : QStringLiteral("Search"));
}

void SearchFriend::addItem(int result, const UserInfo& user)
{
    ui.friendList->clear();

    if (result != 0)
    {
        ui.friendList->addItem(QStringLiteral("未找到该用户"));
        return;
    }

    auto* listItem = new QListWidgetItem();
    listItem->setSizeHint(QSize(0, 68));

    auto* addFriendItem = new AddFriendItem();
    addFriendItem->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addFriendItem->setFixedHeight(62);
    addFriendItem->setData(user);

    connect(addFriendItem, &AddFriendItem::addClicked,
        this, &SearchFriend::addFriendRequested);

    listItem->setData(Qt::UserRole, user.userId);
    listItem->setData(Qt::UserRole + 1, QString::fromStdString(user.username));

    ui.friendList->addItem(listItem);
    ui.friendList->setItemWidget(listItem, addFriendItem);
}

void SearchFriend::on_searchButton_clicked()
{
    const QString username = ui.searchEdit->text().trimmed();
    if (username.isEmpty())
    {
        return;
    }

    setSearching(true);
    // ui.friendList->clear();
    emit searchRequested(username);
}
