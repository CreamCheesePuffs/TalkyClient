#include "Login.h"
#include "DragWidgetFilter.h"
#include "MainWindow.h"
#include "SearchFriend.h"
#include "Signin.h"
#include <QMessageBox>

Login::Login(QWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    setWindowFlag(Qt::FramelessWindowHint);
    this->installEventFilter(new DragWidgetFilter(this));

    //ui.close_toolButton->setCursor(Qt::PointingHandCursor);
    ui.register_pushButton->setCursor(Qt::PointingHandCursor);

    connect(ui.close_toolButton, &QToolButton::clicked,
        this, &Login::on_close_toolButton_clicked);

    connect(ui.register_pushButton, &QPushButton::clicked,
        this, &Login::on_register_pushButton_clicked);
     
    connect(ui.login_pushButton, &QPushButton::clicked,
        this, &Login::on_login_pushButton_clicked);
    
    connect(ui.password_lineEdit, &QLineEdit::returnPressed,
        this, &Login::on_login_pushButton_clicked);
}

Login::~Login()
{}

void Login::setSubmitting(bool submitting)
{
    ui.login_pushButton->setEnabled(!submitting);
    ui.register_pushButton->setEnabled(!submitting);
    ui.username_lineEdit->setEnabled(!submitting);
    ui.password_lineEdit->setEnabled(!submitting);
}

void Login::on_close_toolButton_clicked()
{
    this->close();
}


void Login::on_register_pushButton_clicked()
{
    emit registerRequested();
}

void Login::on_login_pushButton_clicked()
{
    const QString username = ui.username_lineEdit->text().trimmed();
    const QString password = ui.password_lineEdit->text();

    if (username.isEmpty() || password.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("用户名和密码不能为空"));
        return;
    }
    emit loginRequested(username, password, 1);
}

void Login::showErrorMessage(const QString& msg)
{
    //ui.errorLabel->setStyleSheet("color: #E05A5A;");  // 红色
    ui.errorLabel->setText(msg);
    ui.errorLabel->show();
}
