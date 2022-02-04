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
    midiin = new RtMidiIn(RtMidi::UNIX_JACK);
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
                Q_EMIT noteChanged(message->midiNote, message->midiChannel, message->velocity, message->setOn);
                ++i;
            }
            lastRelevantMessage = -1;
        }
        msleep(10);
    }
}

void MidiListener::markAsDone() {
    done = true;
}

void MidiListener::addMessage(int midiNote, int midiChannel, int velocity, bool setOn)
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
    }
}

void handleRtMidiMessage(double /*timeStamp*/, std::vector< unsigned char > *message, void *userData) {
    MidiListener* listener = static_cast<MidiListener*>(userData);
    // Periodically check input queue.
    int nBytes;
    bool setOn{false};
    int midiNote{0};
    int midiChannel{0};
    int velocity{0};
    nBytes = message->size();
    midiNote = -1;
    if ( nBytes > 0 ) {
        switch (message->at(0)) {
            case 0x90:
                setOn = true;
                midiChannel = 0;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x91:
                setOn = true;
                midiChannel = 1;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x92:
                setOn = true;
                midiChannel = 2;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x93:
                setOn = true;
                midiChannel = 3;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x94:
                setOn = true;
                midiChannel = 4;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x95:
                setOn = true;
                midiChannel = 5;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x96:
                setOn = true;
                midiChannel = 6;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x97:
                setOn = true;
                midiChannel = 7;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x98:
                setOn = true;
                midiChannel = 8;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x99:
                setOn = true;
                midiChannel = 9;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x9A:
                setOn = true;
                midiChannel = 10;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x9B:
                setOn = true;
                midiChannel = 11;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x9C:
                setOn = true;
                midiChannel = 12;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x9D:
                setOn = true;
                midiChannel = 13;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x9E:
                setOn = true;
                midiChannel = 14;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x9F:
                setOn = true;
                midiChannel = 15;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x80:
                setOn = false;
                midiChannel = 0;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x81:
                setOn = false;
                midiChannel = 1;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x82:
                setOn = false;
                midiChannel = 2;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x83:
                setOn = false;
                midiChannel = 3;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x84:
                setOn = false;
                midiChannel = 4;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x85:
                setOn = false;
                midiChannel = 5;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x86:
                setOn = false;
                midiChannel = 6;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x87:
                setOn = false;
                midiChannel = 7;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x88:
                setOn = false;
                midiChannel = 8;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x89:
                setOn = false;
                midiChannel = 9;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x8A:
                setOn = false;
                midiChannel = 10;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x8B:
                setOn = false;
                midiChannel = 11;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x8C:
                setOn = false;
                midiChannel = 12;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x8D:
                setOn = false;
                midiChannel = 13;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x8E:
                setOn = false;
                midiChannel = 14;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            case 0x8F:
                setOn = false;
                midiChannel = 15;
                midiNote = message->at(1);
                velocity = message->at(2);
                break;
            default:
                // unused
                break;
        }
        if (midiNote > -1) {
            listener->addMessage(midiNote, midiChannel, velocity, setOn);
        } else {
//            // Spit out the unknown thing onto the cli - this should come in handy later on
//            for ( i=0; i<nBytes; i++ ) {
//                std::cout << "Byte " << i << " = " << (int)message[i] << ", ";
//            }
//            std::cout << "stamp = " << timeStamp << std::endl;
        }
    }
}
