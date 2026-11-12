QT += core testlib
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = systate_test
TEMPLATE = app

# 被测对象只依赖 QtCore（systate 不含任何界面代码），这正是分层解耦的收益
INCLUDEPATH += ..
SOURCES += ../systate.cpp systate_test.cpp
HEADERS += ../systate.h

# 输出单独目录，不与主程序构建互相干扰
DESTDIR = $$OUT_PWD/../../dist/tests
