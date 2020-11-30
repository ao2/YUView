/*  This file is part of YUView - The YUV player with advanced analytics toolset
*   <https://github.com/IENT/YUView>
*   Copyright (C) 2015  Institut für Nachrichtentechnik, RWTH Aachen University, GERMANY
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 3 of the License, or
*   (at your option) any later version.
*
*   In addition, as a special exception, the copyright holders give
*   permission to link the code of portions of this program with the
*   OpenSSL library under certain conditions as described in each
*   individual source file, and distribute linked combinations including
*   the two.
*   
*   You must obey the GNU General Public License in all respects for all
*   of the code used other than OpenSSL. If you modify file(s) with this
*   exception, you may extend this exception to your version of the
*   file(s), but you are not obligated to do so. If you do not wish to do
*   so, delete this exception statement from your version. If you delete
*   this exception statement from all source files in the program, then
*   also delete it here.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "videoHandlerResample.h"

#include <algorithm>
#include <QPainter>

#include "videoHandlerYUV.h"

// Activate this if you want to know when which buffer is loaded/converted to image and so on.
#define VIDEOHANDLERRESAMPLE_DEBUG_LOADING 1
#if VIDEOHANDLERRESAMPLE_DEBUG_LOADING && !NDEBUG
#define DEBUG_RESAMPLE qDebug
#else
#define DEBUG_RESAMPLE(fmt,...) ((void)0)
#endif

videoHandlerResample::videoHandlerResample() : videoHandler()
{
}

QImage videoHandlerResample::calculateDifference(frameHandler *item2, const int frameIndex0, const int frameIndex1, QList<infoItem> &differenceInfoList, const int amplificationFactor, const bool markDifference)
{
  if (!this->inputValid())
    return {};

  auto mappedIndex = frameIndex0 + (this->ui.created() ? ui.spinBoxStart->value() : 0);
  return videoHandler::calculateDifference(item2, mappedIndex, frameIndex1, differenceInfoList, amplificationFactor, markDifference);
}

itemLoadingState videoHandlerResample::needsLoading(int frameIndex, bool loadRawValues)
{
  auto mappedIndex = frameIndex + (this->ui.created() ? ui.spinBoxStart->value() : 0);
  return videoHandler::needsLoading(mappedIndex, loadRawValues);
}

void videoHandlerResample::loadResampledFrame(int frameIndex, bool loadToDoubleBuffer)
{
  if (!this->inputValid())
    return;

  auto mappedIndex = frameIndex + (this->ui.created() ? ui.spinBoxStart->value() : 0);
  DEBUG_RESAMPLE("videoHandlerResample::loadResampledFrame frameIndex %d mapped to %d", frameIndex, mappedIndex);
  
  auto video = dynamic_cast<videoHandler*>(this->inputVideo.data());
  if (video && video->getCurrentImageIndex() != mappedIndex)
    video->loadFrame(mappedIndex);
  
  auto interpolationMode = Qt::SmoothTransformation;
  if (ui.created() && ui.comboBoxInterpolation->currentIndex() == 1)
    interpolationMode = Qt::FastTransformation;
  auto newFrame = this->inputVideo->getCurrentFrameAsImage().scaled(this->getFrameSize(), Qt::IgnoreAspectRatio, interpolationMode);

  if (newFrame.isNull())
    return;

  if (loadToDoubleBuffer)
  {
    doubleBufferImage = newFrame;
    doubleBufferImageFrameIndex = mappedIndex;
    DEBUG_RESAMPLE("videoHandlerResample::loadResampledFrame Loaded frame %d to double buffer", mappedIndex);
  }
  else
  {
    // The new difference frame is ready
    QMutexLocker lock(&this->currentImageSetMutex);
    currentImage = newFrame;
    currentImageIndex = mappedIndex;
    DEBUG_RESAMPLE("videoHandlerResample::loadResampledFrame Loaded frame %d", mappedIndex);
  }
}

bool videoHandlerResample::inputValid() const
{
  return (!this->inputVideo.isNull() && this->inputVideo->isFormatValid());
}

void videoHandlerResample::setInputVideo(frameHandler *childVideo, indexRange childFrameRange)
{
  if (this->inputVideo != childVideo)
  {
    // Something changed
    this->inputVideo = childVideo;
    DEBUG_RESAMPLE("videoHandlerResample::loadResampledFrame setting new vide");

    if (this->inputValid())
    {
      auto size = this->inputVideo->getFrameSize();

      if (ui.created())
      {
        QSignalBlocker blockerWidth(ui.spinBoxWidth);
        QSignalBlocker blockerHeight(ui.spinBoxHeight);
        ui.spinBoxWidth->setValue(size.width());
        ui.spinBoxHeight->setValue(size.height());

        QSignalBlocker blockerSampling(ui.spinBoxSampling);
        ui.spinBoxSampling->setValue(1);

        QSignalBlocker blockerStart(ui.spinBoxStart);
        ui.spinBoxStart->setMinimum(childFrameRange.first);
        ui.spinBoxStart->setMaximum(childFrameRange.second);
        ui.spinBoxStart->setValue(childFrameRange.first);

        QSignalBlocker blockerEnd(ui.spinBoxEnd);
        ui.spinBoxEnd->setMinimum(childFrameRange.first);
        ui.spinBoxEnd->setMaximum(childFrameRange.second);
        ui.spinBoxEnd->setValue(childFrameRange.second);
      }
      
      this->setFrameSize(size);
    }

    // If something changed, we might need a redraw
    emit signalHandlerChanged(true, RECACHE_NONE);
  }
}

QLayout *videoHandlerResample::createResampleHandlerControls()
{
  Q_ASSERT_X(!ui.created(), "createResampleHandlerControls", "Controls must only be created once");

  ui.setupUi();

  ui.comboBoxInterpolation->addItem("Bilinear");
  ui.comboBoxInterpolation->addItem("Linear");
  ui.comboBoxInterpolation->setCurrentIndex(0);

  auto size = QSize(0, 0);
  if (this->inputVideo)
    size = this->inputVideo->getFrameSize();

  ui.spinBoxWidth->setValue(size.width());
  ui.spinBoxHeight->setValue(size.height());

  this->connect(ui.spinBoxWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, &videoHandlerResample::slotResampleControlChanged);
  this->connect(ui.spinBoxHeight, QOverload<int>::of(&QSpinBox::valueChanged), this, &videoHandlerResample::slotResampleControlChanged);
  this->connect(ui.comboBoxInterpolation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &videoHandlerResample::slotInterpolationModeChanged);
  this->connect(ui.spinBoxStart, QOverload<int>::of(&QSpinBox::valueChanged), this, &videoHandlerResample::slotCutAndSampleControlChanged);
  this->connect(ui.spinBoxEnd, QOverload<int>::of(&QSpinBox::valueChanged), this, &videoHandlerResample::slotCutAndSampleControlChanged);
  this->connect(ui.spinBoxSampling, QOverload<int>::of(&QSpinBox::valueChanged), this, &videoHandlerResample::slotCutAndSampleControlChanged);

  return ui.topVBoxLayout;
}

void videoHandlerResample::slotResampleControlChanged(int value)
{
  Q_UNUSED(value);

  auto newSize = QSize(ui.spinBoxWidth->value(), ui.spinBoxHeight->value());
  this->setFrameSize(newSize);
  this->invalidateAllBuffers();

  emit signalHandlerChanged(true, RECACHE_CLEAR);
}

void videoHandlerResample::slotInterpolationModeChanged(int value)
{
  Q_UNUSED(value);
  this->invalidateAllBuffers();
  emit signalHandlerChanged(true, RECACHE_CLEAR);
}

void videoHandlerResample::slotCutAndSampleControlChanged(int value)
{
  Q_UNUSED(value);
  DEBUG_RESAMPLE("videoHandlerResample::slotCutAndSampleControlChanged");
  this->invalidateAllBuffers();
  emit signalHandlerChanged(true, RECACHE_CLEAR);
}