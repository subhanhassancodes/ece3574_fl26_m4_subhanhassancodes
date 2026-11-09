#include "aiws/corpus_builder.hpp"
#include "aiws/gui/main_window.hpp"
#include "aiws/remote_add_server.hpp"
#include "aiws/remote_protocol.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using aiws::CorpusBuilder;
using aiws::RemoteAddServer;
using aiws::gui::MainWindow;

namespace {

const char* kTextA =
    "Threads let a program do work in the background while the window stays "
    "responsive. A worker thread builds the corpus.\n\nThe GUI thread keeps "
    "handling clicks and repaints.";
const char* kTextB =
    "Interprocess communication moves messages between programs. A Unix domain "
    "socket is one way to do it on Linux.";

QPushButton* button(MainWindow& w, const QString& text) {
    for (QPushButton* b : w.findChildren<QPushButton*>()) {
        if (b->text() == text) return b;
    }
    return nullptr;
}

QString corpus_status(MainWindow& w) {
    return w.findChild<QLabel*>("corpus_status")->text();
}

int document_count(MainWindow& w) {
    return w.findChild<QListWidget*>()->count();
}

bool is_chunk_count(const QString& status) {
    return status.endsWith(" chunk") || status.endsWith(" chunks");
}

int run_send(const QStringList& args, const QString& working_dir = QString()) {
    QProcess p;
    if (!working_dir.isEmpty()) p.setWorkingDirectory(working_dir);
    p.start(SEND_TOOL, args);
    if (!p.waitForFinished(10000)) return -1;
    return p.exitCode();
}

}  // namespace

class StudentTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;

    QString write_file(const QString& name, const char* text) {
        const QString path = dir_.filePath(name);
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(text);
        return path;
    }

    QString socket_path(const char* name) {
        return QDir::tempPath() + "/aiws-test-" + QString::number(::getpid()) + "-" + name + ".sock";
    }

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
    }

    void unreadable_file_changes_nothing() {
        MainWindow w;
        QVERIFY(!w.add_document_file(dir_.filePath("missing.txt")));
        QCOMPARE(document_count(w), 0);
        QVERIFY(!button(w, "Build Corpus")->isEnabled());
    }

    void build_becomes_current() {
        MainWindow w;
        QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));
        button(w, "Build Corpus")->click();
        QTRY_VERIFY(is_chunk_count(corpus_status(w)));
        QVERIFY(button(w, "Inspect")->isEnabled());
        QCOMPARE(w.active_builds(), std::size_t{0});
    }

    void build_does_not_block_gui() {
        MainWindow w;
        w.set_build_delay_ms(1500);
        QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));

        QElapsedTimer t;
        t.start();
        button(w, "Build Corpus")->click();
        QVERIFY(t.elapsed() < 500);
        QCOMPARE(corpus_status(w), QString("Building..."));
        QVERIFY(!button(w, "Build Corpus")->isEnabled());
        QVERIFY(!button(w, "Inspect")->isEnabled());

        QVERIFY(w.add_document_file(write_file("b.txt", kTextB)));
        QCOMPARE(document_count(w), 2);
    }

    void build_is_stale_after_add() {
        MainWindow w;
        w.set_build_delay_ms(200);
        QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));
        button(w, "Build Corpus")->click();
        QVERIFY(w.add_document_file(write_file("b.txt", kTextB)));

        QTRY_COMPARE(w.active_builds(), std::size_t{0});
        QVERIFY(!is_chunk_count(corpus_status(w)));
        QVERIFY(!button(w, "Inspect")->isEnabled());
        QVERIFY(button(w, "Build Corpus")->isEnabled());
    }

    void build_is_stale_after_reset() {
        MainWindow w;
        w.set_build_delay_ms(200);
        QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));
        button(w, "Build Corpus")->click();
        button(w, "Reset")->click();

        QTRY_COMPARE(w.active_builds(), std::size_t{0});
        QCOMPARE(corpus_status(w), QString("Not built"));
        QCOMPARE(document_count(w), 0);
        QVERIFY(!button(w, "Inspect")->isEnabled());
    }

    void newer_build_wins_over_older() {
        MainWindow w;
        w.set_build_delay_ms(300);
        QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));
        button(w, "Build Corpus")->click();
        QVERIFY(w.add_document_file(write_file("b.txt", kTextB)));
        w.set_build_delay_ms(0);
        button(w, "Build Corpus")->click();

        QTRY_COMPARE(w.active_builds(), std::size_t{0});

        aiws::Workspace both;
        both.add_document(aiws::Document("a", "a", kTextA));
        both.add_document(aiws::Document("b", "b", kTextB));
        aiws::ProcessingCore core;
        core.rebuild(both);
        const QString expected = QString::number(core.chunk_count()) +
                                 (core.chunk_count() == 1 ? " chunk" : " chunks");
        QCOMPARE(corpus_status(w), expected);
        QVERIFY(button(w, "Inspect")->isEnabled());
    }

    void failed_build_is_reported() {
        MainWindow w;
        const QString path = write_file("dup.txt", kTextA);
        QVERIFY(w.add_document_file(path));
        QVERIFY(w.add_document_file(path));
        button(w, "Build Corpus")->click();

        QTRY_COMPARE(corpus_status(w), QString("Build failed"));
        QVERIFY(button(w, "Build Corpus")->isEnabled());
        QVERIFY(!button(w, "Inspect")->isEnabled());

        button(w, "Reset")->click();
        QVERIFY(w.add_document_file(path));
        button(w, "Build Corpus")->click();
        QTRY_VERIFY(is_chunk_count(corpus_status(w)));
    }

    void exit_during_build_is_clean() {
        for (int i = 0; i < 3; ++i) {
            auto w = std::make_unique<MainWindow>();
            w->set_build_delay_ms(5000);
            QVERIFY(w->add_document_file(write_file("a.txt", kTextA)));
            button(*w, "Build Corpus")->click();

            QElapsedTimer t;
            t.start();
            w.reset();
            QVERIFY(t.elapsed() < 1000);
        }
    }

    void builder_reports_result_and_joins() {
        CorpusBuilder builder;
        QSignalSpy spy(&builder, &CorpusBuilder::build_finished);

        aiws::Workspace ws;
        ws.add_document(aiws::Document("a", "a", kTextA));
        const auto id = builder.start(ws, 7, 0);
        QCOMPARE(builder.job_count(), std::size_t{1});

        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), static_cast<qulonglong>(id));
        CorpusBuilder::Result r = builder.take(id);
        QCOMPARE(r.version, std::uint64_t{7});
        QVERIFY(r.core != nullptr);
        QVERIFY(r.core->chunk_count() > 0);
        QCOMPARE(builder.job_count(), std::size_t{0});
    }

    void remote_add_relative_path() {
        MainWindow w;
        const QString sock = socket_path("rel");
        QVERIFY(w.start_remote_add(sock));
        write_file("remote.txt", kTextA);

        QCOMPARE(run_send({"--socket", sock, "add", "remote.txt"}, dir_.path()), 0);
        QTRY_COMPARE(document_count(w), 1);
        QCOMPARE(w.findChild<QListWidget*>()->item(0)->text(), QString("remote.txt"));
        QVERIFY(button(w, "Build Corpus")->isEnabled());
    }

    void remote_add_rejects_unreadable() {
        MainWindow w;
        const QString sock = socket_path("bad");
        QVERIFY(w.start_remote_add(sock));

        QCOMPARE(run_send({"--socket", sock, "add", dir_.filePath("nope.txt")}), 1);
        QTest::qWait(200);
        QCOMPARE(document_count(w), 0);
    }

    void remote_add_unusual_names() {
        MainWindow w;
        const QString sock = socket_path("names");
        QVERIFY(w.start_remote_add(sock));

        const QStringList names = {"with space.txt", QString::fromUtf8("caf\xc3\xa9 \xe6\x96\x87.txt"),
                                   "new\nline.txt"};
        for (const QString& name : names) {
            const QString path = write_file(name, kTextB);
            QCOMPARE(run_send({"--socket", sock, "add", path}), 0);
        }
        QTRY_COMPARE(document_count(w), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(w.findChild<QListWidget*>()->item(i)->text(), names[i]);
        }
    }

    void remote_add_in_pieces() {
        MainWindow w;
        const QString sock = socket_path("pieces");
        QVERIFY(w.start_remote_add(sock));
        const std::string path = write_file("pieces.txt", kTextA).toStdString();

        std::atomic<int> reply{-1};
        std::thread client([&] {
            const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            std::strcpy(addr.sun_path, sock.toStdString().c_str());
            if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                const std::string msg = aiws::remote::encode(aiws::remote::kAdd, path);
                for (std::size_t i = 0; i < msg.size(); i += 3) {
                    ::send(fd, msg.data() + i, std::min<std::size_t>(3, msg.size() - i), MSG_NOSIGNAL);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                std::uint8_t tag = 0;
                std::string reason;
                const auto deadline = aiws::remote::Clock::now() + std::chrono::seconds(5);
                if (aiws::remote::read_message(fd, tag, reason, deadline)) reply = tag;
            }
            ::close(fd);
        });

        QTRY_COMPARE(document_count(w), 1);
        client.join();
        QCOMPARE(reply.load(), static_cast<int>(aiws::remote::kOk));
    }

    void remote_stops_on_exit() {
        const QString sock = socket_path("exit");
        {
            MainWindow w;
            w.set_build_delay_ms(5000);
            QVERIFY(w.start_remote_add(sock));
            QVERIFY(QFileInfo::exists(sock));
            QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));
            button(w, "Build Corpus")->click();
        }
        QVERIFY(!QFileInfo::exists(sock));
        QCOMPARE(run_send({"--socket", sock, "add", dir_.filePath("a.txt")}), 2);
    }

    void server_stop_is_quick() {
        RemoteAddServer server;
        const QString sock = socket_path("quick");
        QVERIFY(server.start(sock.toStdString()));
        QElapsedTimer t;
        t.start();
        server.stop();
        QVERIFY(t.elapsed() < 500);
        QVERIFY(!server.is_running());
        QVERIFY(!QFileInfo::exists(sock));
    }

    void send_without_application() {
        QCOMPARE(run_send({"--socket", socket_path("none"), "add", "x.txt"}), 2);
    }
};

QTEST_MAIN(StudentTests)
#include "student_tests.moc"