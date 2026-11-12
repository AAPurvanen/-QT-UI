#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("超声波距离监测与安全预警系统");
    a.setWindowIcon(QIcon());
    MainWindow w;
    w.show();
    return a.exec();
}
