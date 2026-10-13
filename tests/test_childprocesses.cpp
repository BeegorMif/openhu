#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <QtTest>
#include <f1x/openauto/autoapp/Core/ChildProcesses.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include "TestHome.hpp"

namespace core = f1x::openauto::autoapp::core;
using f1x::openauto::autoapp::plugins::PluginManager;

namespace
{

// gone, or a zombie nobody reaped yet (an orphan in a container without init)
bool ended(qint64 pid)
{
    QFile stat(QString("/proc/%1/stat").arg(pid));
    if(!stat.open(QIODevice::ReadOnly))
    {
        return true;
    }
    const QByteArray line = stat.readAll();
    const int end = line.lastIndexOf(')');
    return end > 0 && line.mid(end + 2, 1) == "Z";
}

QString childFds(const QString& script)
{
    QProcess process;
    core::startChild(process, "sh", {"-c", script});
    process.waitForFinished(5000);
    return QString::fromUtf8(process.readAllStandardOutput());
}

void writeFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

}

class TestChildProcesses: public QObject
{
    Q_OBJECT

private slots:
    // cage's pipe: OpenHU keeps it, its children do not get it, OpenHU's own restart (execv) keeps it
    void inheritedDescriptors()
    {
        int pipeFds[2];
        QCOMPARE(::pipe(pipeFds), 0);
        const int inherited = 200;
        QCOMPARE(::dup2(pipeFds[1], inherited), inherited);
        QVERIFY(!(::fcntl(inherited, F_GETFD) & FD_CLOEXEC));

        core::protectInheritedDescriptors();
        QVERIFY(core::isInheritedDescriptor(inherited));
        QVERIFY(::fcntl(inherited, F_GETFD) & FD_CLOEXEC);
        QVERIFY(!core::isInheritedDescriptor(STDERR_FILENO));
        QCOMPARE(childFds("test -e /proc/self/fd/200 && echo open || echo closed").trimmed(), QString("closed"));

        core::keepInheritedDescriptorsOnExec();
        QVERIFY(!(::fcntl(inherited, F_GETFD) & FD_CLOEXEC));
        QCOMPARE(childFds("test -e /proc/self/fd/200 && echo open || echo closed").trimmed(), QString("open"));

        ::close(inherited);
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
    }

    // a crash of OpenHU (here: _exit of a helper that started "sleep 60") ends its children
    void childEndsWithOpenHu()
    {
        QProcess helper;
        helper.start(QCoreApplication::applicationFilePath(), {"--start-child-and-die"});
        QVERIFY(helper.waitForFinished(10000));
        bool ok = false;
        const qint64 child = helper.readAllStandardOutput().trimmed().toLongLong(&ok);
        QVERIFY2(ok && child > 0, qPrintable(helper.readAllStandardError()));
        QTRY_VERIFY_WITH_TIMEOUT(ended(child), 5000);
    }

    void ownGroup()
    {
        QProcess process;
        core::startChild(process, "sleep", {"60"}, core::ChildGroup::OwnGroup);
        QVERIFY(process.waitForStarted());
        const auto pid = pid_t(process.processId());
        QCOMPARE(::getpgid(pid), pid);
        QCOMPARE(core::terminateGroup(process), qint64(pid));
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(core::terminateGroup(process), qint64(0));

        QProcess shared;
        core::startChild(shared, "sleep", {"60"});
        QVERIFY(shared.waitForStarted());
        QCOMPARE(::getpgid(pid_t(shared.processId())), ::getpgrp());
        QCOMPARE(core::terminateGroup(shared), qint64(0));
        QVERIFY(shared.waitForFinished(5000));
    }

    // Two plugins that ignore SIGTERM, each with a child that ignores it too: stopped together (one 3 s deadline,
    // not 3 s each) and with everything they started
    void pluginsStopTogether()
    {
        TestHome home;
        for(const QString id: {"first", "second"})
        {
            const QString dir = PluginManager::pluginsDir() + "/" + id;
            QVERIFY(QDir().mkpath(dir));
            writeFile(dir + "/manifest.json", QString(R"({"id": "%1", "name": "%1", "version": "1", "api": %2,
                "process": {"exec": "run.sh", "restart": "never"}})").arg(id).arg(PluginManager::cApiVersion).toUtf8());
            writeFile(dir + "/run.sh", R"(trap '' TERM
sh -c 'exec sleep 60' &
echo $! > "$OPENHU_DATA_DIR/child.pid"
while :; do wait; done
)");
        }

        PluginManager plugins(5520);
        plugins.load();
        QList<qint64> children;
        for(const QString id: {"first", "second"})
        {
            const QString pidFile = PluginManager::baseDir() + "/plugin-data/" + id + "/child.pid";
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(pidFile) && QFileInfo(pidFile).size() > 0, 5000);
            QFile file(pidFile);
            QVERIFY(file.open(QIODevice::ReadOnly));
            children.append(file.readAll().trimmed().toLongLong());
            QVERIFY(children.last() > 0 && !ended(children.last()));
        }

        QElapsedTimer stop;
        stop.start();
        plugins.stopAll();
        QVERIFY2(stop.elapsed() < 5000, qPrintable(QString("%1 ms").arg(stop.elapsed())));
        for(const qint64 child: children)
        {
            QTRY_VERIFY_WITH_TIMEOUT(ended(child), 2000);
        }
    }
};

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    // helper of childEndsWithOpenHu: starts a child like OpenHU does, prints its pid and dies without cleaning up
    if(argc > 1 && QByteArray(argv[1]) == "--start-child-and-die")
    {
        QProcess child;
        core::startChild(child, "sleep", {"60"});
        if(!child.waitForStarted())
        {
            return 2;
        }
        std::printf("%lld\n", static_cast<long long>(child.processId()));
        std::fflush(stdout);
        ::_exit(0);
    }
    TestChildProcesses test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_childprocesses.moc"
