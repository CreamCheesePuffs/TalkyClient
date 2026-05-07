#pragma once

#include <QWidget>
#include "ui_FriendItem.h"
#include "tasks/Msg.h"

class FriendItem : public QWidget
{
    Q_OBJECT

public:
    FriendItem(QWidget *parent = nullptr);
    ~FriendItem();

    void setData(const UserInfo& user);

signals:
    void deleteClicked(int userId);

private slots:
    void onDeleteButtonClicked();

private:
    int _userId = 0;
    Ui::FriendItemClass ui;
};

