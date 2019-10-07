/*
 * Copyright 2016-2017 Matthieu Gallien <matthieu_gallien@yahoo.fr>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "musiclistenersmanager.h"

#include "config-upnp-qt.h"

#include "indexersManager.h"

#if defined UPNPQT_FOUND && UPNPQT_FOUND
#include "upnp/upnplistener.h"
#endif

#if defined KF5Baloo_FOUND && KF5Baloo_FOUND
#include "baloo/baloolistener.h"
#include "baloo/baloodetector.h"
#endif

#if defined Qt5AndroidExtras_FOUND && Qt5AndroidExtras_FOUND
#include "android/androidmusiclistener.h"
#endif

#include "databaseinterface.h"
#include "mediaplaylist.h"
#include "file/filelistener.h"
#include "file/localfilelisting.h"
#include "trackslistener.h"
#include "notificationitem.h"
#include "elisaapplication.h"
#include "elisa_settings.h"
#include "modeldataloader.h"

#include <KI18n/KLocalizedString>

#include <QThread>
#include <QMutex>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QList>
#include <QScopedPointer>
#include <QPointer>
#include <QFileSystemWatcher>
#include <QAction>

#include <QDebug>

#include <list>

class MusicListenersManagerPrivate
{
public:

    QThread mDatabaseThread;

    QThread mListenerThread;

#if defined UPNPQT_FOUND && UPNPQT_FOUND
    UpnpListener mUpnpListener;
#endif

#if defined KF5Baloo_FOUND && KF5Baloo_FOUND
    BalooDetector mBalooDetector;

    BalooListener mBalooListener;
#endif

    FileListener mFileListener;

#if defined Qt5AndroidExtras_FOUND && Qt5AndroidExtras_FOUND
    std::unique_ptr<AndroidMusicListener> mAndroidMusicListener;
#endif

    DatabaseInterface mDatabaseInterface;

    std::unique_ptr<TracksListener> mTracksListener;

    QFileSystemWatcher mConfigFileWatcher;

    ElisaApplication *mElisaApplication = nullptr;

    int mImportedTracksCount = 0;

    bool mIndexerBusy = false;

    bool mFileSystemIndexerActive = false;

    bool mBalooIndexerActive = false;

    bool mBalooIndexerAvailable = false;

    bool mAndroidIndexerActive = false;

    bool mAndroidIndexerAvailable = false;

};

MusicListenersManager::MusicListenersManager(QObject *parent)
    : QObject(parent), d(std::make_unique<MusicListenersManagerPrivate>())
{
    d->mListenerThread.start();
    d->mDatabaseThread.start();

    d->mDatabaseInterface.moveToThread(&d->mDatabaseThread);

    connect(&d->mDatabaseInterface, &DatabaseInterface::requestsInitDone,
            this, &MusicListenersManager::databaseReady);
    connect(this, &MusicListenersManager::clearDatabase,
            &d->mDatabaseInterface, &DatabaseInterface::clearData);
    connect(&d->mDatabaseInterface, &DatabaseInterface::cleanedDatabase,
            this, &MusicListenersManager::cleanedDatabase);

#if defined KF5Baloo_FOUND && KF5Baloo_FOUND
    connect(&d->mBalooDetector, &BalooDetector::balooAvailabilityChanged,
            this, &MusicListenersManager::balooAvailabilityChanged);
#endif

    qCInfo(orgKdeElisaIndexersManager) << "Local file system indexer is inactive";
    qCInfo(orgKdeElisaIndexersManager) << "Baloo indexer is unavailable";
    qCInfo(orgKdeElisaIndexersManager) << "Baloo indexer is inactive";

    const auto &localDataPaths = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    auto databaseFileName = QString();
    if (!localDataPaths.isEmpty()) {
        QDir myDataDirectory;
        myDataDirectory.mkpath(localDataPaths.first());
        databaseFileName = localDataPaths.first() + QStringLiteral("/elisaDatabase.db");
    }

    QMetaObject::invokeMethod(&d->mDatabaseInterface, "init", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("listeners")), Q_ARG(QString, databaseFileName));

    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &MusicListenersManager::applicationAboutToQuit);

    connect(Elisa::ElisaConfiguration::self(), &Elisa::ElisaConfiguration::configChanged,
            this, &MusicListenersManager::configChanged);

    connect(&d->mConfigFileWatcher, &QFileSystemWatcher::fileChanged,
            this, &MusicListenersManager::configChanged);

    auto initialRootPath = Elisa::ElisaConfiguration::rootPath();
    if (initialRootPath.isEmpty()) {
        auto systemMusicPaths = QStandardPaths::standardLocations(QStandardPaths::MusicLocation);
        for (const auto &musicPath : qAsConst(systemMusicPaths)) {
            initialRootPath.push_back(musicPath);
        }

        Elisa::ElisaConfiguration::setRootPath(initialRootPath);
        Elisa::ElisaConfiguration::self()->save();
    }

    d->mConfigFileWatcher.addPath(Elisa::ElisaConfiguration::self()->config()->name());

    connect(&d->mDatabaseInterface, &DatabaseInterface::tracksAdded,
            this, &MusicListenersManager::increaseImportedTracksCount);
}

MusicListenersManager::~MusicListenersManager()
= default;

DatabaseInterface *MusicListenersManager::viewDatabase() const
{
    return &d->mDatabaseInterface;
}

void MusicListenersManager::subscribeForTracks(MediaPlayList *client)
{
    createTracksListener();
    connect(d->mTracksListener.get(), &TracksListener::trackHasChanged, client, &MediaPlayList::trackChanged);
    connect(d->mTracksListener.get(), &TracksListener::trackHasBeenRemoved, client, &MediaPlayList::trackRemoved);
    connect(d->mTracksListener.get(), &TracksListener::tracksListAdded, client, &MediaPlayList::tracksListAdded);
    connect(client, &MediaPlayList::newEntryInList, d->mTracksListener.get(), &TracksListener::newEntryInList);
    connect(client, &MediaPlayList::newTrackByNameInList, d->mTracksListener.get(), &TracksListener::trackByNameInList);
}

int MusicListenersManager::importedTracksCount() const
{
    return d->mImportedTracksCount;
}

ElisaApplication *MusicListenersManager::elisaApplication() const
{
    return d->mElisaApplication;
}

bool MusicListenersManager::indexerBusy() const
{
    return d->mIndexerBusy;
}

bool MusicListenersManager::fileSystemIndexerActive() const
{
    return d->mFileSystemIndexerActive;
}

bool MusicListenersManager::balooIndexerActive() const
{
    return d->mBalooIndexerActive;
}

bool MusicListenersManager::balooIndexerAvailable() const
{
    return d->mBalooIndexerAvailable;
}

bool MusicListenersManager::androidIndexerActive() const
{
    return d->mAndroidIndexerActive;
}

bool MusicListenersManager::androidIndexerAvailable() const
{
    return d->mAndroidIndexerAvailable;
}

void MusicListenersManager::databaseReady()
{
    configChanged();
}

void MusicListenersManager::applicationAboutToQuit()
{
    d->mDatabaseInterface.applicationAboutToQuit();

    Q_EMIT applicationIsTerminating();

    d->mDatabaseThread.exit();
    d->mDatabaseThread.wait();

    d->mListenerThread.exit();
    d->mListenerThread.wait();
}

void MusicListenersManager::showConfiguration()
{
    auto configureAction = d->mElisaApplication->action(QStringLiteral("options_configure"));

    configureAction->trigger();
}

void MusicListenersManager::setElisaApplication(ElisaApplication *elisaApplication)
{
    if (d->mElisaApplication == elisaApplication) {
        return;
    }

    d->mElisaApplication = elisaApplication;
    emit elisaApplicationChanged();
}

void MusicListenersManager::playBackError(const QUrl &sourceInError, QMediaPlayer::Error playerError)
{
    qCDebug(orgKdeElisaIndexersManager) << "MusicListenersManager::playBackError" << sourceInError;

    if (playerError == QMediaPlayer::ResourceError) {
        Q_EMIT removeTracksInError({sourceInError});

        if (sourceInError.isLocalFile()) {
            Q_EMIT displayTrackError(sourceInError.toLocalFile());
        } else {
            Q_EMIT displayTrackError(sourceInError.toString());
        }
    }
}

void MusicListenersManager::connectModel(ModelDataLoader *dataLoader)
{
    dataLoader->moveToThread(&d->mDatabaseThread);
}

void MusicListenersManager::resetMusicData()
{
    Q_EMIT clearDatabase();
}

void MusicListenersManager::configChanged()
{
    auto currentConfiguration = Elisa::ElisaConfiguration::self();

    d->mConfigFileWatcher.addPath(currentConfiguration->config()->name());

    currentConfiguration->load();
    currentConfiguration->read();

    const auto &allRootPaths = currentConfiguration->rootPath();
    d->mFileListener.setAllRootPaths(allRootPaths);

#if defined KF5Baloo_FOUND && KF5Baloo_FOUND
    d->mBalooListener.setAllRootPaths(allRootPaths);
#endif

    if (!d->mBalooIndexerActive && !d->mFileSystemIndexerActive) {
        testBalooIndexerAvailability();
    }

#if defined UPNPQT_FOUND && UPNPQT_FOUND
    d->mUpnpListener.setDatabaseInterface(&d->mDatabaseInterface);
    d->mUpnpListener.moveToThread(&d->mDatabaseThread);
    connect(this, &MusicListenersManager::applicationIsTerminating,
            &d->mUpnpListener, &UpnpListener::applicationAboutToQuit, Qt::DirectConnection);
#endif

#if defined Qt5AndroidExtras_FOUND && Qt5AndroidExtras_FOUND
    if (!d->mAndroidMusicListener) {
        d->mAndroidMusicListener = std::make_unique<AndroidMusicListener>();
        d->mAndroidMusicListener->moveToThread(&d->mListenerThread);
        d->mAndroidMusicListener->setDatabaseInterface(&d->mDatabaseInterface);
        connect(this, &MusicListenersManager::applicationIsTerminating,
                d->mAndroidMusicListener.get(), &AndroidMusicListener::applicationAboutToQuit, Qt::DirectConnection);
        connect(d->mAndroidMusicListener.get(), &AndroidMusicListener::indexingStarted,
                this, &MusicListenersManager::monitorStartingListeners);
        connect(d->mAndroidMusicListener.get(), &AndroidMusicListener::indexingFinished,
                this, &MusicListenersManager::monitorEndingListeners);
        connect(d->mAndroidMusicListener.get(), &AndroidMusicListener::clearDatabase,
                &d->mDatabaseInterface, &DatabaseInterface::removeAllTracksFromSource);
        connect(d->mAndroidMusicListener.get(), &AndroidMusicListener::newNotification,
                this, &MusicListenersManager::newNotification);
        connect(d->mAndroidMusicListener.get(), &AndroidMusicListener::closeNotification,
                this, &MusicListenersManager::closeNotification);
    }
#endif
}

void MusicListenersManager::increaseImportedTracksCount(const DatabaseInterface::ListTrackDataType &allTracks)
{
    d->mImportedTracksCount += allTracks.size();

    //if (d->mImportedTracksCount >= 4) {
        Q_EMIT closeNotification(QStringLiteral("notEnoughTracks"));
    //}

    Q_EMIT importedTracksCountChanged();
}

void MusicListenersManager::decreaseImportedTracksCount()
{
    --d->mImportedTracksCount;

    Q_EMIT importedTracksCountChanged();
}

void MusicListenersManager::monitorStartingListeners()
{
    d->mIndexerBusy = true;
    Q_EMIT indexerBusyChanged();
}

void MusicListenersManager::monitorEndingListeners()
{
    /*if (d->mImportedTracksCount < 4 && d->mElisaApplication) {
        NotificationItem notEnoughTracks;

        notEnoughTracks.setNotificationId(QStringLiteral("notEnoughTracks"));

        notEnoughTracks.setTargetObject(this);

        notEnoughTracks.setMessage(i18nc("No track found message", "No track have been found"));

        auto configureAction = d->mElisaApplication->action(QStringLiteral("options_configure"));

        notEnoughTracks.setMainButtonText(configureAction->text());
        notEnoughTracks.setMainButtonIconName(configureAction->icon().name());
        notEnoughTracks.setMainButtonMethodName(QStringLiteral("showConfiguration"));

        Q_EMIT newNotification(notEnoughTracks);
    }*/

    d->mIndexerBusy = false;
    Q_EMIT indexerBusyChanged();
}

void MusicListenersManager::cleanedDatabase()
{
    d->mImportedTracksCount = 0;
    Q_EMIT importedTracksCountChanged();
}

void MusicListenersManager::balooAvailabilityChanged()
{
#if defined KF5Baloo_FOUND && KF5Baloo_FOUND
    if (!d->mBalooDetector.balooAvailability()) {
#else
    if (true) {
#endif
        if (!d->mFileSystemIndexerActive) {
            startLocalFileSystemIndexing();
        }

        return;
    }

    qCInfo(orgKdeElisaIndexersManager) << "Baloo indexer is available";
    d->mBalooIndexerAvailable = true;
    Q_EMIT balooIndexerAvailableChanged();
    startBalooIndexing();
}

void MusicListenersManager::testBalooIndexerAvailability()
{
#if defined KF5Baloo_FOUND && KF5Baloo_FOUND
    d->mBalooDetector.checkBalooAvailability();
#else
    qCInfo(orgKdeElisaIndexersManager) << "Baloo indexer is unavailable";
    d->mBalooIndexerAvailable = false;
    Q_EMIT balooIndexerAvailableChanged();

    qCInfo(orgKdeElisaIndexersManager) << "Baloo indexer is inactive";
    d->mBalooIndexerActive = false;
    Q_EMIT balooIndexerActiveChanged();

    startLocalFileSystemIndexing();
#endif
}

void MusicListenersManager::startLocalFileSystemIndexing()
{
    if (d->mFileSystemIndexerActive) {
        return;
    }

    d->mFileListener.setDatabaseInterface(&d->mDatabaseInterface);
    d->mFileListener.moveToThread(&d->mListenerThread);
    connect(this, &MusicListenersManager::applicationIsTerminating,
            &d->mFileListener, &FileListener::applicationAboutToQuit, Qt::DirectConnection);
    connect(&d->mFileListener, &FileListener::indexingStarted,
            this, &MusicListenersManager::monitorStartingListeners);
    connect(&d->mFileListener, &FileListener::indexingFinished,
            this, &MusicListenersManager::monitorEndingListeners);
    connect(&d->mFileListener, &FileListener::newNotification,
            this, &MusicListenersManager::newNotification);
    connect(&d->mFileListener, &FileListener::closeNotification,
            this, &MusicListenersManager::closeNotification);

    QMetaObject::invokeMethod(d->mFileListener.fileListing(), "init", Qt::QueuedConnection);

    qCInfo(orgKdeElisaIndexersManager) << "Local file system indexer is active";

    d->mFileSystemIndexerActive = true;
    Q_EMIT fileSystemIndexerActiveChanged();
}

void MusicListenersManager::startBalooIndexing()
{
#if defined KF5Baloo_FOUND && KF5Baloo_FOUND
    d->mBalooListener.moveToThread(&d->mListenerThread);
    d->mBalooListener.setDatabaseInterface(&d->mDatabaseInterface);
    connect(this, &MusicListenersManager::applicationIsTerminating,
            &d->mBalooListener, &BalooListener::applicationAboutToQuit, Qt::DirectConnection);
    connect(&d->mBalooListener, &BalooListener::indexingStarted,
            this, &MusicListenersManager::monitorStartingListeners);
    connect(&d->mBalooListener, &BalooListener::indexingFinished,
            this, &MusicListenersManager::monitorEndingListeners);
    connect(&d->mBalooListener, &BalooListener::clearDatabase,
            &d->mDatabaseInterface, &DatabaseInterface::clearData);
    connect(&d->mBalooListener, &BalooListener::newNotification,
            this, &MusicListenersManager::newNotification);
    connect(&d->mBalooListener, &BalooListener::closeNotification,
            this, &MusicListenersManager::closeNotification);

    QMetaObject::invokeMethod(d->mBalooListener.fileListing(), "init", Qt::QueuedConnection);

    qCInfo(orgKdeElisaIndexersManager) << "Baloo indexer is active";

    d->mBalooIndexerActive = true;
    Q_EMIT balooIndexerActiveChanged();
#endif
}

void MusicListenersManager::createTracksListener()
{
    if (!d->mTracksListener) {
        d->mTracksListener = std::make_unique<TracksListener>(&d->mDatabaseInterface);
        d->mTracksListener->moveToThread(&d->mDatabaseThread);

        connect(this, &MusicListenersManager::removeTracksInError,
                &d->mDatabaseInterface, &DatabaseInterface::removeTracksList);

        connect(&d->mDatabaseInterface, &DatabaseInterface::trackRemoved, d->mTracksListener.get(), &TracksListener::trackRemoved);
        connect(&d->mDatabaseInterface, &DatabaseInterface::tracksAdded, d->mTracksListener.get(), &TracksListener::tracksAdded);
        connect(&d->mDatabaseInterface, &DatabaseInterface::trackModified, d->mTracksListener.get(), &TracksListener::trackModified);
    }
}


#include "moc_musiclistenersmanager.cpp"
