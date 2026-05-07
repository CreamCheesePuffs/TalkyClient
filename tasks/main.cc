#include "tasks/TalkyClient.h"
#include "utils/stdafx.h"
#include "utils/IULog.h"
#include <QtWidgets/QApplication>
#include <QString>
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>
#include <iostream>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QString appPath = QCoreApplication::applicationDirPath() + "/";
    QDir().mkpath(appPath + "Logs");

    QString logFilePath = QString("%1Logs/%2_%3.log")
        .arg(appPath)
        .arg(QDateTime::currentDateTime().toString("yyyyMMddHHmmss"))
        .arg(QCoreApplication::applicationPid());
    std::wstring ws = logFilePath.toStdWString();

    bool logInited = CIULog::Init(true, false, ws.c_str());
    if (!logInited)
    {
        qWarning() << "failed to init log file:" << logFilePath;
    }
    else
    {
        LOG_INFO("logger initialized");
    }
    
    app.setWindowIcon(QIcon(":/imgs/app.ico"));

    
    TalkyClient talkyClient;
    talkyClient.start("1.13.156.179", 12345);

    return app.exec();
}
