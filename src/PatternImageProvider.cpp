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

#include "PatternImageProvider.h"

#include "Note.h"
#include "PatternModel.h"
#include "PlayGridManager.h"
#include "SequenceModel.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>

/**
 * \brief A worker class which does the bulk of the work for PreviewImageProvider
 */
class PatternRunnable : public QObject, public QRunnable {
    Q_OBJECT;
public:
    explicit PatternRunnable(const QString &id, const QSize &requestedSize);
    virtual ~PatternRunnable();

    void run() override;

    /**
     * Request that the preview worker abort what it's doing
     */
    Q_SLOT void abort();

    /**
     * \brief Emitted once the preview has been retrieved (successfully or not)
     * @param image The preview image in the requested size (possibly a placeholder)
     */
    Q_SIGNAL void done(QImage image);
private:
    class Private;
    std::unique_ptr<Private> d;
};

class PatternImageProvider::Private {
public:
    Private() {}
    ~Private() {}
};

PatternImageProvider::PatternImageProvider()
    : QQuickAsyncImageProvider()
    , d(new Private)
{
}

class PatternResponse : public QQuickImageResponse
{
    public:
        PatternResponse(const QString &id, const QSize &requestedSize)
        {
            m_runnable = new PatternRunnable(id, requestedSize);
            m_runnable->setAutoDelete(false);
            connect(m_runnable, &PatternRunnable::done, this, &PatternResponse::handleDone, Qt::QueuedConnection);
            connect(this, &QQuickImageResponse::finished, m_runnable, &QObject::deleteLater,  Qt::QueuedConnection);
            QThreadPool::globalInstance()->start(m_runnable);
        }

        void handleDone(QImage image) {
            m_image = image;
            Q_EMIT finished();
        }

        QQuickTextureFactory *textureFactory() const override
        {
            return QQuickTextureFactory::textureFactoryForImage(m_image);
        }

        void cancel() override
        {
            m_runnable->abort();
        }

        PatternRunnable* m_runnable{nullptr};
        QImage m_image;
};

QQuickImageResponse * PatternImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize)
{
    PatternResponse* response = new PatternResponse(id, requestedSize);
    return response;
}


class PatternRunnable::Private {
public:
    Private() {}
    QString id;
    QSize requestedSize;

    bool abort{false};
    QMutex abortMutex;
    bool isAborted() {
        QMutexLocker locker(&abortMutex);
        return abort;
    }
};

PatternRunnable::PatternRunnable(const QString& id, const QSize& requestedSize)
    : d(new Private)
{
    d->id = id;
    d->requestedSize = requestedSize;
}

PatternRunnable::~PatternRunnable()
{
    abort();
}

void PatternRunnable::abort()
{
    QMutexLocker locker(&d->abortMutex);
    d->abort = true;
}

void PatternRunnable::run()
{
    QImage img;
    // In case there's a ? to signify e.g. a timestamp or other trickery to get our thumbnail updated, ignore that section
    QStringList splitId = d->id.split('?').first().split('/');
    if (splitId.count() == 3) {
        QString sequenceName{splitId[0]};
        int patternIndex{splitId[1].toInt()};
        int bank{splitId[2].toInt()};
        SequenceModel* sequence = qobject_cast<SequenceModel*>(PlayGridManager::instance()->getSequenceModel(sequenceName));
        if (sequence) {
            PatternModel *pattern = qobject_cast<PatternModel*>(sequence->get(patternIndex));
            if (pattern) {
                int height = 12;
                int width = pattern->width() * pattern->bankLength();
                img = QImage(width, height, QImage::Format_RGB32);
                // White dot for "got notes to play"
                static const QColor white{"white"};
                // Dark gray dot for "no note, but pattern is enabled"
                static const QColor gray{"gray"};
                // Black dot for "bank is not within availableBars
                static const QColor black{"black"};
                img.fill(black);
                for (int row = bank * pattern->bankLength(); row < (bank + 1) * pattern->bankLength(); ++row) {
                    for (int column = 0; column < pattern->width(); ++column) {
                        if (row < pattern->availableBars()) {
                            QList<QColor> stepColors{gray, gray, gray, gray, gray, gray, gray, gray, gray, gray, gray, gray};
                            const Note *note = qobject_cast<const Note*>(pattern->getNote(row, column));
                            if (note) {
                                const QVariantList &subnotes = note->subnotes();
                                for (const QVariant &subnoteVar : subnotes) {
                                    Note *subnote = subnoteVar.value<Note*>();
                                    // This really shouldn't happen, but let's make sure anyway...
                                    if (subnote->octave() < 12) {
                                        stepColors[subnote->octave()] = white;
                                    }
                                }
                            }
                            for (int stepColumn = 0; stepColumn < height; ++stepColumn) {
                                img.setPixelColor((row * pattern->width() + column), stepColumn, stepColors[stepColumn]);
                            }
                        }
                    }
                }
            }
        }
    }

    Q_EMIT done(img);
}

#include "PatternImageProvider.moc" // We have us some Q_OBJECT bits in here, so we need this one in here as well
