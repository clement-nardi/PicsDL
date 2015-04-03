/**
 * Copyright 2014-2015 Clément Nardi
 *
 * This file is part of PicsDL.
 *
 * PicsDL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PicsDL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PicsDL.  If not, see <http://www.gnu.org/licenses/>.
 *
 **/

#ifndef TRANSFERMANAGER_H
#define TRANSFERMANAGER_H

#include <QObject>
class Geotagger;
class File;
#include <QElapsedTimer>
class DownloadModel;
#include <QAtomicInteger>
#include <QSemaphore>
#include <QThread>
class TransferManager;

#define NB_WORKERS 2


class TransferWorker: public QObject {
    Q_OBJECT
public:
    TransferWorker(TransferManager *tm_, bool geotag_) ;
signals:
    void launchFile(File *file, bool geotag);
public slots:
    void processList();

private:
    TransferManager *tm;
    QList<File *> *fileList;
    QSemaphore *semaphore;
    bool geotag;
};


class TransferManager : public QObject
{
    Q_OBJECT
public:
    explicit TransferManager(QObject *parent, DownloadModel *dm);
    ~TransferManager();

    void launchDownloads();
    void stopDownloads();
    Geotagger *geotagger;

    QAtomicInteger<qint64> totalRead;
    QAtomicInteger<qint64> totalWritten;
    QAtomicInteger<qint64> totalToWrite;
    QAtomicInteger<int> nbFilesTransfered;
    QAtomicInteger<int> nbFilesToTransfer;
    QAtomicInteger<qint64> totalToCache;
    bool wasStopped;

    QList<File *> filesToTransfer;
    QList<File *> filesToGeotag;
    QSemaphore directSemaphore;
    QSemaphore geotagSemaphore;
    DownloadModel *dm;
public slots:
    void udpateGeoTagger();

signals:
    void downloadFinished();
    void startWorkers();

private:
    void resetStats();

    void initWorker(int idx);
    TransferWorker *tw[NB_WORKERS]; /* 0 for direct transfers, 1 for geotag transfers */
    QThread *wt[NB_WORKERS];

private slots:
    void handleWriteFinished(File *file);
    void launchFile(File *file, bool geotag);
};

#endif // TRANSFERMANAGER_H
