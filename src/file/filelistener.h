/*
 * Copyright 2016-2017 Matthieu Gallien <matthieu_gallien@yahoo.fr>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef FILELISTENER_H
#define FILELISTENER_H

#include <QObject>

class FileListenerPrivate;
class DatabaseInterface;

class FileListener : public QObject
{
    Q_OBJECT

    Q_PROPERTY(DatabaseInterface* databaseInterface
               READ databaseInterface
               WRITE setDatabaseInterface
               NOTIFY databaseInterfaceChanged)

public:
    explicit FileListener(QObject *parent = 0);

    virtual ~FileListener();

    DatabaseInterface* databaseInterface() const;

Q_SIGNALS:

    void refreshContent();

    void databaseInterfaceChanged();

public Q_SLOTS:

    void setDatabaseInterface(DatabaseInterface* databaseInterface);

private:

    FileListenerPrivate *d = nullptr;

};

#endif // FILELISTENER_H