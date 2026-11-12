QT       += core gui widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
CONFIG -= app_bundle

TARGET = ChaoShenBo
TEMPLATE = app

QT += core gui widgets sql

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    systate.cpp \
    distancewidget.cpp \
    serialbridge.cpp \
    recordstore.cpp

HEADERS += \
    mainwindow.h \
    systate.h \
    distancewidget.h \
    serialbridge.h \
    recordstore.h

RESOURCES += resources.qrc

# Windows 高 DPI 感知，避免系统按比例缩放整个窗口导致显示不全
RC_FILE = app.rc
