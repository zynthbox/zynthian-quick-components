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

#include "NotesModel.h"
#include "Note.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QJSValue>

struct Entry {
    Note* note{nullptr};
    QVariant metaData;
};

class NotesModel::Private {
public:
    Private(NotesModel *q)
        : q(q)
    {
        noteDataChangedUpdater.setInterval(1);
        noteDataChangedUpdater.setSingleShot(true);
        QObject::connect(&noteDataChangedUpdater, &QTimer::timeout, q, [this,q](){
            for (const QList<Entry> &list : entries) {
                for (const Entry &obj : list) {
                    Note *note = obj.note;
                    if (note) {
                        note->disconnect(q);
                        connect(note, &Note::nameChanged, q, [this,note](){ noteChanged(note); });
                        connect(note, &Note::midiNoteChanged, q, [this,note](){ noteChanged(note); });
                        connect(note, &Note::midiChannelChanged, q, [this,note](){ noteChanged(note); });
                        connect(note, &Note::isPlayingChanged, q, [this,note](){ noteChanged(note); });
                        connect(note, &Note::subnotesChanged, q, [this,note](){ noteChanged(note); });
                    }
                }
            }
        });
        noteDataChangedEmitter.setInterval(0);
        noteDataChangedEmitter.setSingleShot(true);
        QObject::connect(&noteDataChangedEmitter, &QTimer::timeout, q, [this](){ emitNoteDataChanged(); });
        isEmtpyUpdater.setInterval(1);
        isEmtpyUpdater.setSingleShot(true);
        QObject::connect(&isEmtpyUpdater, &QTimer::timeout, q, [this](){ updateIsEmtpy(); });
    }
    NotesModel *q;
    NotesModel *parentModel{nullptr};
    int parentRow{-1};
    quint64 lastModified{0};
    QList<NotesModel*> childModels;
    bool isEmpty{true};
    QList< QList<Entry> > entries;

    void ensurePositionExists(int row, int column) {
        if (entries.count() < row + 1) {
            q->beginInsertRows(QModelIndex(), entries.count(), row);
            for (int i = entries.count(); i < row + 1; ++i) {
                entries << QList<Entry>();
            }
            q->endInsertRows();
        }
        QList<Entry> rowList = entries[row];
        if (rowList.count() < column + 1) {
            q->beginInsertColumns(QModelIndex(), rowList.count(), column + 1);
            for (int i = rowList.count(); i < column + 1; ++i) {
                rowList << Entry();
            }
            entries[row] = rowList;
            q->endInsertColumns();
        }
    }
    QTimer noteDataChangedUpdater;
    QList<Note*> updateNotes;
    void noteChanged(Note* note) {
        if (!updateNotes.contains(note)) {
            updateNotes << note;
            noteDataChangedEmitter.start();
        }
    }
    QTimer noteDataChangedEmitter;
    void emitNoteDataChanged() {
        for (int row = 0; row < entries.count(); ++row) {
            QList<Entry> rowEntries = entries[row];
            for (int column = 0; column < rowEntries.count(); ++column) {
                const Entry &entry = rowEntries.at(column);
                if (updateNotes.contains(entry.note)) {
                    QModelIndex idx = q->index(row, column);
                    q->dataChanged(idx, idx);
                }
            }
        }
        updateNotes.clear();
    }

    QTimer isEmtpyUpdater;
    void updateIsEmtpy() {
        bool updatedIsEmpty{true};
        for (const QList<Entry> &row : entries) {
            for (const Entry &entry : row) {
                if (entry.note && (entry.note->midiNote() < 128 || entry.note->subnotes().count() > 0)) {
                    updatedIsEmpty = false;
                    break;
                }
            }
            if (updatedIsEmpty == false) {
                break;
            }
        }
        if (updatedIsEmpty != isEmpty) {
            isEmpty = updatedIsEmpty;
            Q_EMIT q->isEmptyChanged();
        }
    }
};

NotesModel::NotesModel(PlayGridManager* parent)
    : QAbstractListModel(parent)
    , d(new Private(this))
{
    QTimer *lastModifiedChanger = new QTimer(this);
    lastModifiedChanger->setInterval(1);
    lastModifiedChanger->setSingleShot(true);
    connect(lastModifiedChanger, &QTimer::timeout, this, [this](){
        d->lastModified = QDateTime::currentMSecsSinceEpoch();
        Q_EMIT lastModifiedChanged();
    });
    connect(this, &QAbstractItemModel::dataChanged, lastModifiedChanger, QOverload<>::of(&QTimer::start));
    connect(this, &QAbstractItemModel::modelReset, lastModifiedChanger, QOverload<>::of(&QTimer::start));
    connect(this, &QAbstractItemModel::rowsInserted, lastModifiedChanger, QOverload<>::of(&QTimer::start));
    connect(this, &QAbstractItemModel::rowsRemoved, lastModifiedChanger, QOverload<>::of(&QTimer::start));
    connect(this, &NotesModel::rowsChanged, lastModifiedChanger, QOverload<>::of(&QTimer::start));
}

NotesModel::NotesModel(NotesModel* parent, int row)
    : QAbstractListModel(parent)
    , d(new Private(this))
{
    d->parentModel = parent;
    d->parentRow = row;
//     qDebug() << Q_FUNC_INFO << parent << row << d->parentModel << d->parentRow;
//     connect(d->parentModel, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex& parent, int first, int last){
//         // If we are being moved out of the way, update our position with the amount we've been moved
//         if (!parent.isValid() && d->parentRow >= first) {
//             d->parentRow += 1 + (last - first);
//             Q_EMIT parentRowChanged();
//         }
//     });
//     connect(d->parentModel, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex& parent, int first, int last){
//         if (!parent.isValid()) {
//             if (d->parentRow > last) {
//                 d->parentRow -= 1 + (last - first);
//                 Q_EMIT parentRowChanged();
//             } else if (d->parentRow >= first && d->parentRow <= last) {
//                 deleteLater();
//             }
//         }
//     });
//     connect(d->parentModel, &QAbstractItemModel::modelReset, this, [this](){ deleteLater(); });
//     connect(d->parentModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector< int >& roles = QVector<int>()){
//         Q_UNUSED(roles);
//         if (topLeft.row() >= d->parentRow && bottomRight.row() <= d->parentRow) {
//             beginResetModel();
//             endResetModel();
//         }
//     });
}

NotesModel::~NotesModel()
{
    delete d;
}

QVariantMap NotesModel::roles() const
{
    static const QVariantMap roles{
        {"note", NoteRole},
        {"metadata", MetadataRole},
        {"rowModel", RowModelRole}
    };
    return roles;
}

QHash<int, QByteArray> NotesModel::roleNames() const
{
    static const QHash<int, QByteArray> roles{
        {NoteRole, "note"},
        {MetadataRole, "metadata"},
        {RowModelRole, "rowModel"}
    };
    return roles;
}

int NotesModel::rowCount(const QModelIndex& parent) const
{
    int count{0};
    if (d->parentModel) {
        if (!parent.isValid()) {
            count = d->parentModel->columnCount(d->parentModel->index(d->parentRow));
        }
    } else {
        if (!parent.isValid()) {
            count = d->entries.count();
        }
    }
    return count;
}

int NotesModel::columnCount(const QModelIndex& parent) const
{
    int count = 0;
    if (d->parentModel) {
        if (parent.isValid()) {
            count = 1;
        }
    } else if (parent.isValid() && parent.row() >= 0 && parent.row() < d->entries.count()) {
        count = d->entries.at(parent.row()).count();
    }
    return count;
}

QVariant NotesModel::data(const QModelIndex& index, int role) const
{
    QVariant result;
//     qDebug() << Q_FUNC_INFO << this << objectName() << d->parentModel << d->parentRow << index.row();
    if (d->parentModel) {
        result = d->parentModel->data(d->parentModel->index(d->parentRow, index.row()), role);
    } else if (index.row() >= 0 && index.row() < d->entries.count()) {
        const QList<Entry> &rowEntries = d->entries.at(index.row());
        if (index.column() >= 0 && index.column() < rowEntries.count()) {
            const Entry &entry = rowEntries.at(index.column());
            switch(role) {
            case Qt::DisplayRole:
            case NoteRole:
                result.setValue<QObject*>(entry.note);
                break;
            case MetadataRole:
                result = entry.metaData;
                break;
            case RowModelRole:
            {
                NotesModel *childModel{nullptr};
                for (NotesModel *aModel : d->childModels) {
                    if (aModel->parentRow() == index.row()) {
                        childModel = aModel;
                        break;
                    }
                }
                if (!childModel) {
                    childModel = new NotesModel(const_cast<NotesModel*>(this), index.row());
                    childModel->setObjectName(QString("%1 child model").arg(objectName()));
                    d->childModels << childModel;
                }
                result.setValue<QObject*>(childModel);
                break;
            }
            default:
                break;
            }
        }
    }
    return result;
}

QModelIndex NotesModel::index(int row, int column, const QModelIndex& /*parent*/) const
{
    QModelIndex idx;
    if (row >= 0 && row < d->entries.count()) {
        if (column >= 0 && column < d->entries[row].count()) {
            idx = createIndex(row, column);
        }
    }
    return idx;
}

QObject* NotesModel::parentModel() const
{
    return d->parentModel;
}

int NotesModel::parentRow() const
{
    return d->parentRow;
}

quint64 NotesModel::lastModified() const
{
    return d->lastModified;
}

bool NotesModel::isEmpty() const
{
    return d->isEmpty;
}

QVariantList NotesModel::getRow(int row) const
{
    QVariantList list;
    if (!d->parentModel) {
        if (row >= 0 && row < d->entries.count()) {
            const QList<Entry> &entries = d->entries.at(row);
            for (const Entry &entry : entries) {
                list << QVariant::fromValue<QObject*>(entry.note);
            }
        }
    }
    return list;
}

QVariantList NotesModel::uniqueRowNotes(int row) const
{
    QVariantList notes;
    if (!d->parentModel) {
        if (row >= 0 && row < d->entries.count()) {
            std::function<void(Note*)> addNotesIfNotThere;
            addNotesIfNotThere = [&notes,&addNotesIfNotThere](Note *note) {
                if (note && note->subnotes().count() > 0) {
                    for (const QVariant &subnote : note->subnotes()) {
                        addNotesIfNotThere(subnote.value<Note*>());
                    }
                } else if (note) {
                    bool foundNote{false};
                    for (const QVariant &val : notes) {
                        if (val.value<Note*>() == note) {
                            foundNote = true;
                            break;
                        }
                    }
                    if (!foundNote) {
                        notes << QVariant::fromValue<QObject*>(note);
                    }
                }
            };
            const QList<Entry> &entries = d->entries.at(row);
            for (const Entry &entry : entries) {
                addNotesIfNotThere(entry.note);
            }
        }
    }
    return notes;
}

QObject* NotesModel::getNote(int row, int column) const
{
    QObject *obj{nullptr};
    if (!d->parentModel) {
        if (row >= 0 && row < d->entries.count()) {
            const QList<Entry> &rowEntries = d->entries.at(row);
            if (column >= 0 && column < rowEntries.count()) {
                obj = rowEntries.at(column).note;
            }
        }
    }
    return obj;
}

void NotesModel::setNote(int row, int column, QObject* note)
{
    if (!d->parentModel) {
        d->ensurePositionExists(row, column);
        QList<Entry> rowList = d->entries[row];
        rowList[column].note = qobject_cast<Note*>(note);
        d->entries[row] = rowList;
        d->isEmtpyUpdater.start();
        QModelIndex changed = createIndex(row, column);
        dataChanged(changed, changed);
    }
}

QVariantList NotesModel::getRowMetadata(int row) const
{
    QVariantList list;
    if (!d->parentModel) {
        if (row >= 0 && row < d->entries.count()) {
            const QList<Entry> &entries = d->entries.at(row);
            for (const Entry &entry : entries) {
                list << entry.metaData;
            }
        }
    }
    return list;
}

QVariant NotesModel::getMetadata(int row, int column) const
{
    QVariant data;
    if (!d->parentModel) {
        if (row >= 0 && row < d->entries.count()) {
            const QList<Entry> &rowEntries = d->entries.at(row);
            if (column >= 0 && column < rowEntries.count()) {
                data = rowEntries.at(column).metaData;
            }
        }
    }
    return data;
}

void NotesModel::setMetadata(int row, int column, QVariant metadata)
{
    static const QLatin1String jsvalueType{"QJSValue"};
    if (!d->parentModel) {
        d->ensurePositionExists(row, column);
        QList<Entry> rowList = d->entries[row];
        QVariant actualMeta{metadata};
        if (QString(metadata.typeName()) == jsvalueType) {
            const QJSValue tempMeta{metadata.value<QJSValue>()};
            actualMeta = tempMeta.toVariant();
        }
        rowList[column].metaData = actualMeta;
        d->entries[row] = rowList;
        d->isEmtpyUpdater.start();
        QModelIndex changed = createIndex(row, column);
        dataChanged(changed, changed);
    }
}

void NotesModel::setRowData(int row, QVariantList notes, QVariantList metadata)
{
    static const QLatin1String jsvalueType{"QJSValue"};
    if (!d->parentModel) {
        if (row > -1 && row < rowCount()) {
            QList<Entry> rowList;
            for (int i = 0; i < notes.count(); ++i) {
                Note* actualNote = notes.at(i).value<Note*>();
                QVariant actualMeta{metadata.at(i)};
                if (QString(actualMeta.typeName()) == jsvalueType) {
                    const QJSValue tempMeta{actualMeta.value<QJSValue>()};
                    actualMeta = tempMeta.toVariant();
                }
                Entry entry;
                entry.note = actualNote;
                entry.metaData = actualMeta;
                rowList.append(entry);
            }
            d->entries[row] = rowList;
            d->isEmtpyUpdater.start();
            dataChanged(createIndex(row, 0), createIndex(row, rowList.count() - 1));
        }
    }
}

void NotesModel::trim()
{
    if (!d->parentModel) {
        QList< QList<Entry> > newList;
        for (const QList<Entry> &rowList : d->entries) {
            QList<Entry> newRow;
            QList<Entry> trailing;
            for (const Entry& entry : rowList) {
                if (entry.note) {
                    newRow << trailing;
                    trailing.clear();
                    newRow << entry;
                } else {
                    trailing << entry;
                }
            }
            if (newRow.count() > 0) {
                newList << newRow;
            }
        }
        beginResetModel();
        d->entries = newList;
        d->isEmtpyUpdater.start();
        endResetModel();
    }
}

void NotesModel::clear()
{
    if (!d->parentModel) {
        beginResetModel();
        for (QList<Entry> &list : d->entries) {
            for (const Entry &entry : list) {
                if (entry.note) {
                    entry.note->disconnect(this);
                }
            }
        }
        d->entries.clear();
        d->isEmtpyUpdater.start();
        endResetModel();
        Q_EMIT rowsChanged();
    }
}

void NotesModel::addRow(const QVariantList &notes, const QVariantList &metadata)
{
    if (!d->parentModel) {
        QList<Entry> actualNotes;
        int metadataCount = metadata.count();
        for (int i = 0; i < notes.count(); ++i) {
            Entry entry;
            Note *note = notes[i].value<Note*>();
            entry.note = note;
            if (i < metadataCount) {
                entry.metaData = metadata[i];
            }
            actualNotes << entry;
        }
        if (actualNotes.count() > 0) {
            beginInsertRows(QModelIndex(), 0, 0);
            d->entries.insert(0, actualNotes);
            d->noteDataChangedUpdater.start();
            d->isEmtpyUpdater.start();
            endInsertRows();
            Q_EMIT rowsChanged();
        }
    }
}

void NotesModel::appendRow(const QVariantList& notes, const QVariantList& metadata)
{
    insertRow(d->entries.count(), notes, metadata);
}

void NotesModel::insertRow(int index, const QVariantList& notes, const QVariantList& metadata)
{
    if (!d->parentModel && index > -1 && index <= d->entries.count()) {
        QList<Entry> actualNotes;
        int metadataCount = metadata.count();
        for (int i = 0; i < notes.count(); ++i) {
            Entry entry;
            Note *note = notes[i].value<Note*>();
            entry.note = note;
            if (i < metadataCount) {
                entry.metaData = metadata[i];
            }
            actualNotes << entry;
        }
        if (actualNotes.count() > 0) {
            beginInsertRows(QModelIndex(), index, index);
            d->entries.insert(index, actualNotes);
            d->noteDataChangedUpdater.start();
            endInsertRows();
            d->isEmtpyUpdater.start();
            Q_EMIT rowsChanged();
        }
    }
}

void NotesModel::removeRow(int row)
{
    if (!d->parentModel && row > -1 && row < d->entries.count()) {
        beginRemoveRows(QModelIndex(), row, row);
        d->entries.removeAt(row);
        d->isEmtpyUpdater.start();
        endRemoveRows();
    }
}

Note *switchNoteMidiChannel(Note *note, int newMidiChannel) {
    Note *newNote{nullptr};
    if (note) {
        if (note->subnotes().count() > 0) {
            QVariantList subnotes;
            for (const QVariant &variantNote : note->subnotes()) {
                subnotes << QVariant::fromValue<QObject*>(switchNoteMidiChannel(variantNote.value<Note*>(), newMidiChannel));
            }
            newNote = qobject_cast<Note*>(PlayGridManager::instance()->getCompoundNote(subnotes));
        } else {
            newNote = qobject_cast<Note*>(PlayGridManager::instance()->getNote(note->midiNote(), newMidiChannel));
        }
    }
    return newNote;
}

void NotesModel::changeMidiChannel(int midiChannel)
{
    qDebug() << this << "Changing midi to" << midiChannel;
    int longestRow{0};
    for (QList<Entry> &entries : d->entries) {
        for (int i = 0; i < entries.count(); ++i) {
            entries[i].note = switchNoteMidiChannel(entries[i].note, qBound(-1, midiChannel, 16));
        }
        longestRow = qMax(longestRow, entries.count());
    }
    dataChanged(createIndex(0, 0), createIndex(d->entries.count(), longestRow));
}

PlayGridManager* NotesModel::playGridManager() const
{
    if (d->parentModel) {
        return d->parentModel->playGridManager();
    }
    return PlayGridManager::instance();
}
