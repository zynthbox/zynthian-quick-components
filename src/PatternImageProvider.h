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

#ifndef PATTERNIMAGEPROVIDER_H
#define PATTERNIMAGEPROVIDER_H

#include <QQuickAsyncImageProvider>

/**
 * \brief An image provider which sends back a visual representation of a PatternModel
 *
 * The url used to fetch an image of a pattern might look as follows:
 * image://pattern/Global/3/1
 * The above will return a representation of
 * Bank B (that is, the bank at the zero indexed position 1) 
 * in the pattern at index 3 (that is, the fourth pattern)
 * in the sequence named Global
 */
class PatternImageProvider : public QQuickAsyncImageProvider
{
public:
    explicit PatternImageProvider();
    ~PatternImageProvider() override;

    /**
     * \brief Get an image.
     *
     * @param id The source of the image.
     * @param requestedSize The required size of the final image, unused.
     *
     * @return an asynchronous image response
     */
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
private:
    class Private;
    std::unique_ptr<Private> d;
};

#endif//PATTERNIMAGEPROVIDER_H
