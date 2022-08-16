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

#ifndef MIDILISTENER_H
#define MIDILISTENER_H

#include <QThread>
#include <jack/midiport.h>

void handleRtMidiMessage(double, std::vector< unsigned char >*, void*);

struct NoteMessage;
class MidiListenerPrivate;
class MidiListener : public QThread {
    Q_OBJECT
public:
    explicit MidiListener(QObject *parent = nullptr);
    ~MidiListener() override;

    enum Port {
        UnknownPort = -1,
        PassthroughPort = 0,
        InternalPassthroughPort = 1,
        HardwareInPassthrough = 2,
        ExternalOutPort = 3,
    };
    Q_ENUM(Port)

    void run() override;
    Q_SLOT void markAsDone();
    Q_SIGNAL void noteChanged(Port port, int midiNote, int midiChannel, int velocity, bool setOn, double timeStamp, const unsigned char &byte1, const unsigned char &byte2, const unsigned char &byte3);
    void addMessage(Port port, double timeStamp, int midiNote, int midiChannel, int velocity, bool setOn, const jack_midi_event_t &event);
private:
    MidiListenerPrivate *d{nullptr};
};

#endif//MIDILISTENER_H
