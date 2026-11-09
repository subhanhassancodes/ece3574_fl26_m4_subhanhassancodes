#include "aiws/gui/main_window.hpp"
#include "aiws/remote_protocol.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QObject>
#include <QTimer>

#include <csignal>
#include <iostream>

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void on_stop_signal(int) {
    g_stop_requested = 1;
}

// Turns SIGINT/SIGTERM into a normal quit so the socket file gets removed.
class StopWatcher : public QObject {
    Q_OBJECT

public:
    explicit StopWatcher(QObject* parent) : QObject(parent), timer_(this) {
        connect(&timer_, &QTimer::timeout, this, &StopWatcher::check);
        timer_.start(100);
    }

private slots:
    void check() {
        if (g_stop_requested) {
            QApplication::quit();
        }
    }

private:
    QTimer timer_;
};

void confirm_exit(QWidget* parent) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        parent, "Exit", "Are you sure you want to exit AI Workspace?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer == QMessageBox::Yes) {
        QApplication::quit();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QCommandLineParser parser;
    const QCommandLineOption delay_option(
        "build-delay-ms", "Extra build time per document in ms.", "N", "0");
    const QCommandLineOption socket_option(
        "ipc-socket", "Unix socket path for aiws_send.", "PATH",
        QString::fromStdString(aiws::remote::default_socket_path()));
    parser.addOption(delay_option);
    parser.addOption(socket_option);
    parser.process(app);

    bool ok = false;
    const int delay = parser.value(delay_option).toInt(&ok);
    if (!ok || delay < 0) {
        std::cerr << "--build-delay-ms needs a non-negative integer\n";
        return 1;
    }

    aiws::gui::MainWindow window;
    window.set_exit_callback(&confirm_exit);
    window.set_build_delay_ms(delay);

    const QString socket_path = parser.value(socket_option);
    if (!window.start_remote_add(socket_path)) {
        std::cerr << "could not listen on " << socket_path.toStdString() << "\n";
    }

    std::signal(SIGINT, on_stop_signal);
    std::signal(SIGTERM, on_stop_signal);
    StopWatcher watcher(&app);

    window.show();
    return app.exec();
}

#include "main.moc"