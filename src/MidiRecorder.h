/*
 * Copyright (C) 2022 Dan Leinir Turthra Jensen <admin@leinir.dk>
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

#ifndef MIDIRECORDER_H
#define MIDIRECORDER_H

#include <QObject>
#include <QCoreApplication>
#include <QFlags>
#include <memory>

class PatternModel;
class MidiRecorderPrivate;
/**
 * \brief A singleton class for recording midi, optionally applying this to PatternModels, and loading from/ saving to midi files
 */
class MidiRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged)
public:
    static MidiRecorder* instance() {
        static MidiRecorder* instance{nullptr};
        if (!instance) {
            instance = new MidiRecorder(qApp);
        }
        return instance;
    };
    explicit MidiRecorder(QObject *parent = nullptr);
    ~MidiRecorder() override;

    /**
     * \brief Start recording
     * @param channel The midi channel to start recording on
     * @param clear Whether or not to clear the current recording before starting the recording (the same as stopping, clearing, and starting)
     */
    Q_INVOKABLE void startRecording(int channel, bool clear = false);
    /**
     * \brief Stop recording
     * @param channel The midi channel you want to stop recording from (if -1, all recording is stopped)
     */
    Q_INVOKABLE void stopRecording(int channel = -1);
    /**
     * \brief Clears any previously recorded data
     * Clearing will also reset the timestamp. Any events recorded during the next recording session will be started at time 0
     */
    Q_INVOKABLE void clearRecording();

    /**
     * \brief Clears the current recording and replaces it with track 0 from the midi file contained in the given data
     * @param midiData The binary contents of a midi file
     * @return True if successfully loaded, false if not
     */
    Q_INVOKABLE bool loadFromMidi(const QByteArray &midiData);
    /**
     * \brief A midi file containing the currently recorded midi data in a single track of a type 1 midi file
     * @return The rendered midi file (or empty if the process failed)
     */
    Q_INVOKABLE QByteArray midi() const;
    /**
     * \brief Convenience function to return a base64 encoded version of the data retrieved by the midi() function
     * @return The base64 data, or an empty string if unsuccessful
     */
    Q_INVOKABLE QString base64Midi() const;
    /**
     * \brief Convenience function to load from a base64 encoded version of a midi file using the loadFromMidi() function
     * @param data The base64 representation of the midi file you wish to load
     * @return True if successfully loaded, false if not
     */
    Q_INVOKABLE bool loadFromBase64Midi (const QString& data);

    /**
     * \brief Force all recorded notes onto the given channel
     * Prior to playing a recording, you may need to move the notes onto a different channel,
     * so they play on the correct instrument
     * @param channel The channel all notes should be forced onto
     */
    Q_INVOKABLE void forceToChannel(int channel);

    /**
     * \brief Play the midi contained in the recorder from start to end and then stop
     */
    Q_INVOKABLE void playRecording();
    /**
     * \brief Stops playback if it is currently running
     */
    Q_INVOKABLE void stopPlayback();

    // TODO This should probably use a "proper" ascii midi representation, perhaps asc2mid from http://www.archduke.org/midi/ ?
    Q_INVOKABLE bool loadFromAscii(const QString &asciiRepresentation);
    Q_INVOKABLE QString ascii() const;

    enum ApplicatorSetting {
        NoFlags = 0x0,
        LimitToPatternChannel = 0x1,
        ClearPatternBeforeApplying = 0x2,
        ApplyChannel0 = 0x4,
        ApplyChannel1 = 0x8,
        ApplyChannel2 = 0x16,
        ApplyChannel3 = 0x32,
        ApplyChannel4 = 0x64,
        ApplyChannel5 = 0x128,
        ApplyChannel6 = 0x256,
        ApplyChannel7 = 0x512,
        ApplyChannel8 = 0x1024,
        ApplyChannel9 = 0x2048,
        ApplyChannel10 = 0x4096,
        ApplyChannel11 = 0x8192,
        ApplyChannel12 = 0x16384,
        ApplyChannel13 = 0x32768,
        ApplyChannel14 = 0x65536,
        ApplyChannel15 = 0x131072,
        ApplyAllChannelsToPattern = ApplyChannel0 | ApplyChannel1 | ApplyChannel2 | ApplyChannel3 | ApplyChannel4 | ApplyChannel5 | ApplyChannel6 | ApplyChannel7 | ApplyChannel8 | ApplyChannel9 | ApplyChannel10 | ApplyChannel11 | ApplyChannel12 | ApplyChannel13 | ApplyChannel14 | ApplyChannel15,
    };
    Q_DECLARE_FLAGS(ApplicatorSettings, ApplicatorSetting)
    Q_FLAG(ApplicatorSettings)
    /**
     * \brief Apply what is contained in the recorder to a pattern
     * @param PatternModel The model to apply the recorder's midi data to
     * @param settings A set of flags you can use to control the behaviour of the function
     */
    Q_INVOKABLE bool applyToPattern(PatternModel *patternModel, QFlags<ApplicatorSetting> settings = NoFlags) const;

    bool isPlaying() const;
    Q_SIGNAL void isPlayingChanged();
    bool isRecording() const;
    Q_SIGNAL void isRecordingChanged();
private:
    std::unique_ptr<MidiRecorderPrivate> d;
};
Q_DECLARE_OPERATORS_FOR_FLAGS(MidiRecorder::ApplicatorSettings)

#endif//MIDIRECORDER_H
