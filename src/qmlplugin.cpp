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

#include "qmlplugin.h"

#include <QtQml/qqml.h>
#include <QQmlEngine>
#include <QQmlContext>

#include "FilterProxy.h"
#include "MidiRecorder.h"
#include "Note.h"
#include "NotesModel.h"
#include "PatternImageProvider.h"
#include "PatternModel.h"
#include "PlayGrid.h"
#include "SettingsContainer.h"
#include "SegmentHandler.h"
#include <MidiRouter.h>
#include <SyncTimer.h>

void QmlPlugins::initializeEngine(QQmlEngine *engine, const char *)
{
    engine->addImageProvider("pattern", new PatternImageProvider());
}

void QmlPlugins::registerTypes(const char *uri)
{
    qmlRegisterType<FilterProxy>(uri, 1, 0, "FilterProxy");
    qmlRegisterUncreatableType<Note>(uri, 1, 0, "Note", "Use the getNote function on the main PlayGrid global object to get one of these");
    qmlRegisterUncreatableType<NotesModel>(uri, 1, 0, "NotesModel", "Use the getModel function on the main PlayGrid global object to get one of these");
    qmlRegisterUncreatableType<PatternModel>(uri, 1, 0, "PatternModel", "Use the getPatternModel function on the main PlayGrid global object to get one of these");
    qmlRegisterUncreatableType<SettingsContainer>(uri, 1, 0, "SettingsContainer", "This is for internal use only");
    qmlRegisterType<PlayGrid>(uri, 1, 0, "PlayGrid");
    qmlRegisterSingletonType<PlayGridManager>(uri, 1, 0, "PlayGridManager", [](QQmlEngine *engine, QJSEngine *scriptEngine) -> QObject * {
        Q_UNUSED(scriptEngine)
        PlayGridManager *playGridManager = PlayGridManager::instance();
        playGridManager->setEngine(engine);
        QQmlEngine::setObjectOwnership(playGridManager, QQmlEngine::CppOwnership);
        return playGridManager;
    });
    qmlRegisterSingletonType<SegmentHandler>(uri, 1, 0, "SegmentHandler", [](QQmlEngine *engine, QJSEngine *scriptEngine) -> QObject * {
        Q_UNUSED(engine)
        Q_UNUSED(scriptEngine)
        SegmentHandler *segmentHandler = SegmentHandler::instance();
        QQmlEngine::setObjectOwnership(segmentHandler, QQmlEngine::CppOwnership);
        return segmentHandler;
    });
    qmlRegisterSingletonType<MidiRecorder>(uri, 1, 0, "MidiRecorder", [](QQmlEngine *engine, QJSEngine *scriptEngine) -> QObject * {
        Q_UNUSED(engine)
        Q_UNUSED(scriptEngine)
        MidiRecorder *midiRecorder = MidiRecorder::instance();
        QQmlEngine::setObjectOwnership(midiRecorder, QQmlEngine::CppOwnership);
        return midiRecorder;
    });
    qmlRegisterSingletonType<MidiRouter>(uri, 1, 0, "MidiRouter", [](QQmlEngine *engine, QJSEngine *scriptEngine) -> QObject * {
        Q_UNUSED(engine)
        Q_UNUSED(scriptEngine)
        MidiRouter *midiRouter = MidiRouter::instance();
        QQmlEngine::setObjectOwnership(midiRouter, QQmlEngine::CppOwnership);
        return midiRouter;
    });
    qmlRegisterSingletonType<SyncTimer>(uri, 1, 0, "SyncTimer", [](QQmlEngine *engine, QJSEngine *scriptEngine) -> QObject * {
        Q_UNUSED(engine)
        Q_UNUSED(scriptEngine)
        SyncTimer *syncTimer = SyncTimer::instance();
        QQmlEngine::setObjectOwnership(syncTimer, QQmlEngine::CppOwnership);
        return syncTimer;
    });
}
