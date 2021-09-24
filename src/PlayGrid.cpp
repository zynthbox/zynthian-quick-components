/*
 * Copyright (C) 2021 Dan Leinir Turthra Jensen <admin@leinir.dk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "PlayGrid.h"
#include "PlayGridManager.h"

#include <QDir>

class PlayGrid::Private
{
public:
    Private() {}
    QString name;
    PlayGridManager *playGridManager{nullptr};
    QString getDataDir()
    {
        // test and make sure that this env var contains something, or spit out .local/zynthian or something
        return QString("%1/playgrid/%2").arg(QString(qgetenv("ZYNTHIAN_MY_DATA_DIR"))).arg(name);
    }

    QString getSafeFilename(const QString& unsafe)
    {
        QStringList keepcharacters{" ",".","_"};
        QString safe;
        for (const QChar &letter : unsafe) {
            if (letter.isLetterOrNumber() || keepcharacters.contains(letter)) {
                safe.append(letter);
            }
        }
        return getDataDir() + "/" + safe;
    }
};

PlayGrid::PlayGrid(QQuickItem* parent)
    : QQuickItem(parent)
    , d(new Private)
{
}

PlayGrid::~PlayGrid()
{
    delete d;
}

QObject* PlayGrid::getNote(int midiNote, int midiChannel)
{
    QObject *result{nullptr};
    if (d->playGridManager) {
        result = d->playGridManager->getNote(midiNote, midiChannel);
    }
    return result;
}

QObject* PlayGrid::getCompoundNote(const QVariantList& notes)
{
    QObject *result{nullptr};
    if (d->playGridManager) {
        result = d->playGridManager->getCompoundNote(notes);
    }
    return result;
}

QObject* PlayGrid::getModel(const QString& modelName)
{
    QObject *result{nullptr};
    if (d->playGridManager) {
        result = d->playGridManager->getNotesModel(modelName);
    }
    return result;
}

void PlayGrid::setNoteOn(QObject* note, int velocity)
{
    if (d->playGridManager) {
        d->playGridManager->setNoteOn(note, velocity);
    }
}

void PlayGrid::setNoteOff(QObject* note)
{
    if (d->playGridManager) {
        d->playGridManager->setNoteOff(note);
    }
}

void PlayGrid::setNotesOn(const QVariantList& notes, const QVariantList& velocities)
{
    if (d->playGridManager) {
        d->playGridManager->setNotesOn(notes, velocities);
    }
}

void PlayGrid::setNotesOff(const QVariantList& notes)
{
    if (d->playGridManager) {
        d->playGridManager->setNotesOff(notes);
    }
}

QString PlayGrid::loadData(const QString& key)
{
    QString data;
    QFile file(d->getSafeFilename(key));
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
            data = QString::fromUtf8(file.readAll());
            file.close();
        }
    }
    return data;
}

bool PlayGrid::saveData(const QString& key, const QString& data)
{
    bool success = false;
    QDir confLocation(d->getDataDir());
    if (confLocation.exists() || confLocation.mkpath(confLocation.path())) {
        QFile dataFile(d->getSafeFilename(key));
        if (dataFile.open(QIODevice::WriteOnly) && dataFile.write(data.toUtf8())) {
            success = true;
        }
    }
    return success;
}

void PlayGrid::setPlayGridManager(QObject* playGridManager)
{
    if (d->playGridManager != playGridManager) {
        if (d->playGridManager) {
            d->playGridManager->disconnect(this);
        }
        d->playGridManager = qobject_cast<PlayGridManager*>(playGridManager);
        connect(d->playGridManager, &PlayGridManager::pitchChanged, this, &PlayGrid::pitchChanged);
        connect(d->playGridManager, &PlayGridManager::modulationChanged, this, &PlayGrid::modulationChanged);
        connect(d->playGridManager, &PlayGridManager::metronomeBeat4thChanged, this, &PlayGrid::metronomeBeat4thChanged);
        connect(d->playGridManager, &PlayGridManager::metronomeBeat8thChanged, this, &PlayGrid::metronomeBeat8thChanged);
        connect(d->playGridManager, &PlayGridManager::metronomeBeat16thChanged, this, &PlayGrid::metronomeBeat16thChanged);
        Q_EMIT playGridManagerChanged();
    }
}

QObject* PlayGrid::playGridManager() const
{
    return d->playGridManager;
}

void PlayGrid::setPitch(int pitch)
{
    if (d->playGridManager) {
        d->playGridManager->setPitch(pitch);
    }
}

int PlayGrid::pitch() const
{
    if (d->playGridManager) {
        return d->playGridManager->pitch();
    }
    return 0;
}

void PlayGrid::setModulation(int modulation)
{
    if (d->playGridManager) {
        d->playGridManager->setModulation(modulation);
    }
}

int PlayGrid::modulation() const
{
    if (d->playGridManager) {
        return d->playGridManager->modulation();
    }
    return 0;
}

void PlayGrid::startMetronome()
{
    if (d->playGridManager) {
        d->playGridManager->startMetronome();
    }
}

void PlayGrid::stopMetronome()
{
    if (d->playGridManager) {
        d->playGridManager->stopMetronome();
    }
}

int PlayGrid::metronomeBeat4th() const
{
    if (d->playGridManager) {
        return d->playGridManager->metronomeBeat4th();
    }
    return 0;
}

int PlayGrid::metronomeBeat8th() const
{
    if (d->playGridManager) {
        return d->playGridManager->metronomeBeat8th();
    }
    return 0;
}

int PlayGrid::metronomeBeat16th() const
{
    if (d->playGridManager) {
        return d->playGridManager->metronomeBeat16th();
    }
    return 0;
}
