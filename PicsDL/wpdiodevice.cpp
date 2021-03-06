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

#ifdef _WIN32

#include "wpdiodevice.h"
#include "WPDInterface.h"
#include <QDebug>
#include <QMutex>

WPDIODevice::WPDIODevice(QString IDPath_)
{
    IDPath = IDPath_;
}

WPDIODevice::~WPDIODevice()
{

}

static QMutex WPDMutex;

bool WPDIODevice::open(QIODevice::OpenMode mode){
    WPDMutex.lock();
    qDebug() << QString("got Lock! - %1").arg(IDPath);
    if (mode != QIODevice::ReadOnly) {
        qWarning() << "WARNING - WPDIODevice can only be opened for reading!!";
    }
    QString deviceID = IDPath.split("/")[1];
    QString objectID = IDPath.split("/").last();
    WCHAR * deviceID_i = (WCHAR*) malloc(sizeof(WCHAR)*(deviceID.size()+1));
    WCHAR * objectID_i = (WCHAR*) malloc(sizeof(WCHAR)*(objectID.size()+1));
    deviceID.toWCharArray(deviceID_i);
    deviceID_i[deviceID.size()] = L'\0';
    objectID.toWCharArray(objectID_i);
    objectID_i[objectID.size()] = L'\0';
    DWORD optimalTransferSize;
    bool initDone = WPDI_InitTransfer(deviceID_i,objectID_i,&optimalTransferSize);
    //qDebug() << "optimalTransferSize=" << optimalTransferSize;
    free(deviceID_i);
    free(objectID_i);
    if (initDone) {
        return QIODevice::open(mode);
    } else {
        return false;
    }
}

void WPDIODevice::close(){
    WPDI_CloseTransfer();
    QIODevice::close();
    WPDMutex.unlock();
}

qint64 WPDIODevice::readData(char * data, qint64 maxSize) {
    if (maxSize > 0) {
        DWORD read = 0;
        WPDI_readNextData((unsigned char *)data,maxSize,&read);
        //qDebug() << QString("maxSize=%1 - read=%2").arg(maxSize).arg(read);
        if (read == 0) {
            return -1;
        }
        return read;
    } else {
        return 0;
    }
}

qint64 WPDIODevice::writeData(const char * data __attribute__ ((unused)),
                              qint64 maxSize __attribute__ ((unused))){
    qWarning() << "WARNING: WPDIODevice does not support writing!!";
    return -1;
}

#endif
