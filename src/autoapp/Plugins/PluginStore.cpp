/*
*  This file is part of openauto project.
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTimer>
#include <QTextStream>
#include <f1x/openauto/autoapp/Core/ChildProcesses.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginStore.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace plugins
{

namespace
{

const QString cOfficialRepository = QStringLiteral("average-web-dev/openhu-plugins");

const char* cDefaultSources =
    "# OpenHU plugin sources, one per line. The first source wins if several offer the same plugin.\n"
    "#   github <user>/<repo> [branch]\n"
    "#   url    https://host/path/\n"
    "#   file   /path/to/repository\n"
    "github average-web-dev/openhu-plugins main\n";

QString sourcesPath()
{
    return PluginManager::baseDir() + "/sources.list";
}

QString installedPath()
{
    return PluginManager::baseDir() + "/installed.json";
}

QVariantMap readJsonFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object().toVariantMap() : QVariantMap();
}

void writeJsonFile(const QString& path, const QVariantMap& data)
{
    QFile file(path);
    if(file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(QJsonObject::fromVariantMap(data)).toJson());
    }
}

bool archCompatible(const QVariantMap& package)
{
    const QStringList arch = package.value("arch").toStringList();
    const bool listed = arch.isEmpty() || arch.contains("all") || arch.contains(PluginStore::currentArch());
    // one archive per architecture: there has to be one for this machine
    return listed
        && (!package.contains("archives") || !PluginStore::archiveFor(package, PluginStore::currentArch()).isEmpty());
}

bool compatible(const QVariantMap& package)
{
    return archCompatible(package) && PluginManager::compatible(package);
}

// Plugin ids become folder names
bool validId(const QString& id)
{
    static const QRegularExpression pattern("^[A-Za-z0-9][A-Za-z0-9._-]*$");
    return pattern.match(id).hasMatch();
}

}

QUrl PluginStore::Source::base() const
{
    if(type == "github")
    {
        return QUrl(QString("https://raw.githubusercontent.com/%1/%2/").arg(location, branch.isEmpty() ? "main" : branch));
    }
    if(type == "file")
    {
        return QUrl::fromLocalFile(location.endsWith('/') ? location : location + "/");
    }
    return QUrl(location.endsWith('/') ? location : location + "/");
}

QString PluginStore::Source::line() const
{
    return branch.isEmpty() ? type + " " + location : type + " " + location + " " + branch;
}

bool PluginStore::Source::official() const
{
    return type == "github" && location == cOfficialRepository;
}

PluginStore::PluginStore(PluginManager& manager, QObject* parent)
    : QObject(parent)
    , manager_(manager)
{
    network_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    this->loadSources();
    this->loadCache();
}

QString PluginStore::currentArch()
{
    const QString arch = QSysInfo::currentCpuArchitecture();
    if(arch == "x86_64")
    {
        return "amd64";
    }
    if(arch == "arm")
    {
        return "armhf";
    }
    return arch;  // arm64, i386, ...
}

QVariantMap PluginStore::archiveFor(const QVariantMap& package, const QString& arch)
{
    if(package.contains("archives"))
    {
        const QVariantMap archive = package.value("archives").toMap().value(arch).toMap();
        return archive.value("archive").toString().isEmpty() ? QVariantMap() : archive;
    }
    if(package.value("archive").toString().isEmpty())
    {
        return QVariantMap();
    }
    return QVariantMap{{"archive", package.value("archive")}, {"sha256", package.value("sha256")},
                       {"size", package.value("size")}};
}

int PluginStore::compareVersions(const QString& a, const QString& b)
{
    const QStringList left = a.split('.');
    const QStringList right = b.split('.');
    for(int i = 0; i < std::max(left.size(), right.size()); ++i)
    {
        const int l = i < left.size() ? left[i].toInt() : 0;
        const int r = i < right.size() ? right[i].toInt() : 0;
        if(l != r)
        {
            return l < r ? -1 : 1;
        }
    }
    return 0;
}

QVariantList PluginStore::packages() const
{
    const QVariantMap installed = this->installed();
    QVariantList result;
    QStringList seen;

    for(int i = 0; i < indexes_.size(); ++i)
    {
        for(const auto& entry: indexes_[i])
        {
            QVariantMap package = entry.toMap();
            const QString id = package.value("id").toString();
            if(seen.contains(id))
            {
                continue;
            }
            seen.append(id);

            const QString installedVersion = installed.value(id).toMap().value("version").toString();
            const bool isInstalled = manager_.contains(id);
            if(package.contains("archives"))
            {
                // archive, sha256 and size of the one for this machine (the store page shows the size)
                package.insert(archiveFor(package, currentArch()));
            }
            package["source"] = sources_[i].line();
            package["official"] = sources_[i].official();
            package["compatible"] = compatible(package);
            package["installed"] = isInstalled;
            package["installedVersion"] = isInstalled ? manager_.plugin(id).value("version").toString() : installedVersion;
            // Newer, or the same version for this plugin API while the installed copy is for another one
            const bool newer = compareVersions(package["installedVersion"].toString(), package.value("version").toString()) < 0;
            const bool installedCompatible = isInstalled && manager_.plugin(id).value("compatible", true).toBool();
            package["updateAvailable"] = isInstalled && package["compatible"].toBool() && (newer || !installedCompatible);
            result.append(package);
        }
    }

    // Installed without a source (copied by hand or the source was removed)
    for(const auto& plugin: manager_.plugins())
    {
        const QVariantMap map = plugin.toMap();
        if(!seen.contains(map.value("id").toString()))
        {
            QVariantMap package = map;
            package["installed"] = true;
            package["installedVersion"] = map.value("version");
            package["updateAvailable"] = false;
            package["compatible"] = map.value("compatible", true);
            package["source"] = QString();
            package["official"] = false;
            result.append(package);
        }
    }

    return result;
}

QVariantList PluginStore::sources() const
{
    QVariantList result;
    for(int i = 0; i < sources_.size(); ++i)
    {
        const auto& source = sources_[i];
        result.append(QVariantMap{{"index", i}, {"type", source.type}, {"location", source.location},
                                  {"branch", source.branch}, {"line", source.line()}, {"official", source.official()},
                                  {"packages", i < indexes_.size() ? indexes_[i].size() : 0}});
    }
    return result;
}

bool PluginStore::busy() const
{
    return busy_;
}

QString PluginStore::status() const
{
    return status_;
}

void PluginStore::refresh()
{
    for(int i = 0; i < sources_.size(); ++i)
    {
        this->enqueue(qtTrId("store.refreshing").arg(sources_[i].line()), [this, i](std::function<void()> done) {
            this->fetchIndex(i, std::move(done));
        });
    }
    this->enqueue(QString(), [this](std::function<void()> done) {
        emit changed();
        done();
    });
}

void PluginStore::install(const QString& id)
{
    this->enqueue(qtTrId("store.installing").arg(id), [this, id](std::function<void()> done) {
        if(!manager_.changeBlocked(id).isEmpty())
        {
            this->setStatus(qtTrId("store.disconnect_phone").arg(id), true);
            done();
            return;
        }
        this->installPackage(id, std::move(done));
    });
}

void PluginStore::updateAll()
{
    for(const auto& entry: this->packages())
    {
        const QVariantMap package = entry.toMap();
        if(package.value("updateAvailable").toBool() && package.value("compatible").toBool())
        {
            this->install(package.value("id").toString());
        }
    }
}

void PluginStore::remove(const QString& id)
{
    this->enqueue(qtTrId("store.removing").arg(id), [this, id](std::function<void()> done) {
        if(!validId(id) || !QDir(PluginManager::pluginsDir() + "/" + id).exists())
        {
            this->setStatus(qtTrId("store.plugin_not_installed").arg(id), true);
            done();
            return;
        }
        if(!manager_.changeBlocked(id).isEmpty())
        {
            this->setStatus(qtTrId("store.disconnect_phone").arg(id), true);
            done();
            return;
        }

        manager_.unload(id);
        this->afterQmlReleased([this, id, done]() {
            QDir(PluginManager::pluginsDir() + "/" + id).removeRecursively();

            QVariantMap installed = this->installed();
            installed.remove(id);
            this->saveInstalled(installed);

            manager_.load();
            this->setStatus(qtTrId("store.removed").arg(id));
            emit changed();
            done();
        });
    });
}

QString PluginStore::addSource(const QString& type, const QString& location, const QString& branch)
{
    Source source{type.trimmed(), location.trimmed(), branch.trimmed()};
    if(source.type != "github" && source.type != "url" && source.type != "file")
    {
        return qtTrId("store.type_must_github_url");
    }
    if(source.location.isEmpty() || source.location.contains(' '))
    {
        return qtTrId("store.invalid_address");
    }
    if(source.type == "github")
    {
        // also accept https://github.com/<user>/<repo>
        source.location.remove(QRegularExpression("^(https?://)?github\\.com/")).remove(QRegularExpression("(\\.git)?/?$"));
        if(source.location.count('/') != 1)
        {
            return qtTrId("store.give_github_source_user");
        }
    }
    for(const auto& existing: sources_)
    {
        if(existing.line() == source.line())
        {
            return qtTrId("store.source_already_listed");
        }
    }

    sources_.append(source);
    indexes_.append(QVariantList());
    this->saveSources();
    emit changed();

    const int index = sources_.size() - 1;
    this->enqueue(qtTrId("store.refreshing").arg(source.line()), [this, index](std::function<void()> done) {
        this->fetchIndex(index, [this, done]() {
            emit changed();
            done();
        });
    });
    return QString();
}

QString PluginStore::removeSource(int index)
{
    if(busy_)
    {
        return qtTrId("store.please_wait_store_busy");
    }
    if(index < 0 || index >= sources_.size())
    {
        return qtTrId("store.unknown_source");
    }

    sources_.removeAt(index);
    indexes_.removeAt(index);
    this->saveSources();
    emit changed();
    return QString();
}

void PluginStore::enqueue(const QString& description, Job job)
{
    queue_.emplace_back(description, std::move(job));
    if(!busy_)
    {
        this->runNext();
    }
}

void PluginStore::runNext()
{
    if(queue_.empty())
    {
        busy_ = false;
        emit stateChanged();
        core::EventBus::instance().publish("store.state", {{"busy", false}, {"status", status_}});
        return;
    }

    busy_ = true;
    auto next = std::move(queue_.front());
    queue_.pop_front();
    if(!next.first.isEmpty())
    {
        this->setStatus(next.first + " …");
    }
    next.second([this]() { QMetaObject::invokeMethod(this, [this]() { this->runNext(); }, Qt::QueuedConnection); });
}

void PluginStore::setStatus(const QString& status, bool error)
{
    status_ = status;
    if(error)
    {
        OPENAUTO_LOG(warning) << "[PluginStore] " << status.toStdString();
    }
    emit stateChanged();
    core::EventBus::instance().publish("store.state", {{"busy", busy_}, {"status", status}, {"error", error}});
}

void PluginStore::loadSources()
{
    QDir().mkpath(PluginManager::baseDir());
    if(!QFile::exists(sourcesPath()))
    {
        QFile file(sourcesPath());
        if(file.open(QIODevice::WriteOnly))
        {
            file.write(cDefaultSources);
        }
    }

    sources_.clear();
    QFile file(sourcesPath());
    if(file.open(QIODevice::ReadOnly))
    {
        QTextStream stream(&file);
        while(!stream.atEnd())
        {
            const QString line = stream.readLine().section('#', 0, 0).trimmed();
            const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if(parts.size() >= 2)
            {
                sources_.append({parts[0], parts[1], parts.value(2)});
            }
        }
    }

    indexes_.clear();
    for(int i = 0; i < sources_.size(); ++i)
    {
        indexes_.append(QVariantList());
    }
}

void PluginStore::saveSources() const
{
    // Keep the comment header, rewrite the entries
    QFile file(sourcesPath());
    if(file.open(QIODevice::WriteOnly))
    {
        QTextStream stream(&file);
        for(const QString& line: QString(cDefaultSources).split('\n'))
        {
            if(line.startsWith('#'))
            {
                stream << line << '\n';
            }
        }
        for(const auto& source: sources_)
        {
            stream << source.line() << '\n';
        }
    }
}

QString PluginStore::cachePath(const Source& source) const
{
    const QByteArray hash = QCryptographicHash::hash(source.line().toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    return PluginManager::baseDir() + "/cache/index-" + QString::fromLatin1(hash) + ".json";
}

void PluginStore::loadCache()
{
    for(int i = 0; i < sources_.size(); ++i)
    {
        indexes_[i] = readJsonFile(this->cachePath(sources_[i])).value("plugins").toList();
    }
}

void PluginStore::fetchIndex(int sourceIndex, std::function<void()> done)
{
    if(sourceIndex >= sources_.size())
    {
        done();
        return;
    }

    const Source source = sources_[sourceIndex];
    QNetworkReply* reply = network_.get(QNetworkRequest(source.base().resolved(QUrl("index.json"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, source, done]() {
        reply->deleteLater();

        // The list may have changed while downloading
        const int index = sources_.indexOf(source);
        if(index < 0)
        {
            done();
            return;
        }

        const QByteArray data = reply->readAll();
        const QJsonDocument document = QJsonDocument::fromJson(data);
        if(reply->error() != QNetworkReply::NoError || !document.isObject())
        {
            this->setStatus(source.line() + ": " + (reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                                              : qtTrId("store.index_json_invalid")), true);
            done();
            return;
        }

        indexes_[index] = document.object().value("plugins").toArray().toVariantList();
        QDir().mkpath(PluginManager::baseDir() + "/cache");
        QFile cache(this->cachePath(source));
        if(cache.open(QIODevice::WriteOnly))
        {
            cache.write(data);
        }
        this->setStatus(qtTrId("store.source_plugins").arg(source.line()).arg(indexes_[index].size()));
        done();
    });
}

QVariantMap PluginStore::findPackage(const QString& id, int* sourceIndex) const
{
    for(int i = 0; i < indexes_.size(); ++i)
    {
        for(const auto& entry: indexes_[i])
        {
            const QVariantMap package = entry.toMap();
            if(package.value("id").toString() == id)
            {
                if(sourceIndex != nullptr)
                {
                    *sourceIndex = i;
                }
                return package;
            }
        }
    }
    return QVariantMap();
}

void PluginStore::installPackage(const QString& id, std::function<void()> done)
{
    int sourceIndex = -1;
    const QVariantMap package = this->findPackage(id, &sourceIndex);
    if(package.isEmpty() || !validId(id))
    {
        this->setStatus(qtTrId("store.plugin_not_offered_any").arg(id), true);
        done();
        return;
    }
    if(!archCompatible(package))
    {
        this->setStatus(qtTrId("store.not_available").arg(id, currentArch()), true);
        done();
        return;
    }
    if(!PluginManager::compatible(package))
    {
        this->setStatus(qtTrId("store.another_openhu_version_plugin")
                            .arg(id).arg(package.value("api", 1).toInt()).arg(PluginManager::cApiVersion), true);
        done();
        return;
    }

    const Source source = sources_[sourceIndex];
    const QVariantMap download = archiveFor(package, currentArch());
    const QUrl url = source.base().resolved(QUrl(download.value("archive").toString()));
    const QString sha256 = download.value("sha256").toString().toLower();
    const QString version = package.value("version").toString();

    QNetworkReply* reply = network_.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, url, sha256, version, source, done]() {
        reply->deleteLater();
        if(reply->error() != QNetworkReply::NoError)
        {
            this->setStatus(qtTrId("store.download_failed").arg(id, reply->errorString()), true);
            done();
            return;
        }

        const QByteArray archive = reply->readAll();
        const QString actual = QString::fromLatin1(QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex());
        if(sha256.isEmpty() || actual != sha256)
        {
            this->setStatus(qtTrId("store.checksum_does_not_match").arg(id), true);
            done();
            return;
        }

        QDir().mkpath(PluginManager::baseDir() + "/tmp");
        const QString archivePath = PluginManager::baseDir() + "/tmp/" + id + ".tar.gz";
        QFile file(archivePath);
        if(!file.open(QIODevice::WriteOnly) || file.write(archive) != archive.size())
        {
            this->setStatus(qtTrId("store.cannot_save_archive").arg(id), true);
            done();
            return;
        }
        file.close();

        QString error;
        const QString staging = this->unpack(id, archivePath, error);
        QFile::remove(archivePath);
        if(staging.isEmpty())
        {
            this->setStatus(id + ": " + error, true);
            done();
            return;
        }
        // PluginManager loads by the manifest in the archive, not by the index: check that one before the
        // installed version is replaced
        const QVariantMap manifest = readJsonFile(staging + "/manifest.json");
        if(!PluginManager::compatible(manifest) || !archCompatible(manifest))
        {
            QDir(PluginManager::baseDir() + "/tmp/unpack-" + id).removeRecursively();
            this->setStatus(qtTrId("store.archive_another_openhu_version")
                                .arg(id).arg(manifest.value("api", 1).toInt()).arg(PluginManager::cApiVersion), true);
            done();
            return;
        }

        manager_.unload(id);
        this->afterQmlReleased([this, id, staging, version, source, done]() {
            const QString target = PluginManager::pluginsDir() + "/" + id;
            QDir(target).removeRecursively();
            QDir().mkpath(PluginManager::pluginsDir());
            const bool moved = QDir().rename(staging, target);
            QDir(PluginManager::baseDir() + "/tmp/unpack-" + id).removeRecursively();
            if(!moved)
            {
                this->setStatus(qtTrId("store.cannot_create_plugin_folder").arg(id), true);
                manager_.load();
                done();
                return;
            }

            QVariantMap installed = this->installed();
            installed.insert(id, QVariantMap{{"version", version}, {"source", source.line()}});
            this->saveInstalled(installed);

            manager_.load();
            this->setStatus(qtTrId("store.plugin_installed").arg(id, version));
            emit changed();
            done();
        });
    });
}

void PluginStore::afterQmlReleased(std::function<void()> next)
{
    // The shell destroys the plugin's pages asynchronously, then its cached QML can be dropped.
    QTimer::singleShot(300, this, [this, next]() {
        manager_.clearQmlCache();
        next();
    });
}

QString PluginStore::unpack(const QString& id, const QString& archive, QString& error)
{
    // Unpack next to the plugins (same file system, so the folder can be moved in place)
    const QString staging = PluginManager::baseDir() + "/tmp/unpack-" + id;
    QDir(staging).removeRecursively();
    QDir().mkpath(staging + "/content");

    QProcess tar;
    core::startChild(tar, "tar", {"-xzf", archive, "-C", staging + "/content", "--no-same-owner"});
    if(!tar.waitForFinished(60000) || tar.exitCode() != 0)
    {
        error = qtTrId("store.cannot_unpack_archive").arg(QString::fromLocal8Bit(tar.readAllStandardError()).trimmed());
        QDir(staging).removeRecursively();
        return QString();
    }

    // Archive contains either <id>/manifest.json or manifest.json at the top
    QString root = staging + "/content";
    if(!QFile::exists(root + "/manifest.json") && QFile::exists(root + "/" + id + "/manifest.json"))
    {
        root += "/" + id;
    }

    if(readJsonFile(root + "/manifest.json").value("id").toString() != id)
    {
        error = qtTrId("store.manifest_json_missing_has");
        QDir(staging).removeRecursively();
        return QString();
    }
    return root;
}

QVariantMap PluginStore::installed() const
{
    return readJsonFile(installedPath());
}

void PluginStore::saveInstalled(const QVariantMap& installed) const
{
    writeJsonFile(installedPath(), installed);
}

}
}
}
}
