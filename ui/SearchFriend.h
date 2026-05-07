#pragma once

#include <QWidget>
#include "ui_SearchFriend.h"
#include "tasks/Msg.h"

class SearchFriend : public QWidget
{
    Q_OBJECT

public:
    SearchFriend(QWidget *parent = nullptr);
    ~SearchFriend();

    void setSearching(bool searching);
    void addItem(int result, const UserInfo& user);

signals:
    void searchRequested(const QString& username);
    void addFriendRequested(const UserInfo& user);

private slots:
    void on_closeButton_clicked();
    void on_searchButton_clicked();

private:
    Ui::SearchFriendClass ui;
};

