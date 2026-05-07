#include "Signin.h"
#include "DragWidgetFilter.h"
#include <QMessageBox>

Signin::Signin(QWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    setWindowFlag(Qt::FramelessWindowHint);
    this->installEventFilter(new DragWidgetFilter(this));

    connect(ui.close_toolButton, &QToolButton::clicked,
        this, &Signin::on_close_toolButton_clicked);

    connect(ui.register_pushButton, &QPushButton::clicked,
        this, &Signin::on_register_pushButton_clicked);
}

Signin::~Signin()
{

}

void Signin::on_close_toolButton_clicked()
{
    this->close();
}

void Signin::setSubmitting(bool submitting)
{
    ui.register_pushButton->setEnabled(!submitting);
    ui.username_lineEdit->setEnabled(!submitting);
    ui.nickname_lineEdit->setEnabled(!submitting);
    ui.password_lineEdit->setEnabled(!submitting);
}

void Signin::on_register_pushButton_clicked()
{
    const QString username = ui.username_lineEdit->text().trimmed();
    const QString password = ui.password_lineEdit->text();
    const QString nickname = ui.nickname_lineEdit->text();

    if (username.isEmpty() || password.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("用户名和密码不能为空"));
        return;
    }

    // 当前注册页没有昵称输入框，这里先用用户名作为 nickname。
    emit registerSubmitted(username, nickname, password);
}

void Signin::showResultMessage(const QString& msg, bool success)
{
    if (success) {
        ui.resultLabel->setStyleSheet("color: #4CAF50;");  // 绿色
    }
    else {
        ui.resultLabel->setStyleSheet("color: #E05A5A;");  // 红色
    }

    ui.resultLabel->setText(msg);
    ui.resultLabel->show();
}