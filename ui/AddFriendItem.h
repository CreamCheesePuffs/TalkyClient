#pragma once

#include <QWidget>
#include "ui_AddFriendItem.h"
#include "tasks/Msg.h"

class AddFriendItem : public QWidget
{
    Q_OBJECT

public:
    AddFriendItem(QWidget *parent = nullptr);
    ~AddFriendItem();

    void setData(const UserInfo& user);

signals:
    void addClicked(const UserInfo& user);

private slots:
    void onAddButtonClicked();

private:
    Ui::AddFriendItemClass ui;
    UserInfo _userInfo{};
};

