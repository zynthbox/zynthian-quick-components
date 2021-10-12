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
#include "Note.h"
#include "NotesModel.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

class PlayGrid::Private
{
public:
    Private() {}
    QString id;
    QString name;
    QObject *dashboardModel{nullptr};
    bool metronomeOn{false};
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

    QJsonObject noteToJsonObject(Note *note) {
        QJsonObject jsonObject;
        if (note) {
            jsonObject.insert("midiNote", note->midiNote());
            jsonObject.insert("midiChannel", note->midiChannel());
            if (note->subnotes().count() > 0) {
                QJsonArray subnoteArray;
                for (const QVariant &subnote : note->subnotes()) {
                    subnoteArray << noteToJsonObject(qobject_cast<Note*>(subnote.value<QObject*>()));
                }
                jsonObject.insert("subnotes", subnoteArray);
            }
        }
        return jsonObject;
    }

    Note *jsonObjectToNote(const QJsonObject &jsonObject) {
        Note *note{nullptr};
        if (jsonObject.contains("subnotes")) {
            QJsonArray subnotes = jsonObject["subnotes"].toArray();
            QVariantList subnotesList;
            for (const QJsonValue &val : subnotes) {
                Note *subnote = jsonObjectToNote(val.toObject());
                subnotesList.append(QVariant::fromValue<QObject*>(subnote));
            }
            note = qobject_cast<Note*>(playGridManager->getCompoundNote(subnotesList));
        } else if (jsonObject.contains("midiNote")) {
            note = qobject_cast<Note*>(playGridManager->getNote(jsonObject.value("midiNote").toInt(), jsonObject.value("midiChannel").toInt()));
        }
        return note;
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
        result = d->playGridManager->getNotesModel(QString("%1 - %2").arg(d->name).arg(modelName));
    }
    return result;
}

QObject* PlayGrid::getNamedInstance(const QString& name, const QString& qmlTypeName)
{
    QObject *result{nullptr};
    if (d->playGridManager) {
        result = d->playGridManager->getNamedInstance(QString("%1 - %2").arg(d->name).arg(name), qmlTypeName);
    }
    return result;
}

QString PlayGrid::modelToJson(QObject* model) const
{
    QJsonDocument json;
    NotesModel* actualModel = qobject_cast<NotesModel*>(model);
    if (actualModel) {
        QJsonArray modelArray;
        for (int row = 0; row < actualModel->rowCount(); ++row) {
            QJsonArray rowArray;
            for (int column = 0; column < actualModel->columnCount(actualModel->index(row)); ++column) {
                QJsonObject obj;
                obj.insert("note", d->noteToJsonObject(qobject_cast<Note*>(actualModel->getNote(row, column))));
                obj.insert("metadata", QJsonValue::fromVariant(actualModel->getMetadata(row, column)));
                rowArray.append(obj);
            }
            modelArray << QJsonValue(rowArray);
        }
        json.setArray(modelArray);
    }
    return json.toJson();
}

void PlayGrid::setModelFromJson(QObject* model, const QString& json)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json.toUtf8());
    NotesModel* actualModel = qobject_cast<NotesModel*>(model);
    actualModel->clear();
    if (jsonDoc.isArray()) {
        QJsonArray notesArray = jsonDoc.array();
        for (const QJsonValue &row : notesArray) {
            if (row.isArray()) {
                QVariantList rowList;
                QVariantList rowMetadata;
                QJsonArray rowArray = row.toArray();
                for (const QJsonValue &note : rowArray) {
                    rowList << QVariant::fromValue<QObject*>(d->jsonObjectToNote(note["note"].toObject()));
                    rowMetadata << note["metadata"].toVariant();
                }
                actualModel->appendRow(rowList, rowMetadata);
            }
        }
    }
}

QString PlayGrid::notesListToJson(const QVariantList& notes) const
{
    QJsonDocument json;
    QJsonArray notesArray;
    for (const QVariant &element : notes) {
        notesArray << d->noteToJsonObject(qobject_cast<Note*>(element.value<QObject*>()));
    }
    json.setArray(notesArray);
    return json.toJson();
}

QVariantList PlayGrid::jsonToNotesList(const QString& json)
{
    QVariantList notes;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json.toUtf8());
    if (jsonDoc.isArray()) {
        QJsonArray notesArray = jsonDoc.array();
        for (const QJsonValue &note : notesArray) {
            notes << QVariant::fromValue<QObject*>(d->jsonObjectToNote(note.toObject()));
        }
    }
    return notes;
}

QString PlayGrid::noteToJson(QObject* note) const
{
    QJsonDocument doc;
    doc.setObject(d->noteToJsonObject(qobject_cast<Note*>(note)));
    return doc.toJson();
}

QObject* PlayGrid::jsonToNote(const QString& json)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json.toUtf8());
    return d->jsonObjectToNote(jsonDoc.object());
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
        Q_EMIT playGridManagerChanged();
    }
}

QObject* PlayGrid::playGridManager() const
{
    return d->playGridManager;
}

void PlayGrid::setId(const QString& id)
{
    if (d->id != id) {
        d->id = id;
        Q_EMIT idChanged();
    }
}

QString PlayGrid::id() const
{
    return d->id;
}

void PlayGrid::setName(const QString& name)
{
    if (d->name != name) {
        d->name = name;
        Q_EMIT nameChanged();
    }
}

QString PlayGrid::name() const
{
    return d->name;
}

void PlayGrid::setDashboardModel(QObject* model)
{
    if (d->dashboardModel != model) {
        d->dashboardModel = model;
        d->playGridManager->registerDashboardModel(d->id, model);
        Q_EMIT dashboardModelChanged();
    }
}

QObject* PlayGrid::dashboardModel() const
{
    return d->dashboardModel;
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
    if (d->playGridManager && !d->metronomeOn) {
        d->metronomeOn = true;
        connect(d->playGridManager, &PlayGridManager::metronomeBeat4thChanged, this, &PlayGrid::metronomeBeat4thChanged);
        connect(d->playGridManager, &PlayGridManager::metronomeBeat8thChanged, this, &PlayGrid::metronomeBeat8thChanged);
        connect(d->playGridManager, &PlayGridManager::metronomeBeat16thChanged, this, &PlayGrid::metronomeBeat16thChanged);
        connect(d->playGridManager, &PlayGridManager::metronomeBeat32ndChanged, this, &PlayGrid::metronomeBeat32ndChanged);
        connect(d->playGridManager, &PlayGridManager::metronomeBeat64thChanged, this, &PlayGrid::metronomeBeat64thChanged);
        connect(d->playGridManager, &PlayGridManager::metronomeBeat128thChanged, this, &PlayGrid::metronomeBeat128thChanged);
        d->playGridManager->startMetronome();
    }
}

void PlayGrid::stopMetronome()
{
    if (d->metronomeOn) {
        d->metronomeOn = false;
        if (d->playGridManager) {
            disconnect(d->playGridManager, &PlayGridManager::metronomeBeat4thChanged, this, &PlayGrid::metronomeBeat4thChanged);
            disconnect(d->playGridManager, &PlayGridManager::metronomeBeat8thChanged, this, &PlayGrid::metronomeBeat8thChanged);
            disconnect(d->playGridManager, &PlayGridManager::metronomeBeat16thChanged, this, &PlayGrid::metronomeBeat16thChanged);
            disconnect(d->playGridManager, &PlayGridManager::metronomeBeat32ndChanged, this, &PlayGrid::metronomeBeat32ndChanged);
            disconnect(d->playGridManager, &PlayGridManager::metronomeBeat64thChanged, this, &PlayGrid::metronomeBeat64thChanged);
            disconnect(d->playGridManager, &PlayGridManager::metronomeBeat128thChanged, this, &PlayGrid::metronomeBeat128thChanged);
            d->playGridManager->stopMetronome();
        }
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

int PlayGrid::metronomeBeat32nd() const
{
    if (d->playGridManager) {
        return d->playGridManager->metronomeBeat32nd();
    }
    return 0;
}

int PlayGrid::metronomeBeat64th() const
{
    if (d->playGridManager) {
        return d->playGridManager->metronomeBeat64th();
    }
    return 0;
}

int PlayGrid::metronomeBeat128th() const
{
    if (d->playGridManager) {
        return d->playGridManager->metronomeBeat128th();
    }
    return 0;
}
