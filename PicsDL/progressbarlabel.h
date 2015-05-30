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

#ifndef PROGRESSBARLABEL_H
#define PROGRESSBARLABEL_H

#include <QWidget>
#include <QProgressBar>
#include <QLabel>

class ProgressBarLabel : public QWidget
{
    Q_OBJECT
public:
    explicit ProgressBarLabel(QWidget *parent = 0);
    ~ProgressBarLabel();

    QProgressBar *bar;
    QLabel *label;

signals:

private slots:
};

#endif // PROGRESSBARLABEL_H
