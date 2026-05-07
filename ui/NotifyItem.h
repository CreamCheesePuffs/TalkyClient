#pragma once

#include <QWidget>
#include "ui_NotifyItem.h"
#include "tasks/Msg.h"

class NotifyItem : public QWidget
{
    Q_OBJECT

public:
    NotifyItem(QWidget *parent = nullptr);
    ~NotifyItem();

    void setData(const UserInfo& user);

signals:
    void acceptRequested(const UserInfo& user);
    void rejectRequested(const UserInfo& user);

private slots:
    void onAcceptButtonClicked();
    void onRejectButtonClicked();

private:
    Ui::NotifyItemClass ui;
    UserInfo            _user;
};

