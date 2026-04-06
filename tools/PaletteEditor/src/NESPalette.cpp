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
#include "NESPalette.h"
#include <QByteArray>

/**
 * @brief Create a new palette (with no colors set)
 */
NESPalette::NESPalette()
{
    this->palette = nullptr;
}

/**
 * @brief Create a palette from file
 * @param filename Palette file path
 * @note If file cannot be loaded, default palette will be used
 */
NESPalette::NESPalette(const QString& filename)
{
    if (!this->loadFromFile(filename)) {
        this->loadDefaultPalette();
    }
}

/**
 * @brief Destructor
 */
NESPalette::~NESPalette()
{
    if (this->palette != nullptr)
        delete this->palette;
}

/**
 * @brief Load palette from file
 * @param filename Palette file path
 * @return QFile:FileError
 */
QFile::FileError NESPalette::loadFromFile(const QString& filename)
{
    QFile file(filename);
    QByteArray data;

    if (!file.open(QIODevice::ReadOnly)) {
        return file.error();
    } else {
        if (file.size() < sizeof(this->default_palette)) {
            file.close();
            return QFile::FileError::ReadError;
        } else {
            data = file.read(file.size());
            file.close();
            if (this->palette == nullptr) {
                this->palette = new palette_t;
            }
            memcpy(this->palette, data.data(), sizeof(palette_t));
        }
    }

    return QFile::FileError::NoError;
}

/**
 * @brief Save palette to file
 * @param filename File path
 * @return QFile::FileError
 */
QFile::FileError NESPalette::saveToFile(const QString& filename)
{
    QFile file(filename);

    if (this->palette == nullptr)
        return QFile::FileError::FatalError;

    if (!file.open(QIODevice::WriteOnly)) {
        return file.error();
    } else {
        QByteArray data((char*)this->palette, sizeof(palette_t));
        file.write(data);
        file.close();
    }

    return QFile::FileError::NoError;
}

/**
 * @brief Load default palette
 * @note Overrides the current palette (if present)
 */
void NESPalette::loadDefaultPalette()
{
    if (this->palette != nullptr) {
        delete this->palette;
    }
    this->palette = new palette_t;
    memcpy(this->palette, this->default_palette, sizeof(palette_t));
}

/**
 * @brief Set a color in the palette
 * @param index Color index
 * @param color Color
 * @return bool True if color was set, False otherwise
 */
bool NESPalette::setColor(int index, QColor color)
{
    if (index < this->getSize() && this->palette != nullptr) {
        this->palette->color[index].R = color.red();
        this->palette->color[index].G = color.green();
        this->palette->color[index].B = color.blue();
        return true;
    } else {
        return false;
    }
}

/**
 * @brief Get a color from the palette
 * @param index Color index
 * @return QColor Color or Black (0,0,0) in case of error
 */
QColor NESPalette::getColor(int index)
{
    if (index < this->getSize() && this->palette != nullptr) {
        return QColor(this->palette->color[index].R,
                      this->palette->color[index].G,
                      this->palette->color[index].B);
    } else {
        return QColor(0,0,0);
    }
}
