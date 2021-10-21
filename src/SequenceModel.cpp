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

#include "SequenceModel.h"
#include "Note.h"
#include "PatternModel.h"

#include <SyncTimer.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>

#define PATTERN_COUNT 5

class SequenceModel::Private {
public:
    Private(SequenceModel *q)
        : q(q)
    { }
    SequenceModel *q;
    PlayGridManager *playGridManager{nullptr};
    QList<PatternModel*> patternModels;
    int activePattern{0};
    int version{0};
    int sequencePosition{-1};
    QObjectList onifiedNotes;
    QObjectList queuedForOffNotes;
    bool listeningToMetronome{false};

    QString getDataLocation()
    {
        QStringList keepcharacters{" ",".","_"};
        QString safe;
        for (const QChar &letter : q->objectName()) {
            if (letter.isLetterOrNumber() || keepcharacters.contains(letter)) {
                safe.append(letter);
            }
        }
        // test and make sure that this env var contains something, or spit out .local/zynthian or something
        return QString("%1/session/sequences/%2").arg(QString(qgetenv("ZYNTHIAN_MY_DATA_DIR"))).arg(safe);
    }
};

SequenceModel::SequenceModel(PlayGridManager* parent)
    : QAbstractListModel(parent)
    , d(new Private(this))
{
    d->playGridManager = parent;
    connect(d->playGridManager, &PlayGridManager::metronomeActiveChanged, this, [this](){
        if (!d->playGridManager->metronomeActive()) {
            stopSequencePlayback();
        }
    });
}

SequenceModel::~SequenceModel()
{
    delete d;
}

QHash<int, QByteArray> SequenceModel::roleNames() const
{
    static const QHash<int, QByteArray> roles{
        {PatternRole, "pattern"},
        {TextRole, "text"},
        {NameRole, "name"},
        {LayerRole, "layer"},
        {BankRole, "bank"},
    };
    return roles;
}

QVariant SequenceModel::data(const QModelIndex& index, int role) const
{
    QVariant result;
    if (checkIndex(index)) {
        PatternModel *model = d->patternModels.at(index.row());
        switch (role) {
        case PatternRole:
            result.setValue<QObject*>(model);
            break;
        // We might well want to do something more clever with the text later on, so...
        case TextRole:
        case NameRole:
            result.setValue(model->objectName());
            break;
        case LayerRole:
            result.setValue(model->midiChannel());
            break;
        case BankRole:
            result.setValue(model->bank());
        default:
            break;
        }
    }
    return result;
}

int SequenceModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return d->patternModels.count();
}

QModelIndex SequenceModel::index(int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return createIndex(row, column);
}

QObject* SequenceModel::get(int patternIndex) const
{
    QObject *pattern{nullptr};
    if (patternIndex > -1 && patternIndex < d->patternModels.count()) {
        pattern = d->patternModels.at(patternIndex);
    }
    return pattern;
}

void SequenceModel::insertPattern(PatternModel* pattern, int row)
{
    int insertionRow = d->patternModels.count();
    if (row > -1) {
        // If we've been requested to add in a specific location, do so
        insertionRow = qMin(qMax(0, row), d->patternModels.count());
    }
    beginInsertRows(QModelIndex(), insertionRow, insertionRow);
    auto updatePattern = [this,pattern](){
        int row = d->patternModels.indexOf(pattern);
        QModelIndex index(createIndex(row, 0));
        dataChanged(index, index);
    };
    connect(pattern, &PatternModel::midiChannelChanged, this, updatePattern);
    connect(pattern, &PatternModel::objectNameChanged, this, updatePattern);
    connect(pattern, &PatternModel::bankOffsetChanged, this, updatePattern);
    d->patternModels.insert(insertionRow, pattern);
    setActivePattern(d->activePattern);
    endInsertRows();
}

void SequenceModel::removePattern(PatternModel* pattern)
{
    int removalPosition = d->patternModels.indexOf(pattern);
    if (removalPosition > -1) {
        beginRemoveRows(QModelIndex(), removalPosition, removalPosition);
        d->patternModels.removeAt(removalPosition);
        pattern->disconnect(this);
        setActivePattern(d->activePattern);
        endRemoveRows();
    }
}

bool SequenceModel::contains(QObject* pattern)
{
    return d->patternModels.contains(qobject_cast<PatternModel*>(pattern));
}

PlayGridManager* SequenceModel::playGridManager() const
{
    return d->playGridManager;
}

void SequenceModel::setActivePattern(int activePattern)
{
    int adjusted = qMin(qMax(0, activePattern), d->patternModels.count());
    if (d->activePattern != adjusted) {
        d->activePattern = adjusted;
        Q_EMIT activePatternChanged();
    }
}

int SequenceModel::activePattern() const
{
    return d->activePattern;
}

QObject* SequenceModel::activePatternObject() const
{
    if (d->activePattern > -1 && d->activePattern < d->patternModels.count()) {
        return d->patternModels.at(d->activePattern);
    }
    return nullptr;
}

void SequenceModel::load()
{
    beginResetModel();
    QString data;
    QFile file(d->getDataLocation() + "/" + QString::number(d->version));
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
            data = QString::fromUtf8(file.readAll());
            file.close();
        }
    }
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data.toUtf8());
    if (jsonDoc.isObject()) {
        QJsonObject obj = jsonDoc.object();
        d->patternModels.clear();
        int patternNumber{0};
        for (const QJsonValue &patternValue : obj.value("patterns").toArray()) {
            ++patternNumber;
            PatternModel *model = qobject_cast<PatternModel*>(playGridManager()->getPatternModel(QString("Pattern %1").arg(QString::number(patternNumber)), objectName()));
            playGridManager()->setModelFromJson(model, patternValue.toString());
        }
        setActivePattern(obj.value("activePattern").toInt());
    }
    // This ensures that when we're first creating ourselves a sequence, we end up with some models in it
    if (d->patternModels.count() < PATTERN_COUNT) {
        for (int i = d->patternModels.count(); i < PATTERN_COUNT; ++i) {
            playGridManager()->getPatternModel(QString("Pattern %1").arg(QString::number(i + 1)), objectName());
        }
    }
    endResetModel();
}

bool SequenceModel::save()
{
    bool success = false;

    QJsonObject sequenceObject;
    sequenceObject["activePattern"] = activePattern();
    QJsonArray patternArray;
    for (PatternModel *pattern : d->patternModels) {
        patternArray.append(playGridManager()->modelToJson(pattern));
    }
    sequenceObject["patterns"] = patternArray;

    QJsonDocument jsonDoc;
    jsonDoc.setObject(sequenceObject);
    QString data = jsonDoc.toJson();

    QDir confLocation(d->getDataLocation());
    if (confLocation.exists() || confLocation.mkpath(confLocation.path())) {
        QFile dataFile(confLocation.path() + "/" + QString::number(d->version));
        if (dataFile.open(QIODevice::WriteOnly) && dataFile.write(data.toUtf8())) {
            success = true;
        }
    }
    return success;
}

void SequenceModel::clear()
{
    for (PatternModel *model : d->patternModels) {
        model->clear();
    }
}

void SequenceModel::setPatternProperty(int patternIndex, const QString& property, const QVariant& value)
{
    if (patternIndex > -1 && patternIndex < d->patternModels.count()) {
        d->patternModels.at(patternIndex)->setProperty(property.toUtf8(), value);
    }
}

void SequenceModel::setPreviousOff() const
{
    for (QObject *obj : d->onifiedNotes) {
        const Note *note = qobject_cast<Note*>(obj);
        if (note) {
            note->setOff();
        }
    }
    d->onifiedNotes.clear();
}

void SequenceModel::setPositionOn(int row, int column, bool stopPrevious) const
{
    if (stopPrevious) {
        setPreviousOff();
    }
    for (PatternModel *model : d->patternModels) {
        if (model->enabled()) {
            d->onifiedNotes.append(model->setPositionOn(row + model->bankOffset(), column));
        }
    }
}

void SequenceModel::startSequencePlayback()
{
    if (!d->listeningToMetronome) {
        d->listeningToMetronome = true;
        d->sequencePosition = qobject_cast<SyncTimer*>(playGridManager()->syncTimer())->beat() - 1;
        connect(playGridManager(), &PlayGridManager::metronomeBeat128thChanged, this, &SequenceModel::advanceSequence);
    }
    playGridManager()->startMetronome();
}

void SequenceModel::stopSequencePlayback()
{
    if (d->listeningToMetronome) {
        disconnect(playGridManager(), &PlayGridManager::metronomeBeat128thChanged, this, &SequenceModel::advanceSequence);
        d->listeningToMetronome = false;
        playGridManager()->stopMetronome();
    }
    for (QObject *noteObject : d->queuedForOffNotes) {
        Note *note = qobject_cast<Note*>(noteObject);
        note->setOff();
    }
    d->queuedForOffNotes.clear();
}

void SequenceModel::resetSequence()
{
    d->sequencePosition = -1;
}

void SequenceModel::advanceSequence()
{
    d->queuedForOffNotes = d->onifiedNotes;
    d->onifiedNotes.clear();
    for (PatternModel *pattern : d->patternModels) {
        d->onifiedNotes.append(pattern->handleSequenceAdvancement(d->sequencePosition));
    }
    d->sequencePosition++;
}
