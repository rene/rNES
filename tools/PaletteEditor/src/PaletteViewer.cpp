/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rNES - Nintendo Entertainment System Emulator
 * Copyright 2021-2023 Renê de Souza Pinto
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "PaletteViewer.h"
#include <QPixmap>
#include <QPainter>
#include <QEvent>
#include <QMouseEvent>
#include <QColorDialog>
#include <QDebug>

/**
 * @brief Constructor
 * @param parent
 */
PaletteViewer::PaletteViewer(QWidget *parent)
    : QWidget{parent}
{
    this->grid   = false;
    this->status = EMPTY;
    this->installEventFilter(this);
}

/**
 * @brief Enable/Disable show grid
 * @param enabled True to enable grid, false otherwise
 */
void PaletteViewer::setGrid(bool enabled)
{
    this->grid = enabled;
}

/**
 * @brief Return show grid state
 * @return bool
 */
bool PaletteViewer::getGrid()
{
    return this->grid;
}

/**
 * @brief Toggle grid state
 */
void PaletteViewer::toggleGrid()
{
    if (this->grid) {
        this->grid = false;
    } else {
        this->grid = true;
    }
    this->repaint();
}

/**
 * @brief Return if palette has changed
 * @return bool
 */
bool PaletteViewer::hasChanged()
{
    if (this->status == CHANGED) {
        return true;
    } else {
        return false;
    }
}

/**
 * @brief Load palette from file
 * @param filename Palette file path
 * @return bool True if the file was loaded, False otherwise
 */
bool PaletteViewer::loadPalette(const QString& filename)
{
    if (this->palette.loadFromFile(filename) == QFile::NoError) {
        this->status = UNCHANGED;
        this->repaint();
        return true;
    } else {
        return false;
    }
}

/**
 * @brief Load default palette
 */
void PaletteViewer::loadDefaultPalette()
{
    this->palette.loadDefaultPalette();
    this->status = UNCHANGED;
    this->repaint();
}

/**
 * @brief Save palette to file
 * @param filename Palette file path
 * @return bool True if file was saved, false otherwise
 */
bool PaletteViewer::savePalette(const QString& filename)
{
    if(this->palette.saveToFile(filename) == QFile::NoError) {
        return true;
    } else {
        return false;
    }
}

/**
 * @brief Export palette as PNG image
 * @param filename Image file path
 * @return bool True if file was saved, false otherwise
 */
bool PaletteViewer::exportPalette(const QString& filename)
{
    QRect area(0, 0, (this->sqrow * this->sqsize),
               (this->palette.getSize() / this->sqrow) * this->sqsize);
    QPixmap png = this->grab(area);
    return png.save(filename, "png", 100);
}

/**
 * @brief Draw event
 * @param event
 */
void PaletteViewer::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    int i, x, y;

    if (this->status == EMPTY)
        return;

    // Draw squares
    x = 0;
    y = 0;
    for (i = 0; i < this->palette.getSize(); i++) {
        QRect quad(x, y, this->sqsize, this->sqsize);
        painter.fillRect(quad, this->palette.getColor(i));
        x += this->sqsize;
        if (((i+1) % this->sqrow) == 0) {
            x  = 0;
            y += this->sqsize;
        }
    }

    // Draw grid (if enabled)
    if (this->grid) {
        x = this->sqsize;
        painter.setPen(QColor(100, 100, 100));
        for (i = 1; i <= this->sqrow; i++) {
            painter.drawLine(x, 0, x, y);
            x += this->sqsize;
        }
        x = this->sqsize * this->sqrow;
        y = this->sqsize;
        for (i = 0; i < (this->palette.getSize() / this->sqrow) - 1; i++) {
            painter.drawLine(0, y, x, y);
            y += this->sqsize;
        }
    }
}

/**
 * @brief Handle mouse events
 * @param obj
 * @param event
 * @return bool
 */
bool PaletteViewer::eventFilter(QObject *obj, QEvent *event)
{
    QMouseEvent *menv;
    if (event->type() == QEvent::MouseButtonPress) {
        menv = static_cast<QMouseEvent *>(event);

        if (this->status == EMPTY)
            return true;

        // Decode which square was clicked
        int ix = menv->pos().x() / this->sqsize;
        int iy = menv->pos().y() / this->sqsize;
        // Color's palette index
        int idx = (iy * this->sqrow) + ix;
        if (idx >= palette.getSize())
            return true;

        // Show color dialog
        const QColor color = QColorDialog::getColor(
            this->palette.getColor(idx), this, tr("Select Color"));

        // Set the new color in the palette (if valid)
        if (color.isValid()) {
            this->palette.setColor(idx, color);
            this->repaint();
            this->status = CHANGED;
        }
        return true;
    } else {
        // Standard event processing
        return QObject::eventFilter(obj, event);
    }
}
