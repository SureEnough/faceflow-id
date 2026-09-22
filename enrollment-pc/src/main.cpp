// enrollment-pc/src/main.cpp
// 录入电脑端入口（架构文档 4.2）：Qt Widgets 应用。
#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  enroll::MainWindow w;
  w.show();
  return app.exec();
}