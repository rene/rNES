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
#ifndef PALLETEVIEWER_H
#define PALLETEVIEWER_H

#include <QWidget>
#include "NESPalette.h"

/**
 * @brief The PaletteViewer Widget
 * Implements a palette viewer Widget which allows to draw and handle NES
 * Palettes
 */
class PaletteViewer : public QWidget
{
    Q_OBJECT
public:
    /** Palette status */
    enum PalStatus {
        /** There is no valid palette loaded */
        EMPTY,
        /** There is a valid palette loaded and unchanged */
        UNCHANGED,
        /** There is a valid palette loaded and it was changed */
        CHANGED,
    };
    /* Constructor */
    explicit PaletteViewer(QWidget *parent = nullptr);
    /* Enable/Disable show grid */
    void setGrid(bool enabled);
    /* Return show grid state */
    bool getGrid();
    /* Toggle grid state */
    void toggleGrid();
    /* Load palette from file */
    bool loadPalette(const QString& filename);
    /* Load default palette */
    void loadDefaultPalette();
    /* Save palette to file */
    bool savePalette(const QString& filename);
    /* Export palette as PNG image */
    bool exportPalette(const QString& filename);
    /* Return if palette has changed */
    bool hasChanged();
    /**
     * @brief Return palette viewer status
     */
    enum PalStatus getStatus() {
        return this->status;
    }

protected:
    /* Paint function */
    void paintEvent(QPaintEvent *event) override;
    /* Capture mouse events */
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    /** Current palette */
    NESPalette palette;
    /** Palette status */
    PalStatus status;
    /** Grid state */
    bool grid;
    /** Color's square size */
    const int sqsize = 50;
    /** Squares per row */
    const int sqrow = 16;
};

#endif // PALLETEVIEWER_H
