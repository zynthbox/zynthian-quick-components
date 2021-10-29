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

#include <RtMidi.h>

class MidiListener : public QThread {
    Q_OBJECT
public:
    explicit MidiListener(int rtMidiInPort)
        : QThread()
        , midiInPort(rtMidiInPort)
    {};
    void run() {
        RtMidiIn *midiin = new RtMidiIn(RtMidi::UNIX_JACK);
        std::vector<unsigned char> message;
        int nBytes, i;
        double stamp;
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
            // Don't ignore sysex, timing, or active sensing messages.
            midiin->ignoreTypes( false, false, false );
            // Periodically check input queue.
            bool setOn{false};
            int midiNote{0};
            int midiChannel{0};
            int velocity{0};
            while ( !done ) {
                stamp = midiin->getMessage( &message );
                nBytes = message.size();
                midiNote = -1;
                if ( nBytes > 0 ) {
                    switch (message[0]) {
                        case 0x90:
                            setOn = true;
                            midiChannel = 0;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x91:
                            setOn = true;
                            midiChannel = 1;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x92:
                            setOn = true;
                            midiChannel = 2;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x93:
                            setOn = true;
                            midiChannel = 3;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x94:
                            setOn = true;
                            midiChannel = 4;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x95:
                            setOn = true;
                            midiChannel = 5;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x96:
                            setOn = true;
                            midiChannel = 6;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x97:
                            setOn = true;
                            midiChannel = 7;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x98:
                            setOn = true;
                            midiChannel = 8;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x99:
                            setOn = true;
                            midiChannel = 9;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x9A:
                            setOn = true;
                            midiChannel = 10;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x9B:
                            setOn = true;
                            midiChannel = 11;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x9C:
                            setOn = true;
                            midiChannel = 12;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x9D:
                            setOn = true;
                            midiChannel = 13;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x9E:
                            setOn = true;
                            midiChannel = 14;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x9F:
                            setOn = true;
                            midiChannel = 15;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x80:
                            setOn = false;
                            midiChannel = 0;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x81:
                            setOn = false;
                            midiChannel = 1;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x82:
                            setOn = false;
                            midiChannel = 2;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x83:
                            setOn = false;
                            midiChannel = 3;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x84:
                            setOn = false;
                            midiChannel = 4;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x85:
                            setOn = false;
                            midiChannel = 5;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x86:
                            setOn = false;
                            midiChannel = 6;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x87:
                            setOn = false;
                            midiChannel = 7;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x88:
                            setOn = false;
                            midiChannel = 8;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x89:
                            setOn = false;
                            midiChannel = 9;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x8A:
                            setOn = false;
                            midiChannel = 10;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x8B:
                            setOn = false;
                            midiChannel = 11;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x8C:
                            setOn = false;
                            midiChannel = 12;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x8D:
                            setOn = false;
                            midiChannel = 13;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x8E:
                            setOn = false;
                            midiChannel = 14;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        case 0x8F:
                            setOn = false;
                            midiChannel = 15;
                            midiNote = message[1];
                            velocity = message[2];
                            break;
                        default:
                            // unused
                            break;
                    }
                    if (midiNote > -1) {
                        Q_EMIT noteChanged(midiNote, midiChannel, setOn);
                    } else {
                        // Spit out the unknown thing onto the cli - this should come in handy later on
                        for ( i=0; i<nBytes; i++ ) {
                            std::cout << "Byte " << i << " = " << (int)message[i] << ", ";
                        }
                        std::cout << "stamp = " << stamp << std::endl;
                    }
                } else {
                    // Sleep for 10 milliseconds - don't sleep unless the queue was empty
                    msleep(10);
                }
            }
        }
        if (midiin) {
            delete midiin;
        }
    }
    Q_SLOT void markAsDone() {
        done = true;
    }
    Q_SIGNAL void noteChanged(int midiNote, int midiChannel, bool setOn);
private:
    bool done{false};
    int midiInPort{-1};
};

#endif//MIDILISTENER_H
