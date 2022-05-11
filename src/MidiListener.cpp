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

#include "MidiListener.h"

#include <QDebug>

void handleRtMidiMessage(double, std::vector< unsigned char >*, void*);

#define MAX_MESSAGES 1000

MidiListener::MidiListener(int rtMidiInPort)
    : QThread()
    , midiInPort(rtMidiInPort)
{
    for (int i = 0; i < MAX_MESSAGES; ++i) {
        messages << new NoteMessage();
    }
    midiin = new RtMidiIn(RtMidi::UNIX_JACK, "Midi Listener Client");
    std::vector<unsigned char> message;
    std::string portName;
    try {
        portName = midiin->getPortName(midiInPort);
        std::cout << "Using input port " << midiInPort << " named " << portName << std::endl;
        midiin->openPort(midiInPort);
    }
    catch (RtMidiError &error) {
        error.printMessage();
        delete midiin;
        midiin = nullptr;
    }
    if (midiin) {
        midiin->setCallback(&handleRtMidiMessage, this);
        // Don't ignore sysex, timing, or active sensing messages.
        midiin->ignoreTypes( false, false, false );
    }
}

MidiListener::~MidiListener() {
    if (midiin) {
        delete midiin;
    }
    qDeleteAll(messages);
    messages.clear();
}

void MidiListener::run() {
    while (true) {
        if (done) {
            break;
        }
        if (lastRelevantMessage > -1) {
            int i{0};
            for (NoteMessage *message : messages) {
                if (i > lastRelevantMessage || i >= MAX_MESSAGES) {
                    break;
                }
                Q_EMIT noteChanged(message->midiNote, message->midiChannel, message->velocity, message->setOn, message->byte1, message->byte2, message->byte3);
                ++i;
            }
            lastRelevantMessage = -1;
        }
        msleep(5);
    }
}

void MidiListener::markAsDone() {
    done = true;
}

void MidiListener::addMessage(int midiNote, int midiChannel, int velocity, bool setOn, std::vector< unsigned char > *data)
{
    if (lastRelevantMessage >= MAX_MESSAGES) {
        qWarning() << "Too many messages in a single run before we could report back - we only expected" << MAX_MESSAGES;
    } else {
        ++lastRelevantMessage;
        NoteMessage* message = messages.at(lastRelevantMessage);
        message->midiNote = midiNote;
        message->midiChannel = midiChannel;
        message->velocity = velocity;
        message->setOn = setOn;
        message->byte1 = data->at(0);
        if (data->size() > 1) {
            message->byte2 = data->at(1);
        }
        if (data->size() > 2) {
            message->byte3 = data->at(2);
        }
    }
}

void handleRtMidiMessage(double /*timeStamp*/, std::vector< unsigned char > *message, void *userData) {
    MidiListener* listener = static_cast<MidiListener*>(userData);
    if ( message->size() > 0 ) {
        const unsigned char &byte1 = message->at(0);
        if (byte1 >= 0x80 && byte1 < 0xA0) {
            const bool setOn = (byte1 >= 0x90);
            const int midiChannel = byte1 - (setOn ? 0x90 : 0x80);
            const int &midiNote = message->at(1);
            const int &velocity = message->at(2);
            listener->addMessage(midiNote, midiChannel, velocity, setOn, message);
        } else {
//            // Spit out the unknown thing onto the cli - this should come in handy later on
//            for ( i=0; i<nBytes; i++ ) {
//                std::cout << "Byte " << i << " = " << (int)message[i] << ", ";
//            }
//            std::cout << "stamp = " << timeStamp << std::endl;
        }
    }
}
