#pragma once

#include <QWidget>
#include <QMouseEvent>
#include "ui_Login.h"


class Login : public QWidget
{
    Q_OBJECT

public:
    Login(QWidget *parent = nullptr);
    ~Login();

    void setSubmitting(bool submitting);
    void showErrorMessage(const QString& msg);

signals:
    void registerRequested();
    void loginRequested(const QString& username, const QString& password, int status);

private:
    void on_close_toolButton_clicked();
    void on_register_pushButton_clicked();
    void on_login_pushButton_clicked();

    Ui::LoginClass ui;

private:
    QPoint pos;
};

