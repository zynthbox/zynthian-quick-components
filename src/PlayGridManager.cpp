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

#include "PlayGridManager.h"
#include "Note.h"
#include "NotesModel.h"
#include "PatternModel.h"
#include "SettingsContainer.h"
#include "MidiListener.h"

// ZynthiLoops library
// Hackety hack - we don't need all the thing, just need some storage things (MidiBuffer and MidiNote specifically)
#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1
#include <juce_audio_formats/juce_audio_formats.h>
#include <libzl.h>
#include <ClipAudioSource.h>
#include <SyncTimer.h>

#include <QQmlEngine>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileSystemWatcher>
#include <QList>
#include <QQmlComponent>
#include <QStandardPaths>
#include <QSettings>

#include <RtMidi.h>

Q_GLOBAL_STATIC(QList<PlayGridManager*>, timer_callback_tickers)
void timer_callback(int beat) {
    for (PlayGridManager* pgm : *timer_callback_tickers) {
        pgm->metronomeTick(beat);
    }
}

class PlayGridManager::Private
{
public:
    Private(PlayGridManager *q) : q(q) {
        // Let's try and avoid any unnecessary things here...
        midiMessage.push_back(0);
        midiMessage.push_back(0);
        midiMessage.push_back(0);

        updatePlaygrids();
        connect(&watcher, &QFileSystemWatcher::directoryChanged, q, [this](){
            updatePlaygrids();
        });
        ensureMidiOutput();
        listenToEverything();
        currentPlaygrids = {
            {"minigrid", 1}, // As these are sorted alphabetically, notesgrid for minigrid and
            {"playgrid", 0}, // drumatique for playgrid
        };
    }
    ~Private() {
        if (midiout) {
            delete midiout;
        }
        for (MidiListener *midiListener : midiListeners) {
            midiListener->markAsDone();
        }
    }
    PlayGridManager *q;
    QQmlEngine *engine{nullptr};
    QStringList playgrids;
    QVariantMap currentPlaygrids;
    QString preferredSequencer;
    QVariantMap dashboardModels;
    int pitch{0};
    int modulation{0};
    QMap<QString, SequenceModel*> sequenceModels;
    QMap<QString, PatternModel*> patternModels;
    QMap<QString, NotesModel*> notesModels;
    QList<Note*> notes;
    QMap<QString, SettingsContainer*> settingsContainers;
    QMap<QString, QObject*> namedInstances;
    QMap<Note*, int> noteStateMap;
    QVariantList mostRecentlyChangedNotes;
    int currentMidiChannel{-1};

    RtMidiOut *midiout{nullptr};
    std::vector<unsigned char> midiMessage;
    QList<MidiListener*> midiListeners;

    SyncTimer *syncTimer{nullptr};
    int metronomeBeat4th{0};
    int metronomeBeat8th{0};
    int metronomeBeat16th{0};
    int metronomeBeat32nd{0};
    int metronomeBeat64th{0};
    int metronomeBeat128th{0};

    QFileSystemWatcher watcher;

    void listenToEverything() {
        for (MidiListener *midiListener : midiListeners) {
            midiListener->markAsDone();
        }
        midiListeners.clear();
        RtMidiIn *midiin = new RtMidiIn(RtMidi::UNIX_JACK);
        unsigned int nPorts = midiin->getPortCount();
        if ( nPorts > 0 ) {
            std::cout << "\nThere are " << nPorts << " MIDI input ports available.\n" << std::endl;
            const char* zynMidiRouterOutName{"ZynMidiRouter:ch"};
            for (unsigned int i = 0; i < nPorts; ++i) {
                const std::string portName = midiin->getPortName(i);
                if (portName.rfind(zynMidiRouterOutName, 0) == 0) {
                    MidiListener *midiListener = new MidiListener(i);
                    QObject::connect(midiListener, &QThread::finished, midiListener, &QObject::deleteLater);
                    QObject::connect(midiListener, &MidiListener::noteChanged, q, [this](int midiNote, int midiChannel, int velocity, bool setOn, const unsigned char &byte1, const unsigned char &byte2, const unsigned char &byte3){ updateNoteState(midiNote, midiChannel, velocity, setOn, byte1, byte2, byte3); }, Qt::QueuedConnection);
                    midiListener->start();
                    midiListeners << midiListener;
                }
            }
        }
    }
    void ensureMidiOutput() {
        // RtMidiOut constructor
        try {
            midiout = new RtMidiOut(RtMidi::UNIX_JACK);
        }
        catch ( RtMidiError &error ) {
            error.printMessage();
            midiout = nullptr;
        }
        if (midiout) {
            // Check outputs.
            unsigned int nPorts = midiout->getPortCount();
            std::string portName;
            std::cout << "\nThere are " << nPorts << " MIDI output ports available.\n";
            for (unsigned int i = 0; i < nPorts; ++i) {
                try {
                    portName = midiout->getPortName(i);
                    if (portName.rfind("ZynMidiRouter:main_in", 0) == 0) {
                        std::cout << "Using output port " << i << " named " << portName << std::endl;
                        midiout->openPort(i);
                    } else {
                        std::cout << "Not using output port " << i << " named " << portName << std::endl;
                    }
                }
                catch (RtMidiError &error) {
                    error.printMessage();
                    delete midiout;
                    midiout = nullptr;
                }
            }
        }
        if (!midiout) {
            std::cout << "Failed to open an actual midi output, which clearly is not great." << std::endl;
        }
    }

    void updateNoteState(int midiNote, int midiChannel, int velocity, bool setOn, const unsigned char &byte1, const unsigned char &byte2, const unsigned char &byte3) {
        static const QLatin1String note_on{"note_on"};
        static const QLatin1String note_off{"note_off"};

        bool shouldAdd{true};
        const qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        for (int i = mostRecentlyChangedNotes.count() - 1; i >= 0; --i) {
            const QVariantMap &previousMostRecent = mostRecentlyChangedNotes.at(i).toMap();
            if (currentTime - previousMostRecent["timestamp"].value<qint64>() > 10) {
                break;
            }
            if (previousMostRecent["note"] == midiNote && previousMostRecent["channel"] == midiChannel && previousMostRecent["velocity"] == velocity && previousMostRecent["type"] == (setOn ? note_on : note_off)) {
                shouldAdd = false;
                break;
            }
        }
        if (shouldAdd) {
            // First notify all our friends of the thing (because they might like to know very quickly)
            Q_EMIT q->midiMessage(byte1, byte2, byte3);
            QVariantMap metadata;
            metadata["note"] = midiNote;
            metadata["channel"] = midiChannel;
            metadata["velocity"] = velocity;
            metadata["type"] = setOn ? note_on : note_off;
            metadata.insert("timestamp", QVariant::fromValue<qint64>(currentTime));
            mostRecentlyChangedNotes << metadata;
            while (mostRecentlyChangedNotes.count() > 100) {
                mostRecentlyChangedNotes.removeFirst();
            }
            Q_EMIT q->mostRecentlyChangedNotesChanged();
        }

        Note *note = findExistingNote(midiNote, midiChannel);
        if (note) {
            if (setOn) {
                note->setIsPlaying(true);
            } else {
                note->setIsPlaying(false);
            }
        }
    }

    void updatePlaygrids()
    {
        static const QStringList searchlist{QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.local/share/zynthian/playgrids", "/home/pi/zynthian-ui/qml-ui/playgrids"};
        QStringList newPlaygrids;

        for (const QString &searchdir : searchlist) {
            QDir dir(searchdir);
            if (dir.exists()) {
                QDirIterator it(searchdir);
                while (it.hasNext()) {
                    const QFileInfo fi(it.next() + "/main.qml");
                    if (it.fileName() != "." && it.fileName() != "..") {
                        if (fi.exists()) {
                            newPlaygrids << fi.absolutePath();
                        } else {
                            qDebug() << Q_FUNC_INFO << "A stray directory that does not contain a main.qml file was found in one of the playgrid search locations: " << fi.absolutePath();
                        }
                    }
                }
            } else {
                // A little naughty, but knewstuff kind of removes directories once everything in it's gone
                dir.mkpath(searchdir);
            }
            if (!watcher.directories().contains(searchdir)) {
                watcher.addPath(searchdir);
            }
        }

        newPlaygrids.sort();
        // Start out by clearing known playgrids - it's a bit of a hack, but it ensures that for e.g. when updating a playgrid from the store, that will also be picked up and reloaded
        playgrids.clear();
        Q_EMIT q->playgridsChanged();
        playgrids = newPlaygrids;
        Q_EMIT q->playgridsChanged();
        qDebug() << Q_FUNC_INFO << "We now have the following known grids:" << playgrids;
    }

    Note *findExistingNote(int midiNote, int midiChannel) {
        Note *note{nullptr};
        for (Note *aNote : notes) {
            if (aNote->midiNote() == midiNote && aNote->midiChannel() == midiChannel) {
                note = aNote;
                break;
            }
        }
        return note;
    }

    QJsonArray generateModelNotesSection(const NotesModel* model) {
        QJsonArray modelArray;
        for (int row = 0; row < model->rowCount(); ++row) {
            QJsonArray rowArray;
            for (int column = 0; column < model->columnCount(model->index(row)); ++column) {
                QJsonObject obj;
                obj.insert("note", q->noteToJsonObject(qobject_cast<Note*>(model->getNote(row, column))));
                obj.insert("metadata", QJsonValue::fromVariant(model->getMetadata(row, column)));
                rowArray.append(obj);
            }
            modelArray << QJsonValue(rowArray);
        }
        return modelArray;
    }
};

void PlayGridManager::setEngine(QQmlEngine* engine)
{
    d->engine = engine;
}

PlayGridManager::PlayGridManager(QObject* parent)
    : QObject(parent)
    , d(new Private(this))
{
    setSyncTimer(SyncTimer_instance());
    QDir mySequenceLocation{QString("%1/sequences/my-sequences").arg(QString(qgetenv("ZYNTHIAN_MY_DATA_DIR")))};
    if (!mySequenceLocation.exists()) {
        mySequenceLocation.mkpath(mySequenceLocation.path());
    }
    QDir communitySequenceLocation{QString("%1/sequences/community-sequences").arg(QString(qgetenv("ZYNTHIAN_MY_DATA_DIR")))};
    if (!communitySequenceLocation.exists()) {
        communitySequenceLocation.mkpath(communitySequenceLocation.path());
    }

    QSettings settings;
    settings.beginGroup("PlayGridManager");
    d->preferredSequencer = settings.value("preferredSequencer", "").toString();
    connect(this, &PlayGridManager::sequenceEditorIndexChanged, [this](){
        QSettings settings;
        settings.beginGroup("PlayGridManager");
        settings.setValue("preferredSequencer", d->preferredSequencer);
    });
}

PlayGridManager::~PlayGridManager()
{
    delete d;
}

QStringList PlayGridManager::playgrids() const
{
    return d->playgrids;
}

void PlayGridManager::updatePlaygrids()
{
    d->updatePlaygrids();
}

QVariantMap PlayGridManager::currentPlaygrids() const
{
    return d->currentPlaygrids;
}

void PlayGridManager::setCurrentPlaygrid(const QString& section, int index)
{
    if (!d->currentPlaygrids.contains(section) || d->currentPlaygrids[section] != index) {
        d->currentPlaygrids[section] = index;
        Q_EMIT currentPlaygridsChanged();
    }
}

QVariantMap PlayGridManager::dashboardModels() const
{
    return d->dashboardModels;
}

void PlayGridManager::pickDashboardModelItem(QObject* model, int index)
{
    Q_EMIT dashboardItemPicked(model, index);
}

void PlayGridManager::registerDashboardModel(const QString &playgrid, QObject* model)
{
    if (!d->dashboardModels.contains(playgrid)) {
        d->dashboardModels[playgrid] = QVariant::fromValue<QObject*>(model);
        connect(model, &QObject::destroyed, this, [this, playgrid](){
            d->dashboardModels.remove(playgrid);
            Q_EMIT dashboardModelsChanged();
        });
        Q_EMIT dashboardModelsChanged();
    }
}

int PlayGridManager::pitch() const
{
    return d->pitch;
}

#define LoByte(i)    ( (char) i )
#define HiByte(i)    ( (char) ( ((int) i) >> 8) )
void PlayGridManager::setPitch(int pitch)
{
    int adjusted = qBound(0, pitch + 8192, 16383);
    if (d->pitch != adjusted) {
        if (!d->midiout) {
            d->ensureMidiOutput();
        }
        if (d->midiout) {
            int shiftedValue = adjusted << 1;              // shift so top bit of lsb is in msb
            unsigned char msb = HiByte(shiftedValue);      // get the high bits
            unsigned char lsb = LoByte(shiftedValue) >> 1; // get the low 7 bits and shift right
            d->midiMessage[0] = 0xE0;
            d->midiMessage[1] = lsb;
            d->midiMessage[2] = msb;
            d->midiout->sendMessage(&d->midiMessage);
        }
        d->pitch = adjusted;
        Q_EMIT pitchChanged();
    }
}

int PlayGridManager::modulation() const
{
    return d->modulation;
}

void PlayGridManager::setModulation(int modulation)
{
    int adjusted = qBound(0, modulation, 127);
    if (d->modulation != adjusted) {
        if (!d->midiout) {
            d->ensureMidiOutput();
        }
        if (d->midiout) {
            d->midiMessage[0] = 0xB0;
            d->midiMessage[1] = 0x01;
            d->midiMessage[2] = adjusted;
            d->midiout->sendMessage(&d->midiMessage);
        }
        d->modulation = adjusted;
        Q_EMIT modulationChanged();
    }
}

int PlayGridManager::sequenceEditorIndex() const
{
    int sequencerIndex{d->playgrids.indexOf(d->preferredSequencer)};
    if (sequencerIndex < 0) {
        for (int i = 0; i < d->playgrids.count(); ++i) {
            if (d->playgrids[i].contains("drumatique")) {
                sequencerIndex = i;
                break;
            }
        }
    }
    return sequencerIndex;
}

void PlayGridManager::setPreferredSequencer(const QString& playgridID)
{
    d->preferredSequencer = playgridID;
    Q_EMIT sequenceEditorIndexChanged();
}

QObject* PlayGridManager::getSequenceModel(const QString& name)
{
    SequenceModel *model = d->sequenceModels.value(name.isEmpty() ? QLatin1String("Global") : name);
    if (!model) {
        model = new SequenceModel(this);
        model->setObjectName(name);
        QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
        d->sequenceModels[name] = model;
        // CAUTION:
        // This causes a fair bit of IO stuff, and will also create models using getPatternModel
        // below, so make sure this happens _after_ adding it to the map above.
        model->load();
    }
    return model;
}

QObject* PlayGridManager::getPatternModel(const QString&name, SequenceModel *sequence)
{
    PatternModel *model = d->patternModels.value(name);
    if (!model) {
        model = new PatternModel(sequence);
        model->setObjectName(name);
        QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
        d->patternModels[name] = model;
    }
    return model;
}

QObject* PlayGridManager::getPatternModel(const QString& name, const QString& sequenceName)
{
    // CAUTION:
    // This will potentially cause the creation of models using this same function, and so it must
    // happen here, rather than later, as otherwise it will potentially cause infinite recursion
    // in silly ways.
    SequenceModel *sequence = qobject_cast<SequenceModel*>(getSequenceModel(sequenceName));
    PatternModel *model = d->patternModels.value(name);
    if (!model) {
        model = new PatternModel(sequence);
        model->setObjectName(name);
        QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
        d->patternModels[name] = model;
    }
    if (!sequence->contains(model)) {
        sequence->insertPattern(model);
    }
    return model;
}

QObject* PlayGridManager::getNotesModel(const QString& name)
{
    NotesModel *model = d->notesModels.value(name);
    if (!model) {
        model = new NotesModel(this);
        model->setObjectName(name);
        QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
        d->notesModels[name] = model;
    }
    return model;
}

QObject* PlayGridManager::getNote(int midiNote, int midiChannel)
{
    Note *note{nullptr};
    // The channel numbers here /are/ invalid - however, we need them to distinguish "invalid" notes while still having a Note to operate with
    if (0 <= midiNote && midiNote <= 127 && -1 <= midiChannel && midiChannel <= 16) {
        for (Note *aNote : d->notes) {
            if (aNote->midiNote() == midiNote && aNote->midiChannel() == midiChannel) {
                note = aNote;
                break;
            }
        }
        if (!note) {
            static const QStringList note_int_to_str_map{"C", "C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            note = new Note(this);
            note->setName(note_int_to_str_map.value(midiNote % 12));
            note->setMidiNote(midiNote);
            note->setMidiChannel(midiChannel);
            QQmlEngine::setObjectOwnership(note, QQmlEngine::CppOwnership);
            d->notes << note;
        }
    }
    return note;
}

QObject* PlayGridManager::getCompoundNote(const QVariantList& notes)
{
    QObjectList actualNotes;
    for (const QVariant &var : notes) {
        actualNotes << var.value<QObject*>();
    }
    Note *note{nullptr};
    // Make the compound note's fake note value...
    int fake_midi_note = 128;
    int index{1};
    for (QObject *subnote : actualNotes) {
        Note *actualSubnote = qobject_cast<Note*>(subnote);
        if (actualSubnote) {
            fake_midi_note = fake_midi_note + (index * (127 * actualSubnote->midiNote() + (actualSubnote->midiChannel() + 1)));
        } else {
            // BAD CODER! THIS IS NOT A NOTE!
            fake_midi_note = -1;
            break;
        }
        ++index;
    }
    if (fake_midi_note > 127) {
        for (Note *aNote : d->notes) {
            if (aNote->midiNote() == fake_midi_note) {
                note = aNote;
                break;
            }
        }
        if (!note) {
            note = new Note(this);
            note->setMidiNote(fake_midi_note);
            note->setSubnotes(notes);
            QQmlEngine::setObjectOwnership(note, QQmlEngine::CppOwnership);
            d->notes << note;
        }
    }
    return note;
}

QObject* PlayGridManager::getSettingsStore(const QString& name)
{
    SettingsContainer *settings = d->settingsContainers.value(name);
    if (!settings) {
        settings = new SettingsContainer(name, this);
        settings->setObjectName(name);
        QQmlEngine::setObjectOwnership(settings, QQmlEngine::CppOwnership);
        d->settingsContainers[name] = settings;
    }
    return settings;
}

QObject* PlayGridManager::getNamedInstance(const QString& name, const QString& qmlTypeName)
{
    QObject *instance{nullptr};
    if (d->namedInstances.contains(name)) {
        instance = d->namedInstances[name];
    } else if (d->engine) {
        QQmlComponent component(d->engine);
        component.setData(QString("import QtQuick 2.4\n%1 { objectName: \"%2\" }").arg(qmlTypeName).arg(name).toUtf8(), QUrl());
        instance = component.create();
        QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);
        d->namedInstances.insert(name, instance);
    }
    return instance;
}

void PlayGridManager::deleteNamedObject(const QString &name)
{
    QObject *instance{nullptr};
    if (d->namedInstances.contains(name)) {
        instance = d->namedInstances.take(name);
    } else if (d->sequenceModels.contains(name)) {
        instance = d->sequenceModels.take(name);
    } else if (d->patternModels.contains(name)) {
        instance = d->patternModels.take(name);
    } else if (d->settingsContainers.contains(name)) {
        instance = d->settingsContainers.take(name);
    }
    if (instance) {
        instance->deleteLater();
    }
}

QJsonObject PlayGridManager::noteToJsonObject(Note *note) const
{
    QJsonObject jsonObject;
    if (note) {
        jsonObject.insert("midiNote", note->midiNote());
        jsonObject.insert("midiChannel", note->midiChannel());
        if (note->subnotes().count() > 0) {
            QJsonArray subnoteArray;
            for (const QVariant &subnote : note->subnotes()) {
                subnoteArray << noteToJsonObject(subnote.value<Note*>());
            }
            jsonObject.insert("subnotes", subnoteArray);
        }
    }
    return jsonObject;
}

Note *PlayGridManager::jsonObjectToNote(const QJsonObject &jsonObject)
{
    Note *note{nullptr};
    if (jsonObject.contains("subnotes")) {
        QJsonArray subnotes = jsonObject["subnotes"].toArray();
        QVariantList subnotesList;
        for (const QJsonValue &val : subnotes) {
            Note *subnote = jsonObjectToNote(val.toObject());
            subnotesList.append(QVariant::fromValue<QObject*>(subnote));
        }
        note = qobject_cast<Note*>(getCompoundNote(subnotesList));
    } else if (jsonObject.contains("midiNote")) {
        note = qobject_cast<Note*>(getNote(jsonObject.value("midiNote").toInt(), jsonObject.value("midiChannel").toInt()));
    }
    return note;
}

QString PlayGridManager::modelToJson(const QObject* model) const
{
    QJsonDocument json;
    const NotesModel* actualModel = qobject_cast<const NotesModel*>(model);
    const PatternModel* patternModel = qobject_cast<const PatternModel*>(model);
    if (patternModel) {
        QJsonObject modelObject;
        modelObject["height"] = patternModel->height();
        modelObject["width"] = patternModel->width();
        modelObject["noteDestination"] = int(patternModel->noteDestination());
        modelObject["midiChannel"] = patternModel->midiChannel();
        modelObject["noteLength"] = patternModel->noteLength();
        modelObject["availableBars"] = patternModel->availableBars();
        modelObject["activeBar"] = patternModel->activeBar();
        modelObject["bankOffset"] = patternModel->bankOffset();
        modelObject["bankLength"] = patternModel->bankLength();
        modelObject["enabled"] = patternModel->enabled();
        modelObject["layerData"] = patternModel->layerData();
        modelObject["gridModelStartNote"] = patternModel->gridModelStartNote();
        modelObject["gridModelEndNote"] = patternModel->gridModelEndNote();
        modelObject["hasNotes"] = patternModel->hasNotes();
        QJsonDocument notesDoc;
        notesDoc.setArray(d->generateModelNotesSection(patternModel));
        modelObject["notes"] = QString::fromUtf8(notesDoc.toJson());
        // Add in the Sound data from whatever sound is currently in use...
        json.setObject(modelObject);
    } else if (actualModel) {
        json.setArray(d->generateModelNotesSection(actualModel));
    }
    return json.toJson();
}

void PlayGridManager::setModelFromJson(QObject* model, const QString& json)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json.toUtf8());
    if (jsonDoc.isArray()) {
        NotesModel* actualModel = qobject_cast<NotesModel*>(model);
        actualModel->clear();
        QJsonArray notesArray = jsonDoc.array();
        int rowPosition{0};
        for (const QJsonValue &row : notesArray) {
            if (row.isArray()) {
                QVariantList rowList;
                QVariantList rowMetadata;
                QJsonArray rowArray = row.toArray();
                for (const QJsonValue &note : rowArray) {
                    rowList << QVariant::fromValue<QObject*>(jsonObjectToNote(note["note"].toObject()));
                    rowMetadata << note["metadata"].toVariant();
                }
                actualModel->insertRow(rowPosition, rowList, rowMetadata);
            }
            ++rowPosition;
        }
    } else if (jsonDoc.isObject()) {
        PatternModel *pattern = qobject_cast<PatternModel*>(model);
        QJsonObject patternObject = jsonDoc.object();
        if (pattern) {
            setModelFromJson(model, patternObject.value("notes").toString());
            pattern->setHeight(patternObject.value("height").toInt());
            pattern->setWidth(patternObject.value("width").toInt());
            pattern->setMidiChannel(patternObject.value("midiChannel").toInt());
            pattern->setNoteLength(patternObject.value("noteLength").toInt());
            pattern->setAvailableBars(patternObject.value("availableBars").toInt());
            pattern->setActiveBar(patternObject.value("activeBar").toInt());
            pattern->setBankOffset(patternObject.value("bankOffset").toInt());
            pattern->setBankLength(patternObject.value("bankLength").toInt());
            // Because we've not always persisted this... probably wants to go away at some point in the near future
            if (patternObject.contains("enabled")) {
                pattern->setEnabled(patternObject.value("enabled").toBool());
            } else {
                pattern->setEnabled(true);
            }
            if (patternObject.contains("layerData")) {
                pattern->setLayerData(patternObject.value("layerData").toString());
            } else {
                pattern->setLayerData("");
            }
            if (patternObject.contains("noteDestination")) {
                pattern->setNoteDestination(PatternModel::NoteDestination(patternObject.value("noteDestination").toInt()));
            } else {
                pattern->setNoteDestination(PatternModel::SynthDestination);
            }
            if (patternObject.contains("gridModelStartNote")) {
                pattern->setGridModelStartNote(patternObject.value("gridModelStartNote").toInt());
            } else {
                pattern->setGridModelStartNote(48);
            }
            if (patternObject.contains("gridModelEndNote")) {
                pattern->setGridModelEndNote(patternObject.value("gridModelEndNote").toInt());
            } else {
                pattern->setGridModelEndNote(64);
            }
        }
    }
}

void PlayGridManager::setModelFromJsonFile(QObject *model, const QString &jsonFile)
{
    QFile file(jsonFile);
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
            QString data = QString::fromUtf8(file.readAll());
            file.close();
            setModelFromJson(model, data);
        }
    }
}

QString PlayGridManager::notesListToJson(const QVariantList& notes) const
{
    QJsonDocument json;
    QJsonArray notesArray;
    for (const QVariant &element : notes) {
        notesArray << noteToJsonObject(element.value<Note*>());
    }
    json.setArray(notesArray);
    return json.toJson();
}

QVariantList PlayGridManager::jsonToNotesList(const QString& json)
{
    QVariantList notes;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json.toUtf8());
    if (jsonDoc.isArray()) {
        QJsonArray notesArray = jsonDoc.array();
        for (const QJsonValue &note : notesArray) {
            notes << QVariant::fromValue<QObject*>(jsonObjectToNote(note.toObject()));
        }
    }
    return notes;
}

QString PlayGridManager::noteToJson(QObject* note) const
{
    QJsonDocument doc;
    doc.setObject(noteToJsonObject(qobject_cast<Note*>(note)));
    return doc.toJson();
}

QObject* PlayGridManager::jsonToNote(const QString& json)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json.toUtf8());
    return jsonObjectToNote(jsonDoc.object());
}

void PlayGridManager::setNotesOn(QVariantList notes, QVariantList velocities)
{
    if (notes.count() == velocities.count()) {
        for (int i = 0; i < notes.count(); ++i) {
            setNoteState(notes.at(i).value<Note*>(), velocities.at(i).toInt(), true);
        }
    }
}

void PlayGridManager::setNotesOff(QVariantList notes)
{
    for (int i = 0; i < notes.count(); ++i) {
        setNoteState(notes.at(i).value<Note*>(), 0, false);
    }
}
void PlayGridManager::setNoteOn(QObject* note, int velocity)
{
    setNoteState(qobject_cast<Note*>(note), velocity, true);
}

void PlayGridManager::setNoteOff(QObject* note)
{
    setNoteState(qobject_cast<Note*>(note), 0, false);
}

void PlayGridManager::setNoteState(Note* note, int velocity, bool setOn)
{
    if (note) {
        QObjectList subnotes;
        const QVariantList tempSubnotes = note->subnotes();
        for (const QVariant &tempSubnote : tempSubnotes) {
            subnotes << tempSubnote.value<QObject*>();
        }
        int subnoteCount = subnotes.count();
        if (subnoteCount > 0) {
            for (QObject *note : subnotes) {
                setNoteState(qobject_cast<Note*>(note), velocity, setOn);
            }
        } else {
            if (d->noteStateMap.contains(note)) {
                if (setOn) {
                    d->noteStateMap[note] = d->noteStateMap[note] + 1;
                } else {
                    d->noteStateMap[note] = d->noteStateMap[note] - 1;
                    if (d->noteStateMap[note] == 0) {
                        note->setOff();
                        d->noteStateMap.remove(note);
                    }
                }
            } else {
                if (setOn) {
                    note->setOn(velocity);
                    d->noteStateMap[note] = 1;
                } else {
                    note->setOff();
                }
            }
        }
    } else {
        qDebug() << "Attempted to set the state of a null-value note";
    }
}

QVariantList PlayGridManager::mostRecentlyChangedNotes() const
{
    return d->mostRecentlyChangedNotes;
}

void PlayGridManager::updateNoteState(QVariantMap metadata)
{
    static const QLatin1String note_on{"note_on"};
    static const QLatin1String note_off{"note_off"};
    int midiNote = metadata.value("note").toInt();
    int midiChannel = metadata.value("channel").toInt();
    const QString messageType = metadata.value("type").toString();
    if (messageType == note_on) {
        Note *note = d->findExistingNote(midiNote, midiChannel);
        if (note) {
            note->setIsPlaying(true);
            Q_EMIT noteStateChanged(note);
        }
    } else if (messageType == note_off) {
        Note *note = d->findExistingNote(midiNote, midiChannel);
        if (note) {
            note->setIsPlaying(false);
            Q_EMIT noteStateChanged(note);
        }
    }
    d->mostRecentlyChangedNotes << metadata;
    Q_EMIT mostRecentlyChangedNotesChanged();
}

void PlayGridManager::setCurrentMidiChannel(int midiChannel)
{
    if (d->currentMidiChannel != midiChannel) {
        d->currentMidiChannel = midiChannel;
        Q_EMIT currentMidiChannelChanged();
    }
}

int PlayGridManager::currentMidiChannel() const
{
    return d->currentMidiChannel;
}

void PlayGridManager::scheduleNote(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity, quint64 duration, quint64 delay)
{
    if (d->syncTimer && midiChannel >= 0 && midiChannel <= 15) {
        d->syncTimer->scheduleNote(midiNote, midiChannel, setOn, velocity, duration, delay);
    }
}

void PlayGridManager::metronomeTick(int beat)
{
    d->metronomeBeat128th = beat;
    Q_EMIT metronomeBeat128thChanged();
    if (beat % 2 == 0) {
        d->metronomeBeat64th = beat / 2;
        Q_EMIT metronomeBeat64thChanged();
    }
    if (beat % 4 == 0) {
        d->metronomeBeat32nd = beat / 4;
        Q_EMIT metronomeBeat32ndChanged();
    }
    if (beat % 8 == 0) {
        d->metronomeBeat16th = beat / 8;
        Q_EMIT metronomeBeat16thChanged();
    }
    if (beat % 16 == 0) {
        d->metronomeBeat8th = beat / 16;
        Q_EMIT metronomeBeat8thChanged();
    }
    if (beat % 32 == 0) {
        d->metronomeBeat4th = beat / 32;
        Q_EMIT metronomeBeat4thChanged();
    }
}

int PlayGridManager::metronomeBeat4th() const
{
    return d->metronomeBeat4th;
}

int PlayGridManager::metronomeBeat8th() const
{
    return d->metronomeBeat8th;
}

int PlayGridManager::metronomeBeat16th() const
{
    return d->metronomeBeat16th;
}

int PlayGridManager::metronomeBeat32nd() const
{
    return d->metronomeBeat32nd;
}

int PlayGridManager::metronomeBeat64th() const
{
    return d->metronomeBeat64th;
}

int PlayGridManager::metronomeBeat128th() const
{
    return d->metronomeBeat128th;
}

void PlayGridManager::setSyncTimer(QObject* syncTimer)
{
    if (d->syncTimer != syncTimer) {
        if (d->syncTimer) {
            d->syncTimer->removeCallback(&timer_callback);
            d->syncTimer->disconnect(this);
        }
        d->syncTimer = qobject_cast<SyncTimer*>(syncTimer);
        if (d->syncTimer) {
            d->syncTimer->addCallback(&timer_callback);
            connect(d->syncTimer, &SyncTimer::timerRunningChanged, this, &PlayGridManager::metronomeActiveChanged);
        }
        Q_EMIT syncTimerChanged();
    }
}

QObject* PlayGridManager::syncTimer()
{
    return d->syncTimer;
}

void hookUpAndMaybeStartTimer(PlayGridManager* pgm, bool startTimer = false)
{
    // If we've already registered ourselves to get a callback, don't do that again, it just gets silly
    if (!timer_callback_tickers->contains(pgm)) {
        // TODO Send start metronome request to libzl directly
        timer_callback_tickers->append(pgm);
    }
    if (startTimer) {
        Q_EMIT pgm->requestMetronomeStart();
    }
}

void PlayGridManager::hookUpTimer()
{
    hookUpAndMaybeStartTimer(this);
}

void PlayGridManager::startMetronome()
{
    hookUpAndMaybeStartTimer(this, true);
}

void PlayGridManager::stopMetronome()
{
    // TODO Send stop metronome request to libzl
    timer_callback_tickers->removeAll(this);
    Q_EMIT requestMetronomeStop();
    QMetaObject::invokeMethod(this, "metronomeActiveChanged", Qt::QueuedConnection);
    d->metronomeBeat4th = 0;
    d->metronomeBeat8th = 0;
    d->metronomeBeat16th = 0;
    d->metronomeBeat32nd = 0;
    d->metronomeBeat64th = 0;
    d->metronomeBeat128th = 0;
    Q_EMIT metronomeBeat4thChanged();
    Q_EMIT metronomeBeat8thChanged();
    Q_EMIT metronomeBeat16thChanged();
    Q_EMIT metronomeBeat32ndChanged();
    Q_EMIT metronomeBeat64thChanged();
    Q_EMIT metronomeBeat128thChanged();
}

bool PlayGridManager::metronomeActive() const
{
    if (d->syncTimer) {
        return d->syncTimer->timerRunning();
    }
    return false;
}

void PlayGridManager::sendAMidiNoteMessage(unsigned char midiNote, unsigned char velocity, unsigned char channel, bool setOn)
{
    if (!d->midiout) {
        d->ensureMidiOutput();
    }
    if (d->midiout && channel >= 0 && channel <= 15) {
        if (setOn) {
            d->midiMessage[0] = 0x90 + channel;
        } else {
            d->midiMessage[0] = 0x80 + channel;
        }
        d->midiMessage[1] = midiNote;
        d->midiMessage[2] = velocity;
        d->midiout->sendMessage(&d->midiMessage);
    }
}

QObject *PlayGridManager::getClipById(int clipID) const
{
    QObject *clip{ClipAudioSource_byID(clipID)};
    if (clip) {
        QQmlEngine::setObjectOwnership(clip, QQmlEngine::CppOwnership);
    }
    return clip;
}
