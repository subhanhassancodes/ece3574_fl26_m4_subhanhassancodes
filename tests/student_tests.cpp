#include "aiws/build_worker.hpp"
#include "aiws/gui/corpus_state.hpp"
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
#include <optional>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using aiws::BuildWorker;
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

aiws::Workspace one_doc_workspace(const char* id = "a") {
    aiws::Workspace ws;
    ws.add_document(aiws::Document(id, id, kTextA));
    return ws;
}

class FakeContext final : public aiws::gui::CorpusContext {
public:
    int starts = 0;
    int cancels = 0;
    int installs = 0;
    QString corpus_label;

    void start_build() override { ++starts; }
    void cancel_build() override { ++cancels; }
    void install_corpus(std::unique_ptr<aiws::ProcessingCore>) override { ++installs; }
    void clear_outputs() override {}
    QString chunk_summary() const override { return "3 chunks"; }
    void show_corpus_status(const QString& text) override { corpus_label = text; }
    void show_status(const QString&) override {}
};

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

    void exit_when_idle_is_quick() {
        const QString sock = socket_path("idle");
        QProcess app;
        app.start(APP_PATH, {"--ipc-socket", sock});
        QVERIFY(app.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(sock), 10000);

        QElapsedTimer t;
        t.start();
        app.terminate();
        const bool exited = app.waitForFinished(3000);
        if (!exited) {
            app.kill();
            app.waitForFinished(3000);
        }
        QVERIFY2(exited, "application hung on exit with no build running");
        QVERIFY(t.elapsed() < 1000);
        QCOMPARE(app.exitStatus(), QProcess::NormalExit);
    }

    void worker_runs_every_build_on_one_thread() {
        BuildWorker worker;
        QSignalSpy spy(&worker, &BuildWorker::build_finished);
        for (int i = 0; i < 3; ++i) {
            const auto id = worker.submit(one_doc_workspace(), 0);
            QTRY_COMPARE(spy.count(), i + 1);
            std::optional<BuildWorker::Result> r = worker.take();
            QVERIFY(r.has_value());
            QCOMPARE(r->id, id);
            QVERIFY(r->core != nullptr);
            QVERIFY(r->built_on == worker.thread_id());
            QVERIFY(r->built_on != std::this_thread::get_id());
        }
    }

    void window_keeps_one_build_thread() {
        MainWindow w;
        QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));
        button(w, "Build Corpus")->click();
        QTRY_VERIFY(is_chunk_count(corpus_status(w)));
        const auto worker = w.build_thread_id();

        for (int i = 0; i < 3; ++i) {
            QVERIFY(w.add_document_file(write_file(QString("n%1.txt").arg(i), kTextB)));
            button(w, "Build Corpus")->click();
            QTRY_VERIFY(is_chunk_count(corpus_status(w)));
            QVERIFY(w.build_thread_id() == worker);
        }
    }

    void submit_does_not_wait_for_running_build() {
        BuildWorker worker;
        worker.submit(one_doc_workspace(), 3000);
        QElapsedTimer t;
        t.start();
        worker.submit(one_doc_workspace(), 0);
        worker.cancel();
        worker.submit(one_doc_workspace(), 0);
        QVERIFY(t.elapsed() < 100);
    }

    void outdated_request_is_never_built() {
        BuildWorker worker;
        QSignalSpy spy(&worker, &BuildWorker::build_finished);
        worker.submit(one_doc_workspace(), 300);
        worker.submit(one_doc_workspace(), 300);
        const auto last = worker.submit(one_doc_workspace(), 0);

        QTRY_COMPARE(spy.count(), 1);
        QTest::qWait(500);
        QCOMPARE(spy.count(), 1);
        std::optional<BuildWorker::Result> r = worker.take();
        QVERIFY(r.has_value());
        QCOMPARE(r->id, last);
    }

    void cancelled_build_never_becomes_current() {
        BuildWorker worker;
        QSignalSpy spy(&worker, &BuildWorker::build_finished);
        worker.submit(one_doc_workspace(), 0);
        worker.cancel();
        QTest::qWait(300);
        QVERIFY(!worker.take().has_value());
    }

    void reset_stops_long_build() {
        MainWindow w;
        w.set_build_delay_ms(3000);
        QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));
        button(w, "Build Corpus")->click();
        QCOMPARE(corpus_status(w), QString("Building..."));
        button(w, "Reset")->click();

        w.set_build_delay_ms(0);
        QVERIFY(w.add_document_file(write_file("b.txt", kTextB)));
        button(w, "Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(is_chunk_count(corpus_status(w)), 1500);
    }

    void add_stops_long_build() {
        MainWindow w;
        w.set_build_delay_ms(3000);
        QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));
        button(w, "Build Corpus")->click();
        QVERIFY(w.add_document_file(write_file("b.txt", kTextB)));
        QVERIFY(button(w, "Build Corpus")->isEnabled());

        w.set_build_delay_ms(0);
        button(w, "Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(is_chunk_count(corpus_status(w)), 1500);
    }

    void remote_add_mid_build_cancels_it() {
        MainWindow w;
        const QString sock = socket_path("mid");
        QVERIFY(w.start_remote_add(sock));
        w.set_build_delay_ms(3000);
        QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));
        button(w, "Build Corpus")->click();
        QCOMPARE(corpus_status(w), QString("Building..."));

        QCOMPARE(run_send({"--socket", sock, "add", write_file("b.txt", kTextB)}), 0);
        QTRY_COMPARE(document_count(w), 2);
        QCOMPARE(corpus_status(w), QString("Not built"));
        QVERIFY(button(w, "Build Corpus")->isEnabled());
        QVERIFY(!button(w, "Inspect")->isEnabled());

        w.set_build_delay_ms(0);
        button(w, "Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(is_chunk_count(corpus_status(w)), 1500);
    }

    void build_stays_disabled_while_building() {
        MainWindow w;
        w.set_build_delay_ms(2000);
        QVERIFY(w.add_document_file(write_file("a.txt", kTextA)));
        button(w, "Build Corpus")->click();
        QVERIFY(!button(w, "Build Corpus")->isEnabled());
        button(w, "Build Corpus")->click();
        QCOMPARE(corpus_status(w), QString("Building..."));
    }

    void states_follow_the_lifecycle() {
        using namespace aiws::gui;
        FakeContext ctx;

        std::unique_ptr<CorpusState> s = std::make_unique<EmptyState>();
        QVERIFY(!s->can_build());
        QVERIFY(s->build_requested(ctx) == nullptr);
        QCOMPARE(ctx.starts, 0);

        s = s->workspace_changed(ctx);
        s->enter(ctx);
        QVERIFY(s->can_build());
        QCOMPARE(ctx.corpus_label, QString("Not built"));

        s = s->build_requested(ctx);
        s->enter(ctx);
        QCOMPARE(ctx.starts, 1);
        QVERIFY(!s->can_build());
        QVERIFY(!s->corpus_current());

        s = s->build_succeeded(ctx, std::make_unique<aiws::ProcessingCore>());
        s->enter(ctx);
        QCOMPARE(ctx.installs, 1);
        QVERIFY(s->corpus_current());
        QCOMPARE(ctx.corpus_label, QString("3 chunks"));

        s = s->workspace_changed(ctx);
        s->enter(ctx);
        QVERIFY(!s->corpus_current());
        QCOMPARE(ctx.corpus_label, QString("Out of date (rebuild required)"));
        QCOMPARE(ctx.cancels, 0);

        s = s->build_requested(ctx);
        s->enter(ctx);
        s = s->reset(ctx);
        s->enter(ctx);
        QCOMPARE(ctx.cancels, 1);
        QVERIFY(!s->can_build());

        s = s->workspace_changed(ctx);
        s = s->build_requested(ctx);
        s = s->build_failed(ctx, "boom");
        s->enter(ctx);
        QCOMPARE(ctx.corpus_label, QString("Build failed"));
        QVERIFY(s->can_build());
        QVERIFY(s->build_succeeded(ctx, std::make_unique<aiws::ProcessingCore>()) == nullptr);
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

        QCOMPARE(corpus_status(w), QString("Not built"));
        QTest::qWait(400);
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

        QTest::qWait(400);
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

        QTRY_VERIFY(is_chunk_count(corpus_status(w)));

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