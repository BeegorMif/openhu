#pragma once

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

// Temporary HOME (~/.openhu) and working directory (openhu.ini) for one test, removed afterwards.
class TestHome
{
public:
    TestHome()
        : oldHome_(qgetenv("HOME"))
        , oldDir_(QDir::currentPath())
    {
        QVERIFY(dir_.isValid());
        qputenv("HOME", dir_.path().toLocal8Bit());
        QDir::setCurrent(dir_.path());
    }

    ~TestHome()
    {
        QDir::setCurrent(oldDir_);
        qputenv("HOME", oldHome_);
    }

    QString path(const QString& relative = QString()) const
    {
        return relative.isEmpty() ? dir_.path() : dir_.filePath(relative);
    }

private:
    QTemporaryDir dir_;
    QByteArray oldHome_;
    QString oldDir_;
};
