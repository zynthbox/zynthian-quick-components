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
#include "SegmentHandler.h"

#include <libzl.h>
#include <SyncTimer.h>

#include <QCollator>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#define TRACK_COUNT 10
#define PART_COUNT 5
#define PATTERN_COUNT (TRACK_COUNT * PART_COUNT)
static const QStringList sketchNames{"S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "S9", "S10"};
static const QStringList partNames{"a", "b", "c", "d", "e"};

class ZLSequenceSynchronisationManager : public QObject {
Q_OBJECT
public:
    explicit ZLSequenceSynchronisationManager(SequenceModel *parent = 0)
        : QObject(parent)
        , q(parent)
    {
        connect(q, &SequenceModel::sceneIndexChanged, this, &ZLSequenceSynchronisationManager::selectedSketchIndexChanged, Qt::QueuedConnection);
        // This actually means current /track/ changed, the track index and our current midi channel are the same number
        connect(q->playGridManager(), &PlayGridManager::currentMidiChannelChanged, this, &ZLSequenceSynchronisationManager::currentMidiChannelChanged, Qt::QueuedConnection);
    };
    SequenceModel *q{nullptr};
    QObject *zlSong{nullptr};
    QObject *zlScenesModel{nullptr};

    void setZlSong(QObject *newZlSong) {
        if (zlSong != newZlSong) {
            if (zlSong) {
                zlSong->disconnect(this);
            }
            zlSong = newZlSong;
            if (zlSong) {
                connect(zlSong, SIGNAL(bpm_changed()), this, SLOT(bpmChanged()), Qt::QueuedConnection);
                connect(zlSong, SIGNAL(__scenes_model_changed__()), this, SLOT(scenesModelChanged()), Qt::QueuedConnection);
                bpmChanged();
            }
            scenesModelChanged();
            currentMidiChannelChanged();
        }
    }

    void setZlScenesModel(QObject *newZlScenesModel) {
        if (zlScenesModel != newZlScenesModel) {
            if (zlScenesModel) {
                zlScenesModel->disconnect(this);
            }
            zlScenesModel = newZlScenesModel;
            if (zlScenesModel) {
                connect(zlScenesModel, SIGNAL(selected_sketch_index_changed()), this, SLOT(selectedSketchIndexChanged()), Qt::QueuedConnection);
                selectedSketchIndexChanged();
            }
        }
    }
public Q_SLOTS:
    void bpmChanged() {
        q->setBpm(zlSong->property("bpm").toInt());
        qobject_cast<SyncTimer*>(SyncTimer_instance())->setBpm(q->bpm());
    }
    void scenesModelChanged() {
        setZlScenesModel(zlSong->property("scenesModel").value<QObject*>());
    }
    void selectedSketchIndexChanged() {
        if (zlScenesModel) {
            const int selectedSketchIndex = zlScenesModel->property("selectedSketchIndex").toInt();
            q->setShouldMakeSounds(selectedSketchIndex == q->sceneIndex());
        }
    }
    void currentMidiChannelChanged() {
        if (zlSong) {
            QObject *tracksModel = zlSong->property("tracksModel").value<QObject*>();
            QObject *track{nullptr};
            QMetaObject::invokeMethod(tracksModel, "getTrack", Qt::DirectConnection, Q_RETURN_ARG(QObject*, track), Q_ARG(int, PlayGridManager::instance()->currentMidiChannel()));
            if (track) {
                const int trackId{track->property("id").toInt()};
                const int selectedPart{track->property("selectedPart").toInt()};
                q->setActiveTrack(trackId, selectedPart);
            }
        }
    }
};

class SequenceModel::Private {
public:
    Private(SequenceModel *q)
        : q(q)
    {}
    SequenceModel *q;
    ZLSequenceSynchronisationManager *zlSyncManager{nullptr};
    PlayGridManager *playGridManager{nullptr};
    SyncTimer *syncTimer{nullptr};
    SegmentHandler *segmentHandler{nullptr};
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
    int sceneIndex{-1};
    bool shouldMakeSounds{true};
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
    d->zlSyncManager = new ZLSequenceSynchronisationManager(this);
    d->syncTimer = qobject_cast<SyncTimer*>(SyncTimer_instance());
    d->segmentHandler = SegmentHandler::instance();
    connect(d->syncTimer, &SyncTimer::timerRunningChanged, this, [this](){
        if (!d->syncTimer->timerRunning()) {
            stopSequencePlayback();
        }
    }, Qt::DirectConnection);
    // Save yourself anytime changes, but not too often, and only after a second... Let's be a bit gentle here
    QTimer *saveThrottle = new QTimer(this);
    saveThrottle->setSingleShot(true);
    saveThrottle->setInterval(1000);
    connect(saveThrottle, &QTimer::timeout, this, [this](){ if (isDirty()) { save(); } });
    connect(this, &SequenceModel::isDirtyChanged, saveThrottle, QOverload<>::of(&QTimer::start));
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

QObject *SequenceModel::getByPart(int trackIndex, int partIndex) const
{
    QObject *pattern{nullptr};
    for (PatternModel *needle : d->patternModels) {
        if (needle->trackIndex() == trackIndex && needle->partIndex() == partIndex) {
            pattern = needle;
            break;
        }
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
    connect(pattern, &NotesModel::lastModifiedChanged, this, &SequenceModel::setDirty);
    int insertionRow = d->patternModels.count();
    if (row > -1) {
        // If we've been requested to add in a specific location, do so
        insertionRow = qMin(qMax(0, row), d->patternModels.count());
    }
    if (!d->isLoading) { beginInsertRows(QModelIndex(), insertionRow, insertionRow); }
    d->patternModels.insert(insertionRow, pattern);
    if (!d->isLoading) {
        endInsertRows();
        setActivePattern(d->activePattern);
    }
    if (!d->isLoading) { Q_EMIT countChanged(); }
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
    if (!d->isLoading) { Q_EMIT countChanged(); }
}

bool SequenceModel::contains(QObject* pattern) const
{
    return d->patternModels.contains(qobject_cast<PatternModel*>(pattern));
}

int SequenceModel::indexOf(QObject *pattern) const
{
    return d->patternModels.indexOf(qobject_cast<PatternModel*>(pattern));
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

void SequenceModel::setActiveTrack(int trackId, int partId)
{
    setActivePattern((trackId * PART_COUNT) + partId);
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

bool SequenceModel::isLoading() const
{
    return d->isLoading;
}

int SequenceModel::sceneIndex() const
{
    return d->sceneIndex;
}

void SequenceModel::setSceneIndex(int sceneIndex)
{
    if (d->sceneIndex != sceneIndex) {
        d->sceneIndex = sceneIndex;
        Q_EMIT sceneIndexChanged();
    }
}

bool SequenceModel::shouldMakeSounds() const
{
    return d->shouldMakeSounds;
}

void SequenceModel::setShouldMakeSounds(bool shouldMakeSounds)
{
    if (d->shouldMakeSounds != shouldMakeSounds) {
        d->shouldMakeSounds = shouldMakeSounds;
        Q_EMIT shouldMakeSoundsChanged();
    }
}

void SequenceModel::load(const QString &fileName)
{
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();
    int loadedPatternCount{0};
    d->isLoading = true;
    Q_EMIT isLoadingChanged();
    beginResetModel();
    QString data;
    d->ensureFilePath(fileName);
    QFile file(d->filePath);

    // Clear our the existing model...
    QList<PatternModel*> oldModels = d->patternModels;
    for (PatternModel *model : d->patternModels) {
        model->disconnect(this);
        model->startLongOperation();
    }
    d->patternModels.clear();

    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
            data = QString::fromUtf8(file.readAll());
            file.close();
        }
    }
    const QString sketchName{sketchNames.contains(objectName()) ? objectName() : ""};
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data.toUtf8());
    if (jsonDoc.isObject()) {
        // First, load the patterns from disk
        QDir dir(QString("%1/patterns").arg(d->filePath.left(d->filePath.lastIndexOf("/"))));
        QFileInfoList entries = dir.entryInfoList({"*.pattern.json"}, QDir::Files, QDir::NoSort);
        QCollator collator;
        collator.setNumericMode(true);
        std::sort(entries.begin(), entries.end(), [&](const QFileInfo &file1, const QFileInfo &file2){ return collator.compare(file1.absoluteFilePath(), file2.absoluteFilePath()) < 0; });
        // Now we have a list of all the entries in the patterns directory that has the pattern
        // file suffix, sorted naturally (so 10 is at the end, not after 1, which is just silly)
        int actualIndex{0};
        for (const QFileInfo &entry : entries) {
            // The filename for patterns is "sequencename-(trackIndex)(partName).pattern.json"
            // where trackIndex is a number from 1 through 10 and partName is a single lower-case letter
            const QString absolutePath{entry.absoluteFilePath()};
            const int startPos{absolutePath.lastIndexOf('-')};
            const int length{absolutePath.length() - startPos - 14}; // 14 is the length+1 of the string .pattern.json, our pattern file suffix
            int trackIndex{absolutePath.midRef(startPos + 1, length - 1).toInt() - 1};
            const QString partName{absolutePath.mid(startPos + length, 1)};
            int partIndex = partNames.indexOf(partName);
//             qDebug() << "Loading pattern" << trackIndex + 1 << partName << "for sequence" << this << "from file" << absolutePath;
            while (actualIndex < (trackIndex * PART_COUNT) + partIndex) {
                // then we're missing some patterns, which is not great and we should deal with that so we don't end up with holes in the model...
                const int intermediaryTrackIndex = actualIndex / PART_COUNT;
                const QString &intermediaryPartName = partNames[actualIndex - (intermediaryTrackIndex * PART_COUNT)];
                PatternModel *model = qobject_cast<PatternModel*>(playGridManager()->getPatternModel(QString("Sketch %1-%2%3").arg(sketchName).arg(QString::number(intermediaryTrackIndex + 1)).arg(intermediaryPartName), this));
                model->startLongOperation();
                model->resetPattern(true);
                model->setTrackIndex(intermediaryTrackIndex);
                model->setPartIndex(actualIndex % PART_COUNT);
                insertPattern(model);
                model->endLongOperation();
//                 qWarning() << "Sequence missing patterns prior to that, added:" << model;
                ++actualIndex;
            }
            PatternModel *model = qobject_cast<PatternModel*>(playGridManager()->getPatternModel(QString("Sketch %1-%2%3").arg(sketchName).arg(QString::number(trackIndex + 1)).arg(partName), this));
            model->startLongOperation();
            model->resetPattern(true);
            model->setTrackIndex(trackIndex);
            model->setPartIndex(partIndex);
            insertPattern(model);
            if (entry.exists()) {
                QFile patternFile{absolutePath};
                if (patternFile.open(QIODevice::ReadOnly)) {
                    QString patternData = QString::fromUtf8(patternFile.readAll());
                    patternFile.close();
                    playGridManager()->setModelFromJson(model, patternData);
                }
            }
            model->endLongOperation();
            ++loadedPatternCount;
//             qWarning() << "Loaded and added:" << model;
            ++actualIndex;
        }
        // Then set the values on the sequence
        QJsonObject obj = jsonDoc.object();
        setActivePattern(obj.value("activePattern").toInt());
        setBpm(obj.value("bpm").toInt());
    }
    // This ensures that when we're first creating ourselves a sequence, we end up with some models in it
    if (d->patternModels.count() < PATTERN_COUNT) {
        for (int i = d->patternModels.count(); i < PATTERN_COUNT; ++i) {
            const int intermediaryTrackIndex = i / PART_COUNT;
            const QString &intermediaryPartName = partNames[i % PART_COUNT];
            PatternModel *model = qobject_cast<PatternModel*>(playGridManager()->getPatternModel(QString("Sketch %1-%2%3").arg(sketchName).arg(QString::number(intermediaryTrackIndex + 1)).arg(intermediaryPartName), this));
            model->startLongOperation();
            model->resetPattern(true);
            model->setTrackIndex(intermediaryTrackIndex);
            model->setPartIndex(i % PART_COUNT);
            insertPattern(model);
            model->endLongOperation();
//             qDebug() << "Added missing model" << intermediaryTrackIndex << intermediaryPartName << "to" << objectName() << model->trackIndex() << model->partIndex();
        }
    }
    if (activePattern() == -1) {
        setActivePattern(0);
    }
    setIsDirty(false);
    endResetModel();
    d->isLoading = false;
    // Unlock the patterns, in case...
    for (PatternModel *model : oldModels) {
        model->endLongOperation();
    }
    Q_EMIT isLoadingChanged();
    Q_EMIT countChanged();
    qDebug() << this << "Loaded" << loadedPatternCount << "patterns and filled in" << PATTERN_COUNT - loadedPatternCount << "in" << elapsedTimer.elapsed() << "milliseconds";
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
                    QString patternIdentifier = QString::number(i + 1);
                    if (pattern->trackIndex() > -1 && pattern->partIndex() > -1) {
                        patternIdentifier = QString("%1%2").arg(QString::number(pattern->trackIndex() + 1)).arg(partNames[pattern->partIndex()]);
                    }
                    QString fileName = QString("%1/%2-%3.pattern.json").arg(patternLocation.path()).arg(sequenceNameForFiles).arg(patternIdentifier);
                    QFile patternFile(fileName);
                    if (pattern->hasNotes()) {
                        pattern->exportToFile(fileName);
                    } else if (patternFile.exists()) {
                        patternFile.remove();
                    }
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
        d->zlSyncManager->setZlSong(song);
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
        if (d->shouldMakeSounds) {
            for (PatternModel *pattern : d->patternModels) {
                pattern->handleSequenceAdvancement(d->syncTimer->cumulativeBeat(), d->syncTimer->scheduleAheadAmount(), 0);
            }
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
    if (d->shouldMakeSounds || d->segmentHandler->songMode()) {
        // The timer schedules ahead internally for sequence advancement type things,
        // so the sequenceProgressionLength thing is only for prefilling at this point.
        const quint64 sequenceProgressionLength{1};
        if (d->soloPattern > -1 && d->soloPattern < d->patternModels.count()) {
            PatternModel *pattern = d->patternModels[d->soloPattern];
            if (pattern) {
                pattern->handleSequenceAdvancement(d->syncTimer->cumulativeBeat(), sequenceProgressionLength);
            }
        } else {
            for (PatternModel *pattern : qAsConst(d->patternModels)) {
                pattern->handleSequenceAdvancement(d->syncTimer->cumulativeBeat(), sequenceProgressionLength);
            }
        }
    }
}

void SequenceModel::updatePatternPositions()
{
    if (d->shouldMakeSounds) {
        if (d->soloPattern > -1 && d->soloPattern < d->patternModels.count()) {
            PatternModel *pattern = d->patternModels[d->soloPattern];
            if (pattern) {
                pattern->updateSequencePosition(d->syncTimer->cumulativeBeat());
            }
        } else {
            for (PatternModel *pattern : qAsConst(d->patternModels)) {
                pattern->updateSequencePosition(d->syncTimer->cumulativeBeat());
            }
        }
    }
}

// Since we've got a QObject up at the top which wants mocing
#include "SequenceModel.moc"
