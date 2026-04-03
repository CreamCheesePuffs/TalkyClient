#include "SearchFriend.h"
#include "DragWidgetFilter.h"

SearchFriend::SearchFriend(QWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    setWindowFlag(Qt::FramelessWindowHint);
    this->installEventFilter(new DragWidgetFilter(this));

    connect(ui.closeButton, &QToolButton::clicked,
        this, &SearchFriend::on_closeButton_clicked);
}

SearchFriend::~SearchFriend()
{}

void SearchFriend::on_closeButton_clicked()
{
    this->close();
}
