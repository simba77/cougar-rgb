#include "main_window.hpp"

#include <QApplication>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Cougar RGB");
    QApplication::setDesktopFileName("cougar-rgb");
    cougar::MainWindow window;
    window.show();

    // Window snapshot for the docs: COUGAR_RGB_SCREENSHOT=out.png cougar-rgb-gui
    if (const QString shot = qEnvironmentVariable("COUGAR_RGB_SCREENSHOT"); !shot.isEmpty())
        QTimer::singleShot(1200, &window, [&window, shot] {
            window.grab().save(shot);
            QApplication::quit();
        });
    return app.exec();
}
