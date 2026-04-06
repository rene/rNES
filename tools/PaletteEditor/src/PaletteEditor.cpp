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
#include "PaletteEditor.h"
#include "./ui_PaletteEditor.h"
#include <QMessageBox>
#include <QFileDialog>

/**
 * @brief Constructor
 * @param parent
 */
PaletteEditor::PaletteEditor(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::PaletteEditor)
{
    ui->setupUi(this);
    this->setSaveButtons(false);
    this->openedFile = "";
}

/**
 * @brief Destructor
 */
PaletteEditor::~PaletteEditor()
{
    delete ui;
}

/**
 * @brief Menu: New
 */
void PaletteEditor::on_actionNew_triggered()
{
    PaletteViewer::PalStatus st = ui->wdPaletteViewer->getStatus();
    if (st == PaletteViewer::EMPTY || st == PaletteViewer::UNCHANGED) {
        ui->wdPaletteViewer->loadDefaultPalette();
        this->openedFile = "";
        this->setSaveButtons(true);
    } else {
        if (askSavePalette() == false) {
            ui->wdPaletteViewer->loadDefaultPalette();
            this->openedFile = "";
            this->setSaveButtons(true);
        }
    }
}

/**
 * @brief Menu: Open
 */
void PaletteEditor::on_actionOpen_triggered()
{
    QString fileName;
    PaletteViewer::PalStatus st = ui->wdPaletteViewer->getStatus();

    if (this->openedFile != "" || st == PaletteViewer::CHANGED) {
        askSavePalette();
    }

    fileName = QFileDialog::getOpenFileName(this,
                                            tr("Open Palette"), "",
                                            tr("NES Palette Files (*.pal)"));
    if (fileName != "") {
        if (ui->wdPaletteViewer->loadPalette(fileName) == true) {
            this->openedFile = fileName;
            this->setSaveButtons(true);
        } else {
            QMessageBox::critical(this, tr("Save Palette"),
                                  tr("Cannot open Palette file!"));
        }
    }
}

/**
 * @brief Save Palette
 */
void PaletteEditor::on_actionSave_triggered()
{
    QString fileName;

    if (this->openedFile != "") {
        fileName = this->openedFile;
    } else {
        fileName = QFileDialog::getSaveFileName(this,
                                                tr("Save Palette"), "",
                                                tr("NES Palette Files (*.pal)"));
        if (fileName == "")
            return;
    }

    if (ui->wdPaletteViewer->savePalette(fileName) == true) {
        QMessageBox::information(this, tr("Save Palette"),
                                 tr("Palette saved successfully!"));
        this->openedFile = fileName;
    } else {
        QMessageBox::critical(this, tr("Save Palette"),
                              tr("Cannot save Palette file!"));
    }
}

/**
 * @brief Menu: Save As...
 */
void PaletteEditor::on_actionSave_As_triggered()
{
    QString fileName;

    fileName = QFileDialog::getSaveFileName(this,
                                            tr("Save Palette"), "",
                                            tr("NES Palette Files (*.pal)"));
    if (fileName == "")
        return;

    if (ui->wdPaletteViewer->savePalette(fileName)) {
        QMessageBox::information(this, tr("Save Palette"),
                                 tr("Palette saved successfully!"));
    } else {
        QMessageBox::critical(this, tr("Save Palette"),
                              tr("Cannot save Palette file!"));
    }
}

/**
 * @brief Menu: Export as PNG...
 */
void PaletteEditor::on_actionExport_as_PNG_triggered()
{
    QString fileName;

    fileName = QFileDialog::getSaveFileName(this,
                                            tr("Export Palette"), "",
                                            tr("PNG Image (*.png)"));
    if (ui->wdPaletteViewer->exportPalette(fileName) == true) {
        QMessageBox::information(this, tr("Export Palette Image"),
                                 tr("Image exported successfully!"));
    } else {
        QMessageBox::critical(this, tr("Export Palette Image"),
                              tr("Cannot export Palette image!"));
    }
}

/**
 * @brief Menu: Quit
 */
void PaletteEditor::on_actionQuit_triggered()
{
    PaletteViewer::PalStatus st = ui->wdPaletteViewer->getStatus();

    if (this->openedFile != "" || st == PaletteViewer::CHANGED) {
        askSavePalette();
    }

    this->close();
}

/**
 * @brief Menu: Edit->Show Grid
 */
void PaletteEditor::on_actionShow_grid_triggered()
{
    ui->wdPaletteViewer->toggleGrid();
}

/**
 * @brief Menu: About
 */
void PaletteEditor::on_actionAbout_triggered()
{
    QMessageBox *aboutBox = new QMessageBox(this);
    aboutBox->setAttribute(Qt::WA_DeleteOnClose);
    aboutBox->setWindowTitle(tr("About"));
    aboutBox->setText("<h3>NES Color Palette Editor</h3>");
    aboutBox->setInformativeText(tr(
        "<p>Website: <a href='http://github.com/rene/rnes'>http://github.com/rene/rnes</a></p>"
        "<p>Copyright 2021-2023 Renê de Souza Pinto</p>"
        "<p>Redistribution and use in source and binary forms, with or without"
        "modification, are permitted provided that the following conditions are met:</p>"
        "<p>Redistributions of source code must retain the above copyright notice, this"
        "list of conditions and the following disclaimer.</p>"
        "<p>Redistributions in binary form must reproduce the above copyright notice,"
        "this list of conditions and the following disclaimer in the documentation"
        "and/or other materials provided with the distribution.</p>"
        "<p>Neither the name of the copyright holder nor the names of its contributors"
        "may be used to endorse or promote products derived from this software without"
        "specific prior written permission.</p>"
        "<p>THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\""
        "AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE"
        "IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE"
        "ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE"
        "LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR"
        "CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF"
        "SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS"
        "INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN"
        "CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)"
        "ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE"
        "POSSIBILITY OF SUCH DAMAGE.</p>"));
    aboutBox->show();
}

/**
 * @brief Ask for user and save the current palette (if wished)
 * @return bool True if a file was saved, False otherwise
 */
bool PaletteEditor::askSavePalette()
{
    QMessageBox::StandardButton reply;

    reply = QMessageBox::question(this, tr("Save Palette"),
                                  tr("Do you want to save the current palette?"));
    if (reply == QMessageBox::Yes) {
        this->on_actionSave_As_triggered();
        return true;
    }
    return false;
}

/**
 * @brief Enable/Disable save related buttons
 * @param enabled
 */
void PaletteEditor::setSaveButtons(bool enabled)
{
    ui->actionSave->setEnabled(enabled);
    ui->actionSave_As->setEnabled(enabled);
    ui->actionExport_as_PNG->setEnabled(enabled);
}
