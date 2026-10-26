QT       += core gui widgets testlib

TARGET   = QtCalculatorShots
TEMPLATE = app

CONFIG  += c++17 console

DEFINES += QT_DEPRECATED_WARNINGS

# 复用主程序的界面与逻辑，仅替换入口为自动化截图工具
SOURCES += \
    tools/screenshot_tool.cpp \
    calculator.cpp

HEADERS += \
    calculator.h

FORMS += \
    calculator.ui

# 输出到独立的 build-shots 目录，避免与主程序产物互相污染
OBJECTS_DIR = build-shots/obj
MOC_DIR     = build-shots/moc
RCC_DIR     = build-shots/rcc
UI_DIR      = build-shots/ui
