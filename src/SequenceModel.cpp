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

#include <libzl.h>
#include <SyncTimer.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#define PATTERN_COUNT 10

class SequenceModel::Private {
public:
    Private(SequenceModel *q)
        : q(q)
    { }
    SequenceModel *q;
    PlayGridManager *playGridManager{nullptr};
    SyncTimer *syncTimer{nullptr};
    QObject *song{nullptr};
    int soloPattern{-1};
    PatternModel *soloPatternObject{nullptr};
    QList<PatternModel*> patternModels;
    int bpm{0};
    int activePattern{0};
    QString filePath;
    bool isDirty{false};
    int version{0};
    QObjectList onifiedNotes;
    QObjectList queuedForOffNotes;
    bool isPlaying{false};
    bool isLoading{false};

    void ensureFilePath(const QString &explicitFile) {
        if (!explicitFile.isEmpty()) {
            q->setFilePath(explicitFile);
        }
        if (filePath.isEmpty()) {
            if (song) {
                QString sketchFolder = song->property("sketchFolder").toString();
                const QString sequenceNameForFiles = QString(q->objectName().toLower()).replace(" ", "-");
                q->setFilePath(QString("%1/sequences/%2/metadata.sequence.json").arg(sketchFolder).arg(sequenceNameForFiles));
            } else {
                q->setFilePath(QString("%1/%2.sequence.json").arg(getDataLocation()).arg(QString::number(version)));
            }
        }
    }

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
    d->syncTimer = qobject_cast<SyncTimer*>(SyncTimer_instance());
    connect(d->syncTimer, &SyncTimer::timerRunningChanged, this, [this](){
        if (!d->syncTimer->timerRunning()) {
            stopSequencePlayback();
        }
    }, Qt::DirectConnection);
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
        {PlaybackPositionRole, "playbackPosition"},
        {BankPlaybackPositionRole, "bankPlaybackPosition"},
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
            result.setValue(model->name());
            break;
        case LayerRole:
            result.setValue(model->midiChannel());
            break;
        case BankRole:
            result.setValue(model->bank());
            break;
        case PlaybackPositionRole:
            result.setValue(model->playbackPosition());
            break;
        case BankPlaybackPositionRole:
            result.setValue(model->bankPlaybackPosition());
            break;
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
    auto updatePattern = [this,pattern](){
        if (!d->isLoading) {
            int row = d->patternModels.indexOf(pattern);
            QModelIndex index(createIndex(row, 0));
            dataChanged(index, index);
        }
    };
    connect(pattern, &PatternModel::midiChannelChanged, this, updatePattern);
    connect(pattern, &PatternModel::objectNameChanged, this, updatePattern);
    connect(pattern, &PatternModel::bankOffsetChanged, this, updatePattern);
    connect(pattern, &PatternModel::playingColumnChanged, this, updatePattern);
    connect(pattern, &PatternModel::layerDataChanged, this, updatePattern);
    connect(pattern, &PatternModel::noteDestinationChanged, this, &SequenceModel::setDirty);
    connect(pattern, &PatternModel::midiChannelChanged, this, &SequenceModel::setDirty);
    connect(pattern, &PatternModel::layerDataChanged, this, &SequenceModel::setDirty);
    connect(pattern, &PatternModel::noteLengthChanged, this, &SequenceModel::setDirty);
    connect(pattern, &PatternModel::availableBarsChanged, this, &SequenceModel::setDirty);
    connect(pattern, &PatternModel::activeBarChanged, this, &SequenceModel::setDirty);
    connect(pattern, &PatternModel::bankOffsetChanged, this, &SequenceModel::setDirty);
    connect(pattern, &PatternModel::bankLengthChanged, this, &SequenceModel::setDirty);
    connect(pattern, &PatternModel::enabledChanged, this, &SequenceModel::setDirty);
    connect(pattern, &NotesModel::lastModifiedChanged, this, &SequenceModel::setDirty);
    int insertionRow = d->patternModels.count();
    if (row > -1) {
        // If we've been requested to add in a specific location, do so
        insertionRow = qMin(qMax(0, row), d->patternModels.count());
    }
    if (!d->isLoading) { beginInsertRows(QModelIndex(), insertionRow, insertionRow); }
    d->patternModels.insert(insertionRow, pattern);
    if (!d->isLoading) { endInsertRows(); }
    setActivePattern(d->activePattern);
}

void SequenceModel::removePattern(PatternModel* pattern)
{
    int removalPosition = d->patternModels.indexOf(pattern);
    if (removalPosition > -1) {
        if (!d->isLoading) { beginRemoveRows(QModelIndex(), removalPosition, removalPosition); }
        d->patternModels.removeAt(removalPosition);
        pattern->disconnect(this);
        setActivePattern(d->activePattern);
        if (!d->isLoading) { endRemoveRows(); }
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

void SequenceModel::setBpm(int bpm)
{
    if(d->bpm != bpm) {
        d->bpm = bpm;
        Q_EMIT bpmChanged();
    }
}

int SequenceModel::bpm() const
{
    return d->bpm;
}

void SequenceModel::setActivePattern(int activePattern)
{
    int adjusted = qMin(qMax(0, activePattern), d->patternModels.count());
    if (d->activePattern != adjusted) {
        d->activePattern = adjusted;
        Q_EMIT activePatternChanged();
        setDirty();
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

QString SequenceModel::filePath() const
{
    return d->filePath;
}

void SequenceModel::setFilePath(const QString &filePath)
{
    if (d->filePath != filePath) {
        d->filePath = filePath;
        Q_EMIT filePathChanged();
    }
}

bool SequenceModel::isDirty() const
{
    return d->isDirty;
}

void SequenceModel::setIsDirty(bool isDirty)
{
    if (d->isDirty != isDirty) {
        d->isDirty = isDirty;
        Q_EMIT isDirtyChanged();
    }
}

void SequenceModel::load(const QString &fileName)
{
    d->isLoading = true;
    beginResetModel();
    QString data;
    d->ensureFilePath(fileName);
    QFile file(d->filePath);

    // Clear our the existing model...
    for (PatternModel *model : d->patternModels) {
        model->disconnect(this);
    }
    d->patternModels.clear();

    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
            data = QString::fromUtf8(file.readAll());
            file.close();
        }
    }
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data.toUtf8());
    if (jsonDoc.isObject()) {
        QJsonObject obj = jsonDoc.object();
        // Load the patterns from disk
//         const QString sequenceNameForFiles = QString(objectName().toLower()).replace(" ", "-");
        for (int i = 0; i < PATTERN_COUNT; ++i) {
            // TODO Not awesome... We need to fix this, that should not be called global :P .arg(sequenceNameForFiles)
            QFile patternFile(QString("%1/patterns/global-%2.pattern.json").arg(d->filePath.left(d->filePath.lastIndexOf("/"))).arg(QString::number(i + 1)));
            PatternModel *model = qobject_cast<PatternModel*>(playGridManager()->getPatternModel(QString("Pattern %1 - %2").arg(QString::number(i + 1)).arg(objectName()), objectName()));
            model->clear();
            if (patternFile.exists()) {
                if (patternFile.open(QIODevice::ReadOnly)) {
                    QString patternData = QString::fromUtf8(patternFile.readAll());
                    patternFile.close();
                    playGridManager()->setModelFromJson(model, patternData);
                }
            }
        }
        setActivePattern(obj.value("activePattern").toInt());
        setBpm(obj.value("bpm").toInt());
    }
    // This ensures that when we're first creating ourselves a sequence, we end up with some models in it
    if (d->patternModels.count() < PATTERN_COUNT) {
        for (int i = d->patternModels.count(); i < PATTERN_COUNT; ++i) {
            PatternModel *model = qobject_cast<PatternModel*>(playGridManager()->getPatternModel(QString("Pattern %1 - %2").arg(QString::number(i + 1)).arg(objectName()), objectName()));
            model->clear();
        }
    }
    if (activePattern() == -1) {
        setActivePattern(0);
    }
    setIsDirty(false);
    endResetModel();
    d->isLoading = false;
}

bool SequenceModel::save(const QString &fileName, bool exportOnly)
{
    bool success = false;

    QJsonObject sequenceObject;
    sequenceObject["activePattern"] = activePattern();
    sequenceObject["bpm"] = bpm();

    QJsonDocument jsonDoc;
    jsonDoc.setObject(sequenceObject);
    QString data = jsonDoc.toJson();

    QString saveToPath;
    if (exportOnly) {
        saveToPath = fileName;
    } else {
        d->ensureFilePath(fileName);
        saveToPath = d->filePath;
    }
    QDir sequenceLocation(saveToPath.left(saveToPath.lastIndexOf("/")));
    QDir patternLocation(saveToPath.left(saveToPath.lastIndexOf("/")) + "/patterns");
    if (sequenceLocation.exists() || sequenceLocation.mkpath(sequenceLocation.path())) {
        QFile dataFile(saveToPath);
        if (dataFile.open(QIODevice::WriteOnly) && dataFile.write(data.toUtf8())) {
            dataFile.close();
            if (patternLocation.exists() || patternLocation.mkpath(patternLocation.path())) {
                int i{0};
                const QString sequenceNameForFiles = QString(objectName().toLower()).replace(" ", "-");
                for (PatternModel *pattern : d->patternModels) {
                    QString fileName = QString("%1/%2-%3.pattern.json").arg(patternLocation.path()).arg(sequenceNameForFiles).arg(QString::number(i + 1));
                    pattern->exportToFile(fileName);
                    ++i;
                }
            }
            success = true;
        }
    }
    setIsDirty(false);
    return success;
}

void SequenceModel::clear()
{
    for (PatternModel *pattern : d->patternModels) {
        pattern->clear();
        pattern->setMidiChannel(0);
        pattern->setLayerData("");
        pattern->setNoteLength(3);
        pattern->setAvailableBars(1);
        pattern->setActiveBar(0);
        pattern->setBankOffset(0);
        pattern->setBankLength(8);
        pattern->setEnabled(true);
    }
    setActivePattern(0);
}

QObject* SequenceModel::song() const
{
    return d->song;
}

void SequenceModel::setSong(QObject* song)
{
    if (d->song != song) {
        if (d->song) {
            d->song->disconnect(this);
        }
        d->song = song;
        if (d->song) {
            QString sketchFolder = d->song->property("sketchFolder").toString();
            const QString sequenceNameForFiles = QString(objectName().toLower()).replace(" ", "-");
            setFilePath(QString("%1/sequences/%2/metadata.sequence.json").arg(sketchFolder).arg(sequenceNameForFiles));
        }
        load();
        Q_EMIT songChanged();
    }
}

int SequenceModel::soloPattern() const
{
    return d->soloPattern;
}

PatternModel* SequenceModel::soloPatternObject() const
{
    return d->soloPatternObject;
}

void SequenceModel::setSoloPattern(int soloPattern)
{
    if (d->soloPattern != soloPattern) {
        d->soloPattern = soloPattern;
        if (d->soloPattern > -1 && d->soloPattern < d->patternModels.count()) {
            d->soloPatternObject = d->patternModels[d->soloPattern];
        } else {
            d->soloPatternObject = nullptr;
        }
        Q_EMIT soloPatternChanged();
        setDirty();
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

bool SequenceModel::isPlaying() const
{
    return d->isPlaying;
}

void SequenceModel::prepareSequencePlayback()
{
    if (!d->isPlaying) {
        d->isPlaying = true;
        Q_EMIT isPlayingChanged();
        // These two must be direct connections, or things will not be done in the correct
        // order, and all the notes will end up scheduled at the wrong time, and the
        // pattern position will be set sporadically, which leads to everything
        // all kinds of looking laggy and weird. So, direct connection.
        connect(playGridManager(), &PlayGridManager::metronomeBeat128thChanged, this, &SequenceModel::advanceSequence, Qt::DirectConnection);
        connect(playGridManager(), &PlayGridManager::metronomeBeat128thChanged, this, &SequenceModel::updatePatternPositions, Qt::DirectConnection);
        // pre-fill the first beat with notes - the first beat will also call the function,
        // but will do so for +1, not current cumulativeBeat, so we need to prefill things a bit.
        for (PatternModel *pattern : d->patternModels) {
            pattern->handleSequenceAdvancement(d->syncTimer->cumulativeBeat() - 1, 6);
        }
    }
    playGridManager()->hookUpTimer();
}

void SequenceModel::startSequencePlayback()
{
    prepareSequencePlayback();
    playGridManager()->startMetronome();
}

void SequenceModel::disconnectSequencePlayback()
{
    if (d->isPlaying) {
        disconnect(playGridManager(), &PlayGridManager::metronomeBeat128thChanged, this, &SequenceModel::advanceSequence);
        disconnect(playGridManager(), &PlayGridManager::metronomeBeat128thChanged, this, &SequenceModel::updatePatternPositions);
        d->isPlaying = false;
        Q_EMIT isPlayingChanged();
    }
    for (QObject *noteObject : d->queuedForOffNotes) {
        Note *note = qobject_cast<Note*>(noteObject);
        note->setOff();
    }
    for (PatternModel *pattern : d->patternModels) {
        pattern->handleSequenceStop();
    }
    d->queuedForOffNotes.clear();
}

void SequenceModel::stopSequencePlayback()
{
    if (d->isPlaying) {
        disconnectSequencePlayback();
        playGridManager()->stopMetronome();
    }
}

void SequenceModel::resetSequence()
{
    // This function is mostly cosmetic... the playback will, in fact, follow the global beat.
    // TODO Maybe we need some way of feeding some reset information back to the sync timer from here?
    for (PatternModel *pattern : d->patternModels) {
        pattern->updateSequencePosition(0);
    }
}

void SequenceModel::advanceSequence()
{
    /// TODO Optimally, this duration wants to be 1 plus the number of notes that would fit inside the Jack buffers we're supposed to be dealing with.
    /// Logic for calculating this can be found in SyncTimer. For now, 6 does us ok
    int sequenceProgressionLength{6};
    if (d->soloPattern > -1 && d->soloPattern < d->patternModels.count()) {
        PatternModel *pattern = d->patternModels[d->soloPattern];
        if (pattern) {
            pattern->handleSequenceAdvancement(d->syncTimer->cumulativeBeat(), sequenceProgressionLength);
        }
    } else {
        for (PatternModel *pattern : d->patternModels) {
            pattern->handleSequenceAdvancement(d->syncTimer->cumulativeBeat(), sequenceProgressionLength);
        }
    }
}

void SequenceModel::updatePatternPositions()
{
    if (d->soloPattern > -1 && d->soloPattern < d->patternModels.count()) {
        PatternModel *pattern = d->patternModels[d->soloPattern];
        if (pattern) {
            pattern->updateSequencePosition(d->syncTimer->cumulativeBeat());
        }
    } else {
        for (PatternModel *pattern : d->patternModels) {
            pattern->updateSequencePosition(d->syncTimer->cumulativeBeat());
        }
    }
}
