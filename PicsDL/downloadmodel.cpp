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

#include "downloadmodel.h"
#include <QDateTime>
#include <QDebug>
#include <QIcon>
#include <QVector>
#include <QThread>
#include <QApplication>
#include <QInputDialog>
#include <QTableWidget>
#include "dcomdialog.h"
#include "exifdialog.h"
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QUuid>
#include <QCryptographicHash>
#include "geotagger.h"

static QStringList dateKeywords = QString("yyyy;yy;MMMM;MMM;MM;dddd;ddd;dd;HH;mm;ss").split(";");

DownloadModel::DownloadModel(Config *dc_, QProgressDialog *pd_, bool editMode_, QObject *parent) :
    QAbstractTableModel(parent)
{
    dc = dc_;
    editMode = editMode_;
    itemBeingDownloaded = -1;
    pd = pd_;

}

DownloadModel::~DownloadModel() {
    emptyFileList();
    if (DLTempFolder.size() > 0) {
        QDir(DLTempFolder).removeRecursively();
    }
}

int DownloadModel::rowCount(const QModelIndex & parent) const{
    return selectedFileList.size();
}
int DownloadModel::columnCount(const QModelIndex & parent) const {
    return 5;
}

QString DownloadModel::newPath(File *fi, bool keepDComStr) const{
    QJsonObject obj = dc->devices[id].toObject();
    QString newName = obj[CONFIG_NEWNAME].toString();
    QString newPath;
    QDateTime lm;
    int tagidx = -1;
    if (dc->devices[id].toObject()[CONFIG_ALLOWEXIF].toBool() &&
        dc->devices[id].toObject()[CONFIG_USEEXIFDATE].toBool()    ) {
        lm = QDateTime::fromTime_t(fi->dateTaken());
    } else {
        lm = QDateTime::fromTime_t(fi->lastModified);
    }
    QStringList oNameList = fi->fileName().split(".");
    QString oExt = oNameList.last();
    oNameList.removeLast();
    QString oName = oNameList.join(".");

    for (int i = 0; i < dateKeywords.size(); i++) {
        newName.replace(dateKeywords.at(i),lm.toString(dateKeywords.at(i)));
    }
    newName.replace("oName",oName);
    if (!keepDComStr && newName.contains("dCom")) {
        newName.replace("dCom",getDCom(fi));
    }
    newName.replace("sCom",sessionComment);

    /* replace EXIF tags with their values */
    while (true) {
        tagidx = newName.indexOf("<",tagidx+1);
        if (tagidx<0) break;
        int endidx = newName.indexOf(">",tagidx);
        if (endidx<0) break;
        QString tagName = newName.mid(tagidx+1,endidx-tagidx-1);
        QString value = "";
        if (dc->devices[id].toObject()[CONFIG_ALLOWEXIF].toBool()) {
            value = fi->getEXIFValue(tagName);
        }
        newName.replace("<" + tagName + ">",value);        
    }

    /* These are forbidden characters under windows: \ : * ? " < > | */
    newName.remove(QRegExp("[\\\\:*?\"<>|]"));

    /* prevent folder and file names to start or end with a space character */
    newName.prepend("/");
    while (newName.contains(" /")) {newName.replace(" /","/");}
    while (newName.contains("/ ")) {newName.replace("/ ","/");}

    newPath = obj[CONFIG_DOWNLOADTO].toString() + newName + "." + oExt;
    while (newPath.contains("//")) {newPath.replace("//","/");}
    return newPath;
}


QString DownloadModel::tempPath(File *fi) const {
    QStringList oNameList = fi->fileName().split(".");
    QString oExt = oNameList.last();
    QString tempName = QString(QCryptographicHash::hash(fi->absoluteFilePath.toUtf8(), QCryptographicHash::Sha3_224).toHex());
    return DLTempFolder + "/" + tempName + "." + oExt;
}

QString DownloadModel::guessCameraName() {
    if (dc->devices[id].toObject()[CONFIG_ALLOWEXIF].toBool()) {
        for (int i = selectedFileList.size()-1; i>=0; i--) {
            File * fi = selectedFileList.at(i);
            QString makeModel = fi->getEXIFValue("Make") + " " + fi->getEXIFValue("Model");
            if (makeModel.size() > 1) {
                return makeModel;
            }
        }
    }
    return "";
}

QString DownloadModel::getDCom(File *fi, bool forceQuery) const{
    QDateTime fileDate = QDateTime::fromTime_t(fi->lastModified);
    QString dayKey = fileDate.toString("yyyy-MM-dd");
    QJsonObject obj = dc->daily_comments;
    QJsonValue com = obj[dayKey];
    QString comment = com.toString();
    /*
    if (com.isNull() || com.isUndefined() || forceQuery) {
        bool ok;
        comment = QInputDialog::getText(this,
                                        "Dayly comment",
                                        "Please enter a comment or a series of keywords for\n" + fileDate.toString(Qt::SystemLocaleLongDate),
                                        QLineEdit::Normal,
                                        com.toString(),
                                        &ok);
        if (ok) {
            obj[dayKey] = comment;
            dc->conf["dCom"] = obj;
        }
    } */
    return comment;
}

bool DownloadModel::getAllCom(){
    qDebug() << "dpm.getAllCom";
    QString newName = dc->devices[id].toObject()[CONFIG_NEWNAME].toString();
    int res = QDialog::Accepted;
    if (selectedFileList.size() > 0 &&
        (newName.contains("dCom") ||
         newName.contains("sCom")   )  ) {
        QJsonObject obj = dc->daily_comments;
        DComDialog *allComQuery = new DComDialog();
        allComQuery->setWindowModality(Qt::WindowModal);
        QTableWidget *queryTable = allComQuery->ui->tableWidget;
        QMap<QString,QString> *daylycomments = new QMap<QString,QString>();
        int numberOfQueries = 0;
        queryTable->setColumnCount(2);
        if (newName.contains("dCom")) {
            for (int i = 0; i < selectedFileList.size(); i++) {
                File *fi = selectedFileList.at(i);
                QDate fileDate = QDateTime::fromTime_t(fi->lastModified).date();
                QString dayKey = fileDate.toString("yyyy-MM-dd");
                QJsonValue com = obj[dayKey];
                if (!daylycomments->contains(dayKey)) {
                    QString comment = com.toString();
                    if (comment.size() == 0) {
                        /* Guess Comment based on existing arborescence at destination */
                        QString dirToSearch = "";
                        QString patternToSearch = "";
                        QStringList np = newPath(fi, true).split("/");
                        bool matchOnDirs = true;
                        int matchCount = 0;
                        int j;
                        for (j = 0; j< np.size(); j++) {
                            if (np.at(j).contains("dCom")) {
                                patternToSearch = np.at(j);
                                break;
                            } else {
                                if (dirToSearch.size()>0)
                                    dirToSearch.append("/");
                                dirToSearch.append(np.at(j));
                            }
                        }

                        if (j == np.size()-1) {
                            /* the dayly comment is in the file name -> match on files */
                            matchOnDirs = false;
                        }
                        qDebug() << "matchOnDirs=" << matchOnDirs;

                        QStringList entries = QDir(dirToSearch).entryList((matchOnDirs?(QDir::Dirs):(QDir::Files))|QDir::NoDotAndDotDot);
                        patternToSearch.replace("dCom","(.*)");
                        QRegExp pattern = QRegExp(patternToSearch);
                        qDebug() << "search " << dirToSearch << "(" << entries.size() << " entries) for" << patternToSearch;
                        for (int j = 0; j < entries.size(); j++) {
                            qDebug() << "   entry=" << entries.at(j);
                            int pos = pattern.indexIn(entries.at(j));
                            if (pos >= 0) {
                                qDebug() << "      match at " << pos;
                                matchCount++;
                                if (matchCount >= 4) {
                                    qDebug() << "         -> too many matches, abandon this meaningless search.";
                                    comment = "";
                                    break;
                                }
                                QString cap1 = pattern.cap(1);
                                if (cap1.size()>comment.size()) {
                                    comment = cap1;
                                }
                            }
                        }
                    }
                    daylycomments->insert(dayKey,comment);
                }
            }
            numberOfQueries += daylycomments->keys().size();
        }
        if (newName.contains("sCom")) {
            numberOfQueries++;
        }

        queryTable->setRowCount(numberOfQueries);
        for (int i = 0; i < daylycomments->keys().size(); i++) {
            QString dayKey = daylycomments->keys().at(i);
            QString dayComment = daylycomments->value(dayKey);
            QString dayName = QDateTime::fromString(dayKey,"yyyy-MM-dd").date().toString(Qt::SystemLocaleLongDate);
            QTableWidgetItem *keyItem = new QTableWidgetItem(dayName);
            keyItem->setFlags(Qt::ItemIsEnabled);
            QTableWidgetItem *valueItem = new QTableWidgetItem(dayComment);
            valueItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable );
            queryTable->setItem(i,0,keyItem);
            queryTable->setItem(i,1,valueItem);
        }
        if (newName.contains("sCom")) {
            QTableWidgetItem *keyItem = new QTableWidgetItem("Session Comment");
            keyItem->setFlags(Qt::ItemIsEnabled);
            QTableWidgetItem *valueItem = new QTableWidgetItem(sessionComment);
            valueItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
            queryTable->setItem(queryTable->rowCount()-1,0,keyItem);
            queryTable->setItem(queryTable->rowCount()-1,1,valueItem);
        }
        queryTable->resizeColumnsToContents();
        res = allComQuery->exec();
        for (int i = 0; i < daylycomments->keys().size(); i++) {
            obj[daylycomments->keys().at(i)] = queryTable->item(i,1)->text();
        }
        if (newName.contains("sCom")) {
            sessionComment = queryTable->item(queryTable->rowCount()-1,1)->text();
        }
        allComQuery->deleteLater();
        dc->daily_comments = obj;
        dc->saveDailyComments();

        dataChanged(createIndex(0,2),
                    createIndex(selectedFileList.size(),2));
        delete daylycomments;
    }
    return (res == QDialog::Accepted);
}

void DownloadModel::showEXIFTags(int row) {
    File *fi = selectedFileList.at(row);
    QStringList tags = fi->getEXIFTags();
    if (tags.size() > 0) {
        EXIFDialog *d = new EXIFDialog();
        QTableWidget *t = d->ui->tableWidget;
        t->setRowCount(tags.size());
        for (int i = 0; i<tags.size(); i++) {
            QTableWidgetItem *keyItem = new QTableWidgetItem("<" + tags.at(i) + ">");
            keyItem->setFlags(Qt::ItemIsEnabled);
            QTableWidgetItem *valueItem = new QTableWidgetItem(fi->getEXIFValue(tags.at(i)));
            valueItem->setFlags(Qt::ItemIsEnabled);
            t->setItem(i,0,keyItem);
            t->setItem(i,1,valueItem);
        }
        d->exec();
        d->deleteLater();
    }

}


QVariant DownloadModel::data(const QModelIndex & index, int role) const{
    File *fi = selectedFileList.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case 0:
            break;
        case 1:
            return fi->fileName();
            break;
        case 2:
        {
            return File::size2Str(fi->size);
        }
            break;
        case 3:
        {
            QDateTime lm = QDateTime::fromTime_t(fi->lastModified);
            return lm.toString("yyyy/MM/dd HH:mm:ss");
        }
            break;
        case 4:
            return newPath(fi);
            break;
        }
        break;
    case Qt::ToolTipRole:
        switch (index.column()) {
        case 0:
        {
            QPixmap tn = fi->getThumbnail();
            if (!tn.isNull()) {
                return QString("%1x%2").arg(tn.size().width()).arg(tn.size().height());
            }
        }
        case 1:
        {
            QString tooltip = fi->absoluteFilePath;
            if (dc->knownFiles.contains(*fi)) {
                tooltip += "\nThis file was tranfered before";
            } else {
                tooltip += "\nThis is a new file";
            }
            return tooltip;
        }
            break;
        case 2:
        {
            return QString("%1 Bytes").arg(fi->size);
        }
            break;
        case 3:
        {
            QDateTime lm = QDateTime::fromTime_t(fi->lastModified);
            return lm.toString(Qt::SystemLocaleLongDate);
        }
            break;
        case 4:
        {
            QString tooltip = newPath(fi);
            if (fi->transferOnGoing) {
                tooltip += "\nThis file is being transfered";
            } else if (QFile(newPath(fi)).exists()) {
                if (fi->transferTo.size()>0) {
                    tooltip += "\nThis file was just transfered";
                } else {
                    tooltip += "\nThis file already exists, it will not be overwritten";
                }
            } else {
                tooltip += "\nThis file will be transfered";
            }
            return tooltip;
        }
            break;
        }
        break;
    case Qt::DecorationRole:
        if (index.column() == 0) {
            QPixmap tn = fi->getThumbnail();
            if (!tn.isNull()) {
                return tn;
            } else {
                if (fi->isJPEG()) {
                    return QIcon(":/icons/picture");
                } else if (fi->isPicture()) {
                    return QIcon(":/icons/image");
                } else if (fi->isVideo()) {
                    return QIcon(":/icons/film");
                } else {
                    return QIcon(":/icons/file");
                }
            }
        }
        if (index.column() == 1) {
            if (dc->knownFiles.contains(*fi)) {
                return QIcon(":/icons/ok");
            } else {
                return QIcon(":/icons/add");
            }
        }
        if (index.column() == 4) {
            if (fi->transferOnGoing) {
                return QIcon(":/icons/download");
            } else if (QFile(newPath(fi)).exists()) {
                if (fi->transferTo.size()>0) {
                    return QIcon(":/icons/ok");
                } else {
                    return QIcon(":/icons/warning");
                }
            } else {
                return QIcon(":/icons/play");
            }
        }
        break;
    case Qt::SizeHintRole:
        if (index.column() == 0) {
            QPixmap tn = fi->getThumbnail();
            if (!tn.isNull()) {
                return tn.size();
            }
        }
        break;
    }
    return QVariant();
}

QVariant DownloadModel::headerData(int section, Qt::Orientation orientation, int role) const{
    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        switch (orientation) {
        case Qt::Horizontal:
            switch (section) {
            case 0: return ""; break;
            case 1: return "File"; break;
            case 2: return "Size"; break;
            case 3: return "Date"; break;
            case 4: return "Destination"; break;
            }
            break;
        case Qt::Vertical:
            break;
        }
    }
    return QVariant();
}

bool pathLessThan(File *a, File *b) {
    return a->absoluteFilePath < b->absoluteFilePath;
}
bool dateLessThan(File *a, File *b) {
    return a->lastModified < b->lastModified;
}

void DownloadModel::emptyFileList() {
    for (int i = 0; i < completeFileList.size(); i++) {
        delete completeFileList.at(i);
    }
    completeFileList.clear();
    selectedFileList.clear();
}

void DownloadModel::loadPreview(QString id_) {
    qDebug() << "dpm.loadPreview";
    id = id_;
    QJsonObject obj = dc->devices[id].toObject();
    sessionComment = "";
    QString path = obj[CONFIG_PATH].toString();
    QString IDPath = obj[CONFIG_IDPATH].toString();

    emptyFileList();

    discoveredFolders = 1;
    browsedFolders = 0;
    browsedFiles = 0;
    pdTimer.start();
    treatDir(File(0,path,0,true,IDPath));
    pd->hide();

    reloadSelection(true);
}


bool DownloadModel::isBlacklisted(File element) {
    if (dc->devices[id].toObject()[CONFIG_FILTERTYPE].toInt() == 0) { /* from all directories */
        return false;
    } else {
        bool patternMatchedOnceInPath = false;
        int reverseNonMatchingIndex = 0;

        QStringList patternList = dc->devices[id].toObject()[CONFIG_FILTER].toString().split(";");
        QStringList directoryList = element.absoluteFilePath.split("/");
        if (!element.isDir) {
            directoryList.removeLast();
        }
        for (int dirIdx = directoryList.size()-1; dirIdx >= 0; dirIdx--) {
            bool patternMatchedOnce = false;
            for (int j = 0; j < patternList.size(); j++) {
                QString temp = patternList.at(j);
                QRegExp pattern = QRegExp("^" + temp.replace(".","\\.").replace("*",".*") + "$");
                if (pattern.indexIn(directoryList.at(dirIdx)) >= 0) {
                    patternMatchedOnce = true;
                    qDebug() << element.absoluteFilePath << "matched on" << patternList.at(j) << pattern;
                }
            }
            if (patternMatchedOnce) {
                patternMatchedOnceInPath = true;
            } else if (reverseNonMatchingIndex == 0) {
                reverseNonMatchingIndex = dirIdx;
            }
        }
        if (dc->devices[id].toObject()[CONFIG_FILTERTYPE].toInt() == 1) { /* from all directories except: */
            return patternMatchedOnceInPath;
        } else { /* from these directories: */
            int minMatchIdx = 1; /* Don't need to match H: in H:/DCIM/116_FUJI */
            if (element.IDPath.startsWith("WPD:/")) {
                /* Don't need to match "LGE Nexus 5/Mémoire de stockage interne"
                                     in LGE Nexus 5/Mémoire de stockage interne/DCIM/Camera/   */
                minMatchIdx = 2;
            }
            return reverseNonMatchingIndex >= minMatchIdx;
        }
    }
}

void DownloadModel::treatDir(File dirInfo) {
    QList<File> content;
    bool theresMore = true;
    browsedFolders++;
    int estimatedTotalFiles = discoveredFolders*browsedFiles/browsedFolders;
    while (theresMore) {
        QApplication::processEvents();
        QList<File> contentPart = dirInfo.ls(&theresMore);
        content.append(contentPart);
        for (int i = 0; i < contentPart.size(); ++i) {
            if (contentPart.at(i).isDir) {
                discoveredFolders++;
            } else {
                browsedFiles++;
            }
        }
        if (pdTimer.elapsed() > 2000 && !pd->isVisible()) {
            pd->reset();
            pd->setLabelText("Looking for files on the device");
            pd->setMinimum(0);
            pd->setMaximum(1);
            pd->setWindowModality(Qt::WindowModal);
            pd->setValue(0);
            pd->show();
        }
        if (pd->isVisible()) {
            if (browsedFiles>=estimatedTotalFiles) {
                estimatedTotalFiles = discoveredFolders*browsedFiles/browsedFolders;
            }
            pd->setMaximum(estimatedTotalFiles);
            pd->setValue(browsedFiles);
            if (pdTimer.elapsed() > 10000 &&
                    (dc->devices[id].toObject()[CONFIG_FILTER].toString().size() == 0 ||
                     dc->devices[id].toObject()[CONFIG_FILTERTYPE].toInt() == 0)) {
                pd->setLabelText(QString("Your device is being browsed, and this is taking a long time...\n") +
                                 QString("Once this dialog window is finished, you will be able to setup\n") +
                                 QString("some filters on the directories that may or may not be browsed.\n") +
                                 QString("Doing so can dramatically speed-up this process for next time."));
            }
        }
    }
    for (int i = 0; i < content.size(); ++i) {
        File element = content.at(i);
        if (element.isDir) {            
            if (isBlacklisted(element)) {
                qDebug() << "Do NOT list content of" << element.fileName();
                blacklistedDirectories.append(new File(element));
            } else {
                treatDir(element);
            }
        } else {
            completeFileList.append(new File(element));
        }
    }
}

void DownloadModel::reloadSelection(bool firstTime) {
    qDebug() << "dpm.reloadSelection";
    bool selectall = (dc->devices[id].toObject()[CONFIG_FILESTODOWNLOAD].toString() == "All");
    beginResetModel();
    selectedFileList.clear();

    /* 1st step: check if some blacklisted directories need to be browsed */
    pdTimer.start();
    QMutableListIterator<File*> bli(blacklistedDirectories);
    bool sortNeeded = false;
    while (bli.hasNext()) {
        File *element = bli.next();
        if (!isBlacklisted(*element)) {
            treatDir(*element);
            bli.remove();
            sortNeeded = true;
        }
    }
    if (sortNeeded || firstTime) {
        qSort(completeFileList.begin(), completeFileList.end(), pathLessThan);
        completeFileList_byDate = completeFileList;
        qSort(completeFileList_byDate.begin(), completeFileList_byDate.end(), dateLessThan);
    }
    pd->hide();

    int count = 0;
    for (int i = 0; i < completeFileList.size(); i++) {
        File *fi = completeFileList.at(i);
        if (fi->isPicture() || fi->isVideo()) {
            if ((selectall || !dc->knownFiles.contains(*fi)) &&
                !isBlacklisted(*fi)) {
                selectedFileList.append(fi);
                //if (count++ > 8) break;
                /* Search for attached files */
                for (int j = i-2; j < i+2; j++) {
                    if (j>=0 && j!=i && j<completeFileList.size()) {
                        File *attachedFile = completeFileList.at(j);
                        if (i>0 && attachedFile->isAttachmentOf(*fi)) {
                            if (! (attachedFile->isPicture() || attachedFile->isVideo())) {
                                QString afp = attachedFile->absoluteFilePath;
                                *attachedFile = *fi;
                                /* Attached files must share the same information as the picture or video they are attached to.
                                   This way they will continue to share the same name once downloaded */
                                attachedFile->absoluteFilePath = afp;
                                selectedFileList.append(attachedFile);
                                qDebug() << "           this file " << fi->absoluteFilePath;
                                qDebug() << "has an attached file " << attachedFile->absoluteFilePath;
                            }
                        }
                    }
                }
            }
        }
    }
    if (dc->devices[id].toObject()[CONFIG_ALLOWEXIF].toBool()) {
        pdTimer.start();
        pd->hide();
        for (int i = 0; i < selectedFileList.size(); i++) {
            File *fi = selectedFileList.at(i);
            if (pd->isVisible()) {
                pd->setValue(i);
                if (pd->wasCanceled()) {
                    QJsonObject obj = dc->devices[id].toObject();
                    obj.insert(CONFIG_ALLOWEXIF,QJsonValue(false));
                    dc->devices[id] = obj;
                    dc->saveDevices();
                    EXIFLoadCanceled(false);
                    break;
                }
            }
            fi->loadExifData();
            if (pdTimer.elapsed() > 1000 && !pd->isVisible()) {
                /* only show the progress bar if the process lasts more than a second. */
                pd->reset();
                pd->setLabelText("Loading EXIF Data");
                pd->setMinimum(0);
                pd->setMaximum(selectedFileList.size());
                pd->setWindowModality(Qt::WindowModal);
                pd->setValue(0);
                pd->show();
                qDebug() << "pd.show()";
                QApplication::processEvents();
            }
        }
        pd->hide();
        qDebug() << "pd.hide()";
    }
    averagePicSize = 0;
    int countPics = 0;
    for (int i = 0; i < selectedFileList.size(); i++) {
        File *fi = selectedFileList.at(i);
        fi->modelRow = i;
        connect(fi,SIGNAL(readStarted(File*)),this,SLOT(readStarted(File*)));
        connect(fi,SIGNAL(writeFinished(File*)),this,SLOT(writeFinished(File*)));
        if (fi->isPicture()) {
            averagePicSize += fi->size;
            countPics++;
        }
    }
    if (countPics>0) {
        averagePicSize /= countPics;
    } else {
        for (int i = completeFileList.size()-1; i>=0 && countPics < 50; i--) {
            File *fi = completeFileList.at(i);
            if (fi->isPicture()) {
                averagePicSize += fi->size;
                countPics++;
            }
        }
        if (countPics > 0) {
            averagePicSize /= countPics;
        } else {
            averagePicSize = 5*1024*1024;
        }
    }
    endResetModel();
    reloaded();
}


void DownloadModel::readStarted(File * file){
    dataChanged(createIndex(file->modelRow,4),
                createIndex(file->modelRow,4));
    emit itemOfInterest(createIndex(file->modelRow,4));
}
void DownloadModel::writeFinished(File * file){
    dataChanged(createIndex(file->modelRow,4),
                createIndex(file->modelRow,4));
}


QString DownloadModel::getTrackingFolder() {
    QJsonObject obj = dc->devices[id].toObject();
    QString trackingFolder;
    if (obj[CONFIG_GEOTAG].toBool() == true) {
        if (obj[CONFIG_GEOTAGMODE].toString() == "OpenPaths.cc") {
            /* Download location information from OpenPath.cc */

            /* Convert downloaded JSON into GPX in temporary folder */

            trackingFolder = "";
        } else {
            trackingFolder = obj[CONFIG_TRACKFOLDER].toString();
        }
    } else {
        trackingFolder = "";
    }
    return trackingFolder;
}

void DownloadModel::getStats(qint64 *totalSize, int *nbFiles) {
    *totalSize = 0;
    for (int i = 0; i < selectedFileList.size(); i++) {
        *totalSize += selectedFileList.at(i)->size;
    }
    *nbFiles = selectedFileList.size();
}


void DownloadModel::freeUpSpace(bool isFakeRun,
                                qint64 *targetAvailable,
                                qint64 *bytesDeleted,
                                int *nbFilesDeleted){
    QJsonObject obj = dc->devices[id].toObject();
    qint64 totalSpace = obj[CONFIG_DEVICESIZE].toString().toLongLong();
    *targetAvailable = 0;
    *bytesDeleted = 0;
    *nbFilesDeleted = 0;
    deletedFiles.clear();

    if (obj[CONFIG_FREEUPSPACE].toBool() && totalSpace != 0) {
        qint64 available = obj[CONFIG_BYTESAVAILABLE].toString().toLongLong();
        bool protect_days = obj[CONFIG_PROTECTDAYS].toBool();
        bool protect_selection = obj[CONFIG_PROTECTTRANSFER].toBool();
        uint lastDate = QDateTime::currentDateTime().toTime_t();
        if (protect_days) {
            lastDate -= obj[CONFIG_PROTECTDAYSVALUE].toInt()*24*60*60;
            qDebug() << "Will not delete files modified after " << QDateTime::fromTime_t(lastDate).toString();
        }
        if (obj[CONFIG_TARGETNBPICS].toBool()) {
            qDebug() << "make space for " << obj[CONFIG_NBPICS].toString().toInt() << "NbFiles";
            *targetAvailable = std::max(*targetAvailable,obj[CONFIG_NBPICS].toString().toInt()*averagePicSize);
        }
        if (obj[CONFIG_TARGETPERCENTAGE].toBool()) {
            qDebug() << "target " << obj[CONFIG_TARGETPERCENTAGEVALUE].toInt() << "%";
            *targetAvailable = std::max(*targetAvailable,obj[CONFIG_TARGETPERCENTAGEVALUE].toInt()*totalSpace/100);
        }
        qDebug() << "completeFileList_byDate.size()=" << completeFileList_byDate.size();
        for (int i = 0;
             i < completeFileList_byDate.size() &&
             available + *bytesDeleted < *targetAvailable;
             i++) {
            File * fi = completeFileList_byDate.at(i);
            if (dc->knownFiles.contains(*fi)) {
                /* This file was transfered before */
                if (fi->lastModified < lastDate) {
                    if (protect_selection && selectedFileList.contains(fi)){
                        if (!isFakeRun)
                            qDebug() << "Do not delete this file as it was just downloaded:" << fi->fileName();
                    } else {
                        if (!isFakeRun) {
                            bool removed = fi->remove();
                            qDebug() << "Removing " << fi->absoluteFilePath << " -> " << (removed?"OK":"KO") ;
                        }
                        *bytesDeleted += fi->size;
                        (*nbFilesDeleted)++;
                        deletedFiles.append(fi);
                    }

                } else {
                    if (!isFakeRun)
                        qDebug() << "Stop deleting because lastModified=" << QDateTime::fromTime_t(fi->lastModified).toString();
                    break;
                }
            } else {
                if (!isFakeRun)
                    qDebug() << "Do not delete unknown file: " << fi->absoluteFilePath;
            }
        }
    }
}



























