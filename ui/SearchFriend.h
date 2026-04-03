#pragma once

#include <QWidget>
#include "ui_SearchFriend.h"

class SearchFriend : public QWidget
{
    Q_OBJECT

public:
    SearchFriend(QWidget *parent = nullptr);
    ~SearchFriend();

private slots:
    void on_closeButton_clicked();

private:
    Ui::SearchFriendClass ui;
};

