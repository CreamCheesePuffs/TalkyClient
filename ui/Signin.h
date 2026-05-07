#pragma once

#include <QWidget>
#include "ui_Signin.h"

class Signin : public QWidget
{
    Q_OBJECT

public:
    Signin(QWidget *parent = nullptr);
    ~Signin();


    void setSubmitting(bool submitting);
    void showResultMessage(const QString& msg, bool success);

signals:
    void closeRequested();
    void registerSubmitted(const QString& username, const QString& nickname, const QString& password);

private:
    void on_close_toolButton_clicked();
    void on_register_pushButton_clicked();

    Ui::SigninClass ui;
};

