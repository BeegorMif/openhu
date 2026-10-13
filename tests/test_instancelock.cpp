#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <QtTest>
#include <f1x/openauto/autoapp/Core/InstanceLock.hpp>

using f1x::openauto::autoapp::core::InstanceLock;

class TestInstanceLock: public QObject
{
    Q_OBJECT

private slots:
    // flock is per open file: a second lock fails in the same process as well
    void secondInstanceFails()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("openhu.lock");
        InstanceLock first(path);
        QVERIFY(first.acquire());
        QVERIFY(first.acquire());

        InstanceLock second(path);
        QVERIFY(!second.acquire());
        QCOMPARE(second.error(), QString());
        QCOMPARE(second.holder(), qint64(::getpid()));
    }

    void freeAfterTheHolderEnds()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("openhu.lock");
        {
            InstanceLock first(path);
            QVERIFY(first.acquire());
        }
        InstanceLock second(path);
        QVERIFY(second.acquire());
    }

    // kill -9 (or a crash): the kernel frees the lock, the new instance takes over and writes its own pid
    void takeOverAfterKill()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("openhu.lock");
        int ready[2];
        QCOMPARE(::pipe(ready), 0);
        const pid_t child = ::fork();
        QVERIFY(child >= 0);
        if(child == 0)
        {
            InstanceLock lock(path);
            const char state = lock.acquire() ? '1' : '0';
            [[maybe_unused]] const auto written = ::write(ready[1], &state, 1);
            ::pause();
            ::_exit(0);
        }
        ::close(ready[1]);
        char state = 0;
        QCOMPARE(::read(ready[0], &state, 1), ssize_t(1));
        ::close(ready[0]);
        QCOMPARE(state, '1');

        InstanceLock lock(path);
        QVERIFY(!lock.acquire());
        QCOMPARE(lock.holder(), qint64(child));

        ::kill(child, SIGKILL);
        ::waitpid(child, nullptr, 0);
        // the pid of the killed one is still in the file, but that process does not run any more
        QCOMPARE(lock.holder(), qint64(0));
        QVERIFY(lock.acquire());
        QCOMPARE(lock.holder(), qint64(::getpid()));
    }

    // every user can write its pid into the file
    void writableForEveryone()
    {
        QTemporaryDir dir;
        InstanceLock lock(dir.filePath("openhu.lock"));
        QVERIFY(lock.acquire());
        struct stat info{};
        QCOMPARE(::stat(QFile::encodeName(lock.path()).constData(), &info), 0);
        QCOMPARE(info.st_mode & 0777, mode_t(0666));
    }

    // the restart of OpenHU (execv) must not keep the lock of the old process
    void closeOnExec()
    {
        QTemporaryDir dir;
        InstanceLock lock(dir.filePath("openhu.lock"));
        QVERIFY(lock.acquire());
        bool found = false;
        for(int fd = 3; fd < 1024 && !found; ++fd)
        {
            char target[4096];
            const auto length = ::readlink(QByteArray("/proc/self/fd/" + QByteArray::number(fd)).constData(), target,
                                           sizeof(target) - 1);
            if(length > 0 && QByteArray(target, length) == QFile::encodeName(lock.path()))
            {
                found = true;
                QVERIFY(::fcntl(fd, F_GETFD) & FD_CLOEXEC);
            }
        }
        QVERIFY(found);
    }

    void errorWithoutFile()
    {
        InstanceLock lock("/nonexistent-dir/openhu.lock");
        QVERIFY(!lock.acquire());
        QVERIFY(lock.error().contains("/nonexistent-dir/openhu.lock"));
    }

    void defaultPath()
    {
        QVERIFY(InstanceLock::defaultPath().endsWith("/openhu.lock"));
    }
};

QTEST_GUILESS_MAIN(TestInstanceLock)
#include "test_instancelock.moc"
