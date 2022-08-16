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

#include <libzl.h>
#include <SyncTimer.h>

#include <jack/jack.h>

#include <QDebug>

#define MAX_MESSAGES 1000

#define DebugMidiListener false

struct NoteMessage {
    MidiListener::Port port;
    bool setOn{false};
    int midiNote{0};
    int midiChannel{0};
    int velocity{0};
    double timeStamp{0};
    unsigned char byte1{0};
    unsigned char byte2{0};
    unsigned char byte3{0};
};

struct MidiListenerPort {
    ~MidiListenerPort() {
        qDeleteAll(messages);
        messages.clear();
    }
    jack_port_t *port{nullptr};
    MidiListener::Port identifier{MidiListener::UnknownPort};
    int lastRelevantMessage{-1};
    int waitTime{5};
    QList<NoteMessage*> messages;
};

class MidiListenerPrivate {
public:
    MidiListenerPrivate(MidiListener *q)
        : q(q)
    {
        syncTimer = qobject_cast<SyncTimer*>(SyncTimer_instance());
    }
    ~MidiListenerPrivate() {
        if (jackClient) {
            jack_client_close(jackClient);
        }
        qDeleteAll(ports);
    }
    MidiListener *q{nullptr};
    bool done{false};
    SyncTimer *syncTimer{nullptr};
    jack_client_t *jackClient{nullptr};
    QList<MidiListenerPort*> ports;

    void connectPorts(const QString &from, const QString &to) {
        int result = jack_connect(jackClient, from.toUtf8(), to.toUtf8());
        if (result == 0 || result == EEXIST) {
            if (DebugMidiListener) { qDebug() << "MidiListener:" << (result == EEXIST ? "Retaining existing connection from" : "Successfully created new connection from" ) << from << "to" << to; }
        } else {
            qWarning() << "MidiListener: Failed to connect" << from << "with" << to << "with error code" << result;
            // This should probably reschedule an attempt in the near future, with a limit to how long we're trying for?
        }
    }

    int process(jack_nframes_t nframes) {
        jack_nframes_t current_frames;
        jack_time_t current_usecs;
        jack_time_t next_usecs;
        float period_usecs;
        jack_get_cycle_times(jackClient, &current_frames, &current_usecs, &next_usecs, &period_usecs);
        const quint64 microsecondsPerFrame = (next_usecs - current_usecs) / nframes;
        const int subbeatLengthInMicroseconds = syncTimer->jackSubbeatLengthInMicroseconds();
        // Actual playhead (or as close as we're going to reasonably get, let's not get too crazy here)
        const int currentJackPlayhead = syncTimer->jackPlayhead() - (period_usecs / subbeatLengthInMicroseconds);

        for (MidiListenerPort *listenerPort : qAsConst(ports)) {
            void *inputBuffer = jack_port_get_buffer(listenerPort->port, nframes);
            uint32_t events = jack_midi_get_event_count(inputBuffer);
            jack_midi_event_t event;
            for (uint32_t eventIndex = 0; eventIndex < events; ++eventIndex) {
                if (jack_midi_event_get(&event, inputBuffer, eventIndex)) {
                    qWarning() << "MidiListener: jack_midi_event_get failed, received note lost!";
                    continue;
                }
                if ((event.buffer[0] & 0xf0) == 0xf0) {
                    continue;
                }
                const unsigned char &byte1 = event.buffer[0];
                if (byte1 >= 0x80 && byte1 < 0xA0) {
                    const bool setOn = (byte1 >= 0x90);
                    const int midiChannel = byte1 - (setOn ? 0x90 : 0x80);
                    const int &midiNote = event.buffer[1];
                    const int &velocity = event.buffer[2];
                    q->addMessage(listenerPort->identifier, currentJackPlayhead + (event.time * microsecondsPerFrame / subbeatLengthInMicroseconds), midiNote, midiChannel, velocity, setOn, event);
                } else {
//                    // Spit out the unknown thing onto the cli - this should come in handy later on
//                    for (int i=0; i<message->size(); i++ ) {
//                        std::cout << "Byte " << i << " = " << (int)event.buffer[i) << ", ";
//                    }
//                    std::cout << "stamp = " << timeStamp << std::endl;
                }
            }
            jack_midi_clear_buffer(inputBuffer);
        }

        return 0;
    }
    int xrun() {
        return 0;
    }
};

static int client_process(jack_nframes_t nframes, void* arg) {
    return static_cast<MidiListenerPrivate*>(arg)->process(nframes);
}
static int client_xrun(void* arg) {
    return static_cast<MidiListenerPrivate*>(arg)->xrun();
}

MidiListener::MidiListener(QObject *parent)
    : QThread(parent)
    , d(new MidiListenerPrivate(this))
{
    static int portTypeId = -1;
    if (portTypeId < 0) { portTypeId = qRegisterMetaType<MidiListener::Port>(); }
    jack_status_t real_jack_status{};
    d->jackClient = jack_client_open("MidiListener", JackNullOption, &real_jack_status);
    if (d->jackClient) {
        // Register the MIDI output port.
        MidiListenerPort *listenerPort{new MidiListenerPort};
        listenerPort->port = jack_port_register(d->jackClient, "PassthroughIn", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        listenerPort->identifier = MidiListener::PassthroughPort;
        listenerPort->waitTime = 0;
        for (int i = 0; i < MAX_MESSAGES; ++i) { listenerPort->messages << new NoteMessage(); }
        d->ports << listenerPort;
        listenerPort = new MidiListenerPort;
        listenerPort->port = jack_port_register(d->jackClient, "InternalPassthroughIn", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        listenerPort->identifier = MidiListener::InternalPassthroughPort;
        listenerPort->waitTime = 5;
        for (int i = 0; i < MAX_MESSAGES; ++i) { listenerPort->messages << new NoteMessage(); }
        d->ports << listenerPort;
        listenerPort = new MidiListenerPort;
        listenerPort->port = jack_port_register(d->jackClient, "HardwareInPassthroughIn", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        listenerPort->identifier = MidiListener::HardwareInPassthrough;
        listenerPort->waitTime = 5;
        for (int i = 0; i < MAX_MESSAGES; ++i) { listenerPort->messages << new NoteMessage(); }
        d->ports << listenerPort;
        listenerPort = new MidiListenerPort;
        listenerPort->port = jack_port_register(d->jackClient, "ExternalOutIn", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        listenerPort->identifier = MidiListener::ExternalOutPort;
        listenerPort->waitTime = 5;
        for (int i = 0; i < MAX_MESSAGES; ++i) { listenerPort->messages << new NoteMessage(); }
        d->ports << listenerPort;
        if (d->ports[0]->port && d->ports[1]->port && d->ports[2]->port && d->ports[3]->port) {
            // Set the process callback.
            if (jack_set_process_callback(d->jackClient, client_process, static_cast<void*>(d)) != 0) {
                qWarning() << "MidiListener: Failed to set the MidiListener Jack processing callback";
            } else {
                jack_set_xrun_callback(d->jackClient, client_xrun, static_cast<void*>(d));
                // Activate the client.
                if (jack_activate(d->jackClient) == 0) {
                    d->connectPorts(QLatin1String{"ZLRouter:Passthrough"}, QLatin1String{"MidiListener:PassthroughIn"});
                    d->connectPorts(QLatin1String{"ZLRouter:InternalPassthrough"}, QLatin1String{"MidiListener:InternalPassthroughIn"});
                    d->connectPorts(QLatin1String{"ZLRouter:HardwareInPassthrough"}, QLatin1String{"MidiListener:HardwareInPassthroughIn"});
                    d->connectPorts(QLatin1String{"ZLRouter:ExternalOut"}, QLatin1String{"MidiListener:ExternalOutIn"});
                    qDebug() << "MidiListener: Successfully created and set up the MidiListener's Jack client";
                } else {
                    qWarning() << "MidiListener: Failed to activate MidiListener Jack client";
                }
            }
        } else {
            qWarning() << "MidiListener: Could not register MidiListener Jack input port for internal messages";
        }
    } else {
        qWarning() << "MidiListener: Could not create the MidiListener Jack client.";
    }
}

MidiListener::~MidiListener() {
    delete d;
}

void MidiListener::run() {
    while (true) {
        if (d->done) {
            break;
        }
        for (MidiListenerPort *listenerPort : qAsConst(d->ports)) {
            if (listenerPort->waitTime > 0 && listenerPort->lastRelevantMessage > -1) {
                int i{0};
                for (NoteMessage *message : qAsConst(listenerPort->messages)) {
                    if (i > listenerPort->lastRelevantMessage || i >= MAX_MESSAGES) {
                        break;
                    }
                    Q_EMIT noteChanged(message->port, message->midiNote, message->midiChannel, message->velocity, message->setOn, message->timeStamp, message->byte1, message->byte2, message->byte3);
                    ++i;
                }
                listenerPort->lastRelevantMessage = -1;
            }
        }
        msleep(5);
    }
}

void MidiListener::markAsDone() {
    d->done = true;
}

void MidiListener::addMessage(Port port, double timeStamp, int midiNote, int midiChannel, int velocity, bool setOn, const jack_midi_event_t &event)
{
    MidiListenerPort *listenerPort = d->ports.at(port);
    if (listenerPort->lastRelevantMessage >= MAX_MESSAGES) {
        qWarning() << "Too many messages in a single run before we could report back - we only expected" << MAX_MESSAGES;
    } else {
        if (listenerPort->waitTime == 0) {
            listenerPort->lastRelevantMessage = 0;
        } else {
            listenerPort->lastRelevantMessage++;
        }
        NoteMessage* message = listenerPort->messages.at(listenerPort->lastRelevantMessage);
        message->port = port;
        message->midiNote = midiNote;
        message->midiChannel = midiChannel;
        message->velocity = velocity;
        message->setOn = setOn;
        message->timeStamp = timeStamp;
        message->byte1 = event.buffer[0];
        if (event.size > 1) {
            message->byte2 = event.buffer[1];
        }
        if (event.size > 2) {
            message->byte3 = event.buffer[2];
        }
        if (listenerPort->waitTime == 0) {
            Q_EMIT noteChanged(message->port, message->midiNote, message->midiChannel, message->velocity, message->setOn, message->timeStamp, message->byte1, message->byte2, message->byte3);
        }
    }
}
