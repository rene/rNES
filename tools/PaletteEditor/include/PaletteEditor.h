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
#ifndef PALETTEEDITOR_H
#define PALETTEEDITOR_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class PaletteEditor; }
QT_END_NAMESPACE

class PaletteEditor : public QMainWindow
{
    Q_OBJECT

public:
    /* Constructor */
    PaletteEditor(QWidget *parent = nullptr);
    /* Destructor */
    ~PaletteEditor();

private slots:
    /* Menu: New */
    void on_actionNew_triggered();
    /* Menu: Open */
    void on_actionOpen_triggered();
    /* Menu: Save */
    void on_actionSave_triggered();
    /* Menu: Save As... */
    void on_actionSave_As_triggered();
    /* Menu: Export as PNG... */
    void on_actionExport_as_PNG_triggered();
    /* Menu: Quit */
    void on_actionQuit_triggered();
    /* Menu: Edit->Show Grid */
    void on_actionShow_grid_triggered();
    /* Menu: About */
    void on_actionAbout_triggered();

private:
    /* Palette editor ui */
    Ui::PaletteEditor *ui;
    /* Current opened file */
    QString openedFile;
    /* Ask and save palette */
    bool askSavePalette();
    /* Enable/Disable save related buttons */
    void setSaveButtons(bool enabled);
};
#endif // PALETTEEDITOR_H
