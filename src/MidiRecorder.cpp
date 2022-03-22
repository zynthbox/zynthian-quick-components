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

#include "MidiRecorder.h"

#include "PlayGridManager.h"
#include "PatternModel.h"

#include <chrono>

// Hackety hack - we don't need all the thing, just need some storage things (MidiBuffer and MidiNote specifically)
#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1
#include <juce_audio_formats/juce_audio_formats.h>

using frame_clock = std::chrono::steady_clock;

class MidiRecorderPrivate {
public:
    MidiRecorderPrivate() {}
    bool isRecording{false};
    QList<int> channels;
    juce::MidiMessageSequence midiMessageSequence;
    frame_clock::time_point mostRecentEventTime;
    void handleMidiMessage(const unsigned char& byte1, const unsigned char& byte2, const unsigned char& byte3) {
        if (isRecording) {
            if (0x7F < byte1 && byte1 < 0xA0) {
                const std::chrono::duration<double, std::micro> timestamp = frame_clock::now() - mostRecentEventTime;
                // We're using decimal seconds with microsecond precision as timestamp intervals (since that's the
                // precision midi operates at, but our time-to-beat calculator functions are double precision seconds)
                juce::MidiMessage message(byte1, byte2, byte3, (double)timestamp.count() / (double)1000000);
                // Always remember, juce thinks channels are 1-indexed
                if (channels.contains(message.getChannel() - 1)) {
                    midiMessageSequence.addEvent(message);
                    mostRecentEventTime = frame_clock::now();
                }
            }
        }
    }
};

MidiRecorder::MidiRecorder(QObject *parent)
    : QObject(parent)
    , d(new MidiRecorderPrivate)
{
    connect(PlayGridManager::instance(), &PlayGridManager::midiMessage, this, [this](const unsigned char& byte1, const unsigned char& byte2, const unsigned char& byte3){ d->handleMidiMessage(byte1, byte2, byte3); });
}

MidiRecorder::~MidiRecorder() = default;

void MidiRecorder::startRecording(int channel, bool clear)
{
    if (clear) {
        clearRecording();
    }
    d->channels << channel;
    if (!d->isRecording) {
        d->mostRecentEventTime = frame_clock::now();
        d->isRecording = true;
    }
}

void MidiRecorder::stopRecording(int channel)
{
    if (channel == -1) {
        d->channels.clear();
    } else {
        d->channels.removeAll(channel);
    }
    if (d->channels.isEmpty()) {
        d->isRecording = false;
    }
}

void MidiRecorder::clearRecording()
{
    d->channels.clear();
    d->midiMessageSequence.clear();
}

bool MidiRecorder::loadFromMidi(const QByteArray &midiData)
{
    bool success{false};

    juce::MemoryInputStream in(midiData.data(), midiData.size(), false);
    juce::MidiFile file;
    if (file.readFrom(in, true)) {
        if (file.getNumTracks() > 0) {
            d->midiMessageSequence = juce::MidiMessageSequence(*file.getTrack(0));
            success = true;
        }
    }

    return success;
}

QByteArray MidiRecorder::midi() const
{
    QByteArray data;

    juce::MidiFile file;
    file.addTrack(d->midiMessageSequence);

    juce::MemoryOutputStream out;
    if (file.writeTo(out)) {
        out.flush();

        juce::MemoryBlock block = out.getMemoryBlock();
        data.reserve(out.getDataSize());
        for (const char *i = block.begin(); i != block.end(); ++i) {
            data.append(i);
        }
    }
    return data;
}

bool MidiRecorder::loadFromBase64Midi(const QString &data)
{
    return loadFromMidi(QByteArray::fromBase64(data.toUtf8()));
}

QString MidiRecorder::base64Midi() const
{
    return midi().toBase64();
}

bool MidiRecorder::loadFromAscii(const QString &/*asciiRepresentation*/)
{
    bool success{false};
    qWarning() << Q_FUNC_INFO << "NO ACTION TAKEN - UNIMPLEMENTED!";
    return success;
}

QString MidiRecorder::ascii() const
{
    QString data;
    qWarning() << Q_FUNC_INFO << "NO ACTION TAKEN - UNIMPLEMENTED!";
    return data;
}

bool MidiRecorder::applyToPattern(PatternModel *patternModel, QFlags<MidiRecorder::ApplicatorSetting> settings) const
{
    bool success{false};
    QList<int> acceptChannel;
    if (settings.testFlag(ClearPatternBeforeApplying)) {
        patternModel->clear();
    }
    if (settings.testFlag(LimitToPatternChannel)) {
        acceptChannel << patternModel->midiChannel();
    } else {
        if (settings.testFlag(ApplyChannel0)) {
            acceptChannel << 0;
        }
        if (settings.testFlag(ApplyChannel1)) {
            acceptChannel << 1;
        }
        if (settings.testFlag(ApplyChannel2)) {
            acceptChannel << 2;
        }
        if (settings.testFlag(ApplyChannel3)) {
            acceptChannel << 3;
        }
        if (settings.testFlag(ApplyChannel4)) {
            acceptChannel << 4;
        }
        if (settings.testFlag(ApplyChannel5)) {
            acceptChannel << 5;
        }
        if (settings.testFlag(ApplyChannel6)) {
            acceptChannel << 6;
        }
        if (settings.testFlag(ApplyChannel7)) {
            acceptChannel << 7;
        }
        if (settings.testFlag(ApplyChannel8)) {
            acceptChannel << 8;
        }
        if (settings.testFlag(ApplyChannel9)) {
            acceptChannel << 9;
        }
        if (settings.testFlag(ApplyChannel10)) {
            acceptChannel << 10;
        }
        if (settings.testFlag(ApplyChannel11)) {
            acceptChannel << 11;
        }
        if (settings.testFlag(ApplyChannel12)) {
            acceptChannel << 12;
        }
        if (settings.testFlag(ApplyChannel13)) {
            acceptChannel << 13;
        }
        if (settings.testFlag(ApplyChannel14)) {
            acceptChannel << 14;
        }
        if (settings.testFlag(ApplyChannel15)) {
            acceptChannel << 15;
        }
    }
    qWarning() << Q_FUNC_INFO << "NO ACTION TAKEN - UNIMPLEMENTED!";
    return success;
}
