//
//    PanoramicDialog.cpp: Description
//    Copyright (C) 2020 Gonzalo José Carracedo Carballal
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as
//    published by the Free Software Foundation, either version 3 of the
//    License, or (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful, but
//    WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public
//    License along with this program.  If not, see
//    <http://www.gnu.org/licenses/>
//

#include <PanoramicDialog.h>
#include <Suscan/Library.h>
#include "ui_PanoramicDialog.h"
#include "MainSpectrum.h"
#include <SuWidgetsHelpers.h>
#include <SigDiggerHelpers.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <QFileDialog>
#include <QMessageBox>
#include <Waterfall.h>
#include <GLWaterfall.h>

using namespace SigDigger;

void
SavedSpectrum::set(qint64 start, qint64 end, const float *data, size_t size)
{
  this->start = start;
  this->end   = end;
  this->data.assign(data, data + size);
}

bool
SavedSpectrum::exportToFile(QString const &path)
{
  std::ofstream of(path.toStdString().c_str(), std::ofstream::binary);

  if (!of.is_open())
    return false;

  of << "%\n";
  of << "% Panoramic Spectrum file generated by SigDigger\n";
  of << "%\n\n";

  of << "freqMin = " << this->start << ";\n";
  of << "freqMax = " << this->end << ";\n";
  of << "PSD = [ ";

  of << std::setprecision(std::numeric_limits<float>::digits10);

  for (auto p : this->data)
    of << p << " ";

  of << "];\n";

  return true;
}

////////////////////////// PanoramicDialogConfig ///////////////////////////////
#define STRINGFY(x) #x
#define STORE(field) obj.set(STRINGFY(field), field)
#define LOAD(field) field = conf.get(STRINGFY(field), field)

void
PanoramicDialogConfig::deserialize(Suscan::Object const &conf)
{
  LOAD(fullRange);
  LOAD(rangeMin);
  LOAD(rangeMax);
  LOAD(panRangeMin);
  LOAD(panRangeMax);
  LOAD(lnbFreq);
  LOAD(device);
  LOAD(antenna);
  LOAD(sampRate);
  LOAD(strategy);
  LOAD(partitioning);
  LOAD(palette);

  for (unsigned int i = 0; i < conf.getFieldCount(); ++i)
    if (conf.getFieldByIndex(i).name().substr(0, 5) == "gain.") {
      gains[conf.getFieldByIndex(i).name()] =
          conf.get(
            conf.getFieldByIndex(i).name(),
            static_cast<SUFLOAT>(0));
    }
}

Suscan::Object &&
PanoramicDialogConfig::serialize(void)
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);

  obj.setClass("PanoramicDialogConfig");

  STORE(fullRange);
  STORE(rangeMin);
  STORE(rangeMax);
  STORE(panRangeMin);
  STORE(panRangeMax);
  STORE(lnbFreq);
  STORE(device);
  STORE(antenna);
  STORE(sampRate);
  STORE(strategy);
  STORE(partitioning);
  STORE(palette);

  for (auto p : gains)
    obj.set(p.first, p.second);

  return persist(obj);
}

bool
PanoramicDialogConfig::hasGain(
    std::string const &dev,
    std::string const &name) const
{
  std::string fullName = "gain." + dev + "." + name;

  return gains.find(fullName) != gains.cend();
}

SUFLOAT
PanoramicDialogConfig::getGain(
    std::string const &dev,
    std::string const &name) const
{
  std::string fullName = "gain." + dev + "." + name;

  if (gains.find(fullName) == gains.cend())
    return 0;

  return gains.at(fullName);
}

void
PanoramicDialogConfig::setGain(
    std::string const &dev,
    std::string const &name,
    SUFLOAT val)
{
  std::string fullName = "gain." + dev + "." + name;

  gains[fullName] = val;
}

///////////////////////////// PanoramicDialog //////////////////////////////////
PanoramicDialog::PanoramicDialog(QWidget *parent) :
  QDialog(parent),
  m_ui(new Ui::PanoramicDialog)
{
  m_ui->setupUi(static_cast<QDialog *>(this));

  assertConfig();
  setWindowFlags(Qt::Window);
  m_ui->sampleRateSpin->setUnits("sps");

  m_ui->centerLabel->setFixedWidth(
        SuWidgetsHelpers::getWidgetTextWidth(
          m_ui->centerLabel,
          "XXX.XXXXXXXXX XHz"));

  m_ui->bwLabel->setFixedWidth(
        SuWidgetsHelpers::getWidgetTextWidth(
          m_ui->bwLabel,
          "XXX.XXXXXXXXX XHz"));

  m_ui->lnbDoubleSpinBox->setMinimum(-300e9);
  m_ui->lnbDoubleSpinBox->setMaximum(300e9);

  connectAll();
}

PanoramicDialog::~PanoramicDialog()
{
  if (m_noGainLabel != nullptr)
    m_noGainLabel->deleteLater();
  delete m_ui;
}

void
PanoramicDialog::setGuiConfig(GuiConfig const &cfg)
{
  if (m_waterfall == nullptr) {
    int index;

    if (cfg.useGLWaterfall) {
      // OpenGL waterfall
      m_waterfall = new GLWaterfall(this);
    } else {
      // Classic waterfall
      m_waterfall = new Waterfall(this);
    }

    m_ui->gridLayout->addWidget(m_waterfall, 2, 0, 2, 4);
    connectWaterfall();

    m_waterfall->setWaterfallSpan(30 * 1000); // 30 seconds
    m_waterfall->setFreqDragLocked(true);

    if (m_dialogConfig) {
      m_waterfall->setPandapterRange(
            m_dialogConfig->panRangeMin,
            m_dialogConfig->panRangeMax);
      m_waterfall->setWaterfallRange(
            m_dialogConfig->panRangeMin,
            m_dialogConfig->panRangeMax);
    }

    m_waterfall->setFftPlotColor(m_colorConfig.spectrumForeground);
    m_waterfall->setFftAxesColor(m_colorConfig.spectrumAxes);
    m_waterfall->setFftBgColor(m_colorConfig.spectrumBackground);
    m_waterfall->setFftTextColor(m_colorConfig.spectrumText);
    m_waterfall->setFilterBoxColor(m_colorConfig.filterBox);

    index = SigDiggerHelpers::instance()->getPaletteIndex(
        m_paletteGradient.toStdString());
    if (index >= 0)
      m_waterfall->setPalette(
          SigDiggerHelpers::instance()->getPalette(index)->getGradient());

    adjustRanges();
  }
}

void
PanoramicDialog::connectAll(void)
{
  connect(
        m_ui->deviceCombo,
        SIGNAL(activated(int)),
        this,
        SLOT(onDeviceChanged(void)));

  connect(
        m_ui->lnbDoubleSpinBox,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onLnbOffsetChanged(void)));

  connect(
        m_ui->sampleRateSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onSampleRateSpinChanged(void)));

  connect(
        m_ui->fullRangeCheck,
        SIGNAL(stateChanged(int)),
        this,
        SLOT(onFullRangeChanged(void)));

  connect(
        m_ui->rangeStartSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onFreqRangeChanged(void)));

  connect(
        m_ui->rangeEndSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onFreqRangeChanged(void)));

  connect(
        m_ui->scanButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onToggleScan(void)));

  connect(
        m_ui->resetButton,
        SIGNAL(clicked(bool)),
        this,
        SIGNAL(reset(void)));

  connect(
        m_ui->rttSpin,
        SIGNAL(valueChanged(int)),
        this,
        SIGNAL(frameSkipChanged(void)));

  connect(
        m_ui->relBwSlider,
        SIGNAL(valueChanged(int)),
        this,
        SIGNAL(relBandwidthChanged(void)));

  connect(
        m_ui->paletteCombo,
        SIGNAL(activated(int)),
        this,
        SLOT(onPaletteChanged(int)));

  connect(
        m_ui->allocationCombo,
        SIGNAL(activated(int)),
        this,
        SLOT(onBandPlanChanged(int)));

  connect(
        m_ui->walkStrategyCombo,
        SIGNAL(currentIndexChanged(int)),
        this,
        SLOT(onStrategyChanged(int)));

  connect(
        m_ui->partitioningCombo,
        SIGNAL(currentIndexChanged(int)),
        this,
        SLOT(onPartitioningChanged(int)));

  connect(
        m_ui->exportButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onExport(void)));
}

void
PanoramicDialog::connectWaterfall(void)
{
  connect(
        m_waterfall,
        SIGNAL(newFilterFreq(int, int)),
        this,
        SLOT(onNewBandwidth(int, int)));

  connect(
        m_waterfall,
        SIGNAL(newDemodFreq(qint64, qint64)),
        this,
        SLOT(onNewOffset()));

  connect(
        m_waterfall,
        SIGNAL(newZoomLevel(float)),
        this,
        SLOT(onNewZoomLevel(float)));

  connect(
        m_waterfall,
        SIGNAL(newFftCenterFreq(qint64)),
        this,
        SLOT(onNewFftCenterFreq(qint64)));

  connect(
        m_waterfall,
        SIGNAL(pandapterRangeChanged(float, float)),
        this,
        SLOT(onRangeChanged(float, float)));
}

// The following values are purely experimental
unsigned int
PanoramicDialog::preferredRttMs(Suscan::Source::Device const &dev)
{
  if (dev.getDriver() == "rtlsdr")
    return 5;
  else if (dev.getDriver() == "airspy")
    return 16;
  else if (dev.getDriver() == "hackrf")
    return 10;
  else if (dev.getDriver() == "uhd")
    return 2;

  return 0;
}

void
PanoramicDialog::refreshUi(void)
{
  bool empty = m_deviceMap.size() == 0;
  bool fullRange = m_ui->fullRangeCheck->isChecked();

  m_ui->deviceCombo->setEnabled(!m_running && !empty);
  m_ui->antennaCombo->setEnabled(!m_running && !empty &&
        m_ui->antennaCombo->count());
  m_ui->fullRangeCheck->setEnabled(!m_running && !empty);
  m_ui->rangeEndSpin->setEnabled(!m_running && !empty && !fullRange);
  m_ui->rangeStartSpin->setEnabled(!m_running && !empty && !fullRange);
  m_ui->lnbDoubleSpinBox->setEnabled(!m_running);
  m_ui->scanButton->setChecked(m_running);
  m_ui->sampleRateSpin->setEnabled(!m_running);
}

SUFREQ
PanoramicDialog::getLnbOffset(void) const
{
  return m_ui->lnbDoubleSpinBox->value();
}

SUFREQ
PanoramicDialog::getMinFreq(void) const
{
  return m_ui->rangeStartSpin->value();
}

SUFREQ
PanoramicDialog::getMaxFreq(void) const
{
  return m_ui->rangeEndSpin->value();
}

void
PanoramicDialog::getZoomRange(qint64 &min, qint64 &max, bool &noHop) const
{
  bool leftBorder = false, rightBorder = false;
  qint64 fc =
        m_waterfall->getCenterFreq()
        + m_waterfall->getFftCenterFreq();
  qint64 span = static_cast<qint64>(m_waterfall->getSpanFreq());

  min = fc - span / 2;
  max = fc + span / 2;

  if (min <= getMinFreq()) {
    leftBorder = true;
    min = static_cast<qint64>(getMinFreq());
  }

  if (max >= getMaxFreq()) {
    rightBorder = true;
    max = static_cast<qint64>(getMaxFreq());
  }

  if (rightBorder || leftBorder) {
    if (leftBorder && !rightBorder) {
      max = min + span;
    } else if (rightBorder && !leftBorder) {
      min = max - span;
    }
  }

  noHop = max - min <= m_minBwForZoom * getRelBw();
}

void
PanoramicDialog::setRunning(bool running)
{
  if (running && !m_running) {
    m_frames = 0;
    m_ui->framesLabel->setText("0");
  } else if (!running && m_running) {
    m_ui->sampleRateSpin->setValue(m_dialogConfig->sampRate);
  }

  if (m_waterfall)
    m_waterfall->setRunningState(running);

  m_running = running;
  refreshUi();
}

QString
PanoramicDialog::getAntenna(void) const
{
  return m_ui->antennaCombo->currentText();
}

QString
PanoramicDialog::getStrategy(void) const
{
  return m_ui->walkStrategyCombo->currentText();
}

QString
PanoramicDialog::getPartitioning(void) const
{
  return m_ui->partitioningCombo->currentText();
}

float
PanoramicDialog::getGain(QString const &gain) const
{
  for (auto p : m_gainControls)
    if (p->getName() == gain.toStdString())
      return p->getGain();

  return 0;
}

void
PanoramicDialog::setBannedDevice(QString const &desc)
{
  m_bannedDevice = desc;
}

void
PanoramicDialog::feed(
    qint64 freqStart,
    qint64 freqEnd,
    float *data,
    size_t size)
{
  if (m_freqStart != freqStart || m_freqEnd != freqEnd) {
    m_freqStart = freqStart;
    m_freqEnd   = freqEnd;
  }

  m_saved.set(
        static_cast<qint64>(freqStart),
        static_cast<qint64>(freqEnd),
        data,
        size);

  m_ui->exportButton->setEnabled(true);
  m_waterfall->setNewPartialFftData(data, static_cast<int>(size),
      freqStart, freqEnd);

  ++m_frames;
  redrawMeasures();
}

void
PanoramicDialog::setColors(ColorConfig const &cfg)
{
  m_colorConfig = cfg;

  if (m_waterfall) {
    m_waterfall->setFftPlotColor(cfg.spectrumForeground);
    m_waterfall->setFftAxesColor(cfg.spectrumAxes);
    m_waterfall->setFftBgColor(cfg.spectrumBackground);
    m_waterfall->setFftTextColor(cfg.spectrumText);
    m_waterfall->setFilterBoxColor(cfg.filterBox);
  }
}

void
PanoramicDialog::setPaletteGradient(QString const &name)
{
  int index = SigDiggerHelpers::instance()->getPaletteIndex(name.toStdString());
  m_paletteGradient = name;

  if (index >= 0) {
    m_ui->paletteCombo->setCurrentIndex(index);

    if (m_waterfall)
      m_waterfall->setPalette(
          SigDiggerHelpers::instance()->getPalette(index)->getGradient());
  }
}

SUFLOAT
PanoramicDialog::getPreferredSampleRate(void) const
{
  return m_ui->sampleRateSpin->value();
}

void
PanoramicDialog::setMinBwForZoom(quint64 bw)
{
  m_minBwForZoom = bw;
  m_ui->sampleRateSpin->setValue(static_cast<int>(bw));
}

void
PanoramicDialog::populateDeviceCombo(void)
{
  Suscan::Singleton *sus = Suscan::Singleton::get_instance();

  m_ui->deviceCombo->clear();
  m_deviceMap.clear();

  for (auto i = sus->getFirstDevice(); i != sus->getLastDevice(); ++i) {
    if (i->getMaxFreq() > 0 && i->isAvailable()) {
      std::string name = i->getDesc();
      m_deviceMap[name] = *i;
      m_ui->deviceCombo->addItem(QString::fromStdString(name));
    }
  }

  if (m_deviceMap.size() > 0)
    onDeviceChanged();

  refreshUi();
}

bool
PanoramicDialog::getSelectedDevice(Suscan::Source::Device &dev) const
{
  std::string name = m_ui->deviceCombo->currentText().toStdString();
  auto p = m_deviceMap.find(name);

  if (p != m_deviceMap.cend()) {
    dev = p->second;
    return true;
  }

  return false;
}

void
PanoramicDialog::adjustRanges(void)
{
  // swap min and max if reversed
  if (m_ui->rangeStartSpin->value() >
      m_ui->rangeEndSpin->value()) {
    auto val = m_ui->rangeStartSpin->value();
    m_ui->rangeStartSpin->setValue(
          m_ui->rangeEndSpin->value());
    m_ui->rangeEndSpin->setValue(val);
  }

  if (m_waterfall) {
    SUFREQ minFreq, maxFreq, bw, demodBw;

    minFreq = m_ui->rangeStartSpin->value();
    maxFreq = m_ui->rangeEndSpin->value();
    bw = maxFreq - minFreq;

    m_waterfall->setFreqUnits(
          getFrequencyUnits(
            static_cast<qint64>(maxFreq)));

    m_waterfall->setSpanFreq(static_cast<qint64>(maxFreq - minFreq));
    m_waterfall->setSampleRate(static_cast<qint64>(maxFreq - minFreq));
    m_waterfall->setCenterFreq(static_cast<qint64>(maxFreq + minFreq) / 2);
    m_waterfall->resetHorizontalZoom();
    m_waterfall->clearPartialFftData();

    demodBw = bw / 20;
    if (demodBw > 4000000000)
      demodBw = 4000000000;

    m_waterfall->setDemodRanges(-bw / 2, 0, 0, bw / 2, true);
    m_waterfall->setHiLowCutFrequencies(-demodBw / 2, demodBw / 2);
  }
}

bool
PanoramicDialog::invalidRange(void) const
{
  return fabs(
        m_ui->rangeEndSpin->value() - m_ui->rangeStartSpin->value()) < 1;
}

int
PanoramicDialog::getFrequencyUnits(qint64 freq)
{
  if (freq < 0)
    freq = -freq;

  if (freq < 1000)
    return 1;

  if (freq < 1000000)
    return 1000;

  if (freq < 1000000000)
    return 1000000;

  return 1000000000;
}


void
PanoramicDialog::setRanges(Suscan::Source::Device const &dev)
{
  SUFREQ minFreq = dev.getMinFreq() + getLnbOffset();
  SUFREQ maxFreq = dev.getMaxFreq() + getLnbOffset();

  // Prevents Waterfall frequencies from overflowing.

  m_ui->rangeStartSpin->setMinimum(minFreq);
  m_ui->rangeStartSpin->setMaximum(maxFreq);
  m_ui->rangeEndSpin->setMinimum(minFreq);
  m_ui->rangeEndSpin->setMaximum(maxFreq);

  if (invalidRange() || m_ui->fullRangeCheck->isChecked()) {
    m_ui->rangeStartSpin->setValue(minFreq);
    m_ui->rangeEndSpin->setValue(maxFreq);
  }

  adjustRanges();
}

void
PanoramicDialog::saveConfig(void)
{
  Suscan::Source::Device dev;
  if (getSelectedDevice(dev)) {
    m_dialogConfig->device = dev.getDesc();
    m_dialogConfig->antenna = m_ui->antennaCombo->currentText().toStdString();
  }

  m_dialogConfig->lnbFreq = m_ui->lnbDoubleSpinBox->value();
  m_dialogConfig->palette = m_paletteGradient.toStdString();
  m_dialogConfig->rangeMin = m_ui->rangeStartSpin->value();
  m_dialogConfig->rangeMax = m_ui->rangeEndSpin->value();

  m_dialogConfig->strategy =
      m_ui->walkStrategyCombo->currentText().toStdString();

  m_dialogConfig->partitioning =
      m_ui->partitioningCombo->currentText().toStdString();

  m_dialogConfig->fullRange = m_ui->fullRangeCheck->isChecked();
}

FrequencyBand
PanoramicDialog::deserializeFrequencyBand(Suscan::Object const &obj)
{
  FrequencyBand band;

  band.min = static_cast<qint64>(obj.get("min", 0.f));
  band.max = static_cast<qint64>(obj.get("max", 0.f));
  band.primary = obj.get("primary", std::string());
  band.secondary = obj.get("secondary", std::string());
  band.footnotes = obj.get("footnotes", std::string());

  band.color.setNamedColor(
        QString::fromStdString(obj.get("color", std::string("#1f1f1f"))));

  return band;
}

void
PanoramicDialog::deserializeFATs(void)
{
  if (m_FATs.size() == 0) {
    Suscan::Singleton *sus = Suscan::Singleton::get_instance();
    Suscan::Object bands;
    unsigned int i, count, ndx = 0;

    for (auto p = sus->getFirstFAT();
         p != sus->getLastFAT();
         p++) {
      m_FATs.resize(ndx + 1);
      m_FATs[ndx] = new FrequencyAllocationTable(p->getField("name").value());
      bands = p->getField("bands");

      SU_ATTEMPT(bands.getType() == SUSCAN_OBJECT_TYPE_SET);

      count = bands.length();

      for (i = 0; i < count; ++i) {
        try {
          m_FATs[ndx]->pushBand(deserializeFrequencyBand(bands[i]));
        } catch (Suscan::Exception &) {
        }
      }

      ++ndx;
    }
  }

  if (m_ui->allocationCombo->count() == 0) {
    m_ui->allocationCombo->insertItem(
          0,
          "(No bandplan)",
          QVariant::fromValue(-1));

    for (unsigned i = 0; i < m_FATs.size(); ++i)
      m_ui->allocationCombo->insertItem(
          static_cast<int>(i + 1),
          QString::fromStdString(m_FATs[i]->getName()),
          QVariant::fromValue(static_cast<int>(i)));
  }
}

void
PanoramicDialog::run(void)
{
  populateDeviceCombo();
  deserializeFATs();
  exec();
  saveConfig();
  m_ui->scanButton->setChecked(false);
  onToggleScan();
  emit stop();
}

void
PanoramicDialog::redrawMeasures(void)
{
  m_demodFreq = static_cast<qint64>(
        m_waterfall->getFilterOffset() +
        m_waterfall->getCenterFreq());

  m_ui->centerLabel->setText(
        SuWidgetsHelpers::formatQuantity(
          static_cast<qreal>(m_demodFreq),
          6,
          "Hz"));

  m_ui->bwLabel->setText(
        SuWidgetsHelpers::formatQuantity(
          static_cast<qreal>(m_waterfall->getFilterBw()),
          6,
          "Hz"));

  m_ui->framesLabel->setText(QString::number(m_frames));
}

unsigned int
PanoramicDialog::getRttMs(void) const
{
  return static_cast<unsigned int>(m_ui->rttSpin->value());
}

float
PanoramicDialog::getRelBw(void) const
{
  return m_ui->relBwSlider->value() / 100.f;
}

DeviceGain *
PanoramicDialog::lookupGain(std::string const &name)
{
  // Why is this? Use a map instead.
  for (auto p = m_gainControls.begin();
       p != m_gainControls.end();
       ++p) {
    if ((*p)->getName() == name)
      return *p;
  }

  return nullptr;
}

void
PanoramicDialog::clearGains(void)
{
  int i, len;

  len = static_cast<int>(m_gainControls.size());

  if (len == 0) {
    QLayoutItem *item = m_ui->gainGridLayout->takeAt(0);
    delete item;

    if (m_noGainLabel != nullptr) {
      m_noGainLabel->deleteLater();
      m_noGainLabel = nullptr;
    }
  } else {
    for (i = 0; i < len; ++i) {
      QLayoutItem *item = m_ui->gainGridLayout->takeAt(0);
      if (item != nullptr)
        delete item;

      // This is what C++ is for.
      m_gainControls[static_cast<unsigned long>(i)]->setVisible(false);
      m_gainControls[static_cast<unsigned long>(i)]->deleteLater();
    }

    QLayoutItem *item = m_ui->gainGridLayout->takeAt(0);
    if (item != nullptr)
      delete item;

    m_gainControls.clear();
  }
}

void
PanoramicDialog::refreshGains(Suscan::Source::Device &device)
{
  DeviceGain *gain = nullptr;

  clearGains();

  for (auto p = device.getFirstGain();
       p != device.getLastGain();
       ++p) {
    gain = new DeviceGain(nullptr, *p);
    m_gainControls.push_back(gain);
    m_ui->gainGridLayout->addWidget(
          gain,
          static_cast<int>(m_gainControls.size() - 1),
          0,
          1,
          1);

    connect(
          gain,
          SIGNAL(gainChanged(QString, float)),
          this,
          SLOT(onGainChanged(QString, float)));

    if (m_dialogConfig->hasGain(device.getDriver(), p->getName()))
      gain->setGain(m_dialogConfig->getGain(device.getDriver(), p->getName()));
    else
      gain->setGain(p->getDefault());
  }

  if (m_gainControls.size() == 0) {
    m_ui->gainGridLayout->addWidget(
        m_noGainLabel = new QLabel("(device has no gains)"),
        0,
        0,
        Qt::AlignCenter | Qt::AlignVCenter);
  } else {
    m_ui->gainGridLayout->addItem(
          new QSpacerItem(40, 20, QSizePolicy::Minimum, QSizePolicy::Minimum),
          static_cast<int>(m_gainControls.size()),
          0);
  }
}

// Overriden methods
Suscan::Serializable *
PanoramicDialog::allocConfig(void)
{
  return m_dialogConfig = new PanoramicDialogConfig();
}

void
PanoramicDialog::applyConfig(void)
{
  SigDiggerHelpers::instance()->populatePaletteCombo(m_ui->paletteCombo);

  setPaletteGradient(QString::fromStdString(m_dialogConfig->palette));
  m_ui->lnbDoubleSpinBox->setValue(
        static_cast<SUFREQ>(m_dialogConfig->lnbFreq));
  m_ui->rangeStartSpin->setValue(m_dialogConfig->rangeMin);
  m_ui->rangeEndSpin->setValue(m_dialogConfig->rangeMax);
  m_ui->fullRangeCheck->setChecked(m_dialogConfig->fullRange);
  m_ui->sampleRateSpin->setValue(m_dialogConfig->sampRate);
  m_ui->walkStrategyCombo->setCurrentText(QString::fromStdString(
        m_dialogConfig->strategy));
  m_ui->partitioningCombo->setCurrentText(QString::fromStdString(
        m_dialogConfig->partitioning));
  m_ui->deviceCombo->setCurrentText(QString::fromStdString(
        m_dialogConfig->device));
  onDeviceChanged();
  m_ui->antennaCombo->setCurrentText(QString::fromStdString(
        m_dialogConfig->antenna));

  if (m_waterfall) {
    m_waterfall->setPandapterRange(
          m_dialogConfig->panRangeMin,
          m_dialogConfig->panRangeMax);
    m_waterfall->setWaterfallRange(
          m_dialogConfig->panRangeMin,
          m_dialogConfig->panRangeMax);
  }
}

////////////////////////////// Slots //////////////////////////////////////

void
PanoramicDialog::onDeviceChanged(void)
{
  Suscan::Source::Device dev;

  if (getSelectedDevice(dev)) {
    unsigned int rtt = preferredRttMs(dev);
    setRanges(dev);
    refreshGains(dev);
    if (rtt != 0)
      m_ui->rttSpin->setValue(static_cast<int>(rtt));
    if (m_ui->fullRangeCheck->isChecked()) {
      m_ui->rangeStartSpin->setValue(dev.getMinFreq() + getLnbOffset());
      m_ui->rangeEndSpin->setValue(dev.getMaxFreq() + getLnbOffset());
    }

    int curAntennaIndex = m_ui->antennaCombo->currentIndex();
    m_ui->antennaCombo->clear();
    for (auto i = dev.getFirstAntenna(); i != dev.getLastAntenna(); i++) {
      m_ui->antennaCombo->addItem(QString::fromStdString(*i));
    }
    int antennaCount = m_ui->antennaCombo->count();
    m_ui->antennaCombo->setEnabled(antennaCount > 0);
    if (curAntennaIndex < antennaCount && curAntennaIndex >= 0)
      m_ui->antennaCombo->setCurrentIndex(curAntennaIndex);
  } else {
    clearGains();
  }

  adjustRanges();
}

void
PanoramicDialog::onFullRangeChanged(void)
{
  Suscan::Source::Device dev;
  bool checked = m_ui->fullRangeCheck->isChecked();

  if (getSelectedDevice(dev)) {
    if (checked) {
      m_ui->rangeStartSpin->setValue(dev.getMinFreq() + getLnbOffset());
      m_ui->rangeEndSpin->setValue(dev.getMaxFreq() + getLnbOffset());
    }
  }

  refreshUi();
}

void
PanoramicDialog::onFreqRangeChanged(void)
{
  adjustRanges();
}

void
PanoramicDialog::onToggleScan(void)
{
  if (m_ui->scanButton->isChecked()) {
    Suscan::Source::Device dev;
    getSelectedDevice(dev);

    if (m_bannedDevice.length() > 0
        && dev.getDesc() == m_bannedDevice.toStdString()) {
      (void)  QMessageBox::critical(
            this,
            "Panoramic spectrum error error",
            "Scan cannot start because the selected device is in use by the main window.",
            QMessageBox::Ok);
      m_ui->scanButton->setChecked(false);
    } else {
      // first clear any references to old scanner PSD data that will be freed on startup
      if (m_waterfall)
        m_waterfall->clearPartialFftData();
      emit start();
    }
  } else {
    emit stop();
  }

  m_ui->scanButton->setText(
        m_ui->scanButton->isChecked()
        ? "Stop"
        : "Start scan");

}

void
PanoramicDialog::onNewZoomLevel(float)
{
  qint64 min, max;

  getZoomRange(min, max, m_fixedFreqMode);
  m_currBw = max - min;

  if (m_running)
    emit detailChanged(min, max, m_fixedFreqMode);
}

void
PanoramicDialog::onRangeChanged(float min, float max)
{
  m_dialogConfig->panRangeMin = min;
  m_dialogConfig->panRangeMax = max;
  m_waterfall->setWaterfallRange(min, max);
}

void
PanoramicDialog::onNewOffset(void)
{
  redrawMeasures();
}

void
PanoramicDialog::onNewBandwidth(int, int)
{
  redrawMeasures();
}

void
PanoramicDialog::onNewFftCenterFreq(qint64 freq)
{
  if (!m_running)
    return;

  // FftCenterFreq is an offset from CenterFreq
  freq += m_waterfall->getCenterFreq();

  qint64 span = m_currBw;
  qint64 min = freq - span / 2;
  qint64 max = freq + span / 2;
  bool leftBorder = false;
  bool rightBorder = false;

  if (min <= getMinFreq()) {
    leftBorder = true;
    min = static_cast<qint64>(getMinFreq());
  }

  if (max >= getMaxFreq()) {
    rightBorder = true;
    max = static_cast<qint64>(getMaxFreq());
  }

  if (rightBorder || leftBorder) {
    if (leftBorder && !rightBorder) {
      max = min + span;
    } else if (rightBorder && !leftBorder) {
      min = max - span;
    }
  }

  emit detailChanged(min, max, m_fixedFreqMode);
}

void
PanoramicDialog::onPaletteChanged(int)
{
  setPaletteGradient(m_ui->paletteCombo->currentText());
}

void
PanoramicDialog::onStrategyChanged(int)
{
  emit strategyChanged(m_ui->walkStrategyCombo->currentText());
}

void
PanoramicDialog::onPartitioningChanged(int)
{
  emit partitioningChanged(m_ui->partitioningCombo->currentText());
}

void
PanoramicDialog::onLnbOffsetChanged(void)
{
  Suscan::Source::Device dev;

  if (getSelectedDevice(dev))
    setRanges(dev);
}

void
PanoramicDialog::onExport(void)
{
  bool done = false;

  do {
    QFileDialog dialog(this);

    dialog.setFileMode(QFileDialog::FileMode::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setWindowTitle(QString("Save panoramic spectrum"));
    dialog.setNameFilter(QString("MATLAB/Octave file (*.m)"));

    if (dialog.exec()) {
      QString path = dialog.selectedFiles().first();

        if (!m_saved.exportToFile(path)) {
          QMessageBox::warning(
                this,
                "Cannot open file",
                "Cannote save file in the specified location. Please choose "
                "a different location and try again.",
                QMessageBox::Ok);
        } else {
          done = true;
        }
    } else {
      done = true;
    }
  } while (!done);
}

void
PanoramicDialog::onBandPlanChanged(int)
{
  int val = m_ui->allocationCombo->currentData().value<int>();

  if (!m_waterfall)
    return;

  if (m_currentFAT.size() > 0)
    m_waterfall->removeFAT(m_currentFAT);

  if (val >= 0) {
    m_waterfall->setFATsVisible(true);
    m_waterfall->pushFAT(m_FATs[static_cast<unsigned>(val)]);
    m_currentFAT = m_FATs[static_cast<unsigned>(val)]->getName();
  } else {
    m_waterfall->setFATsVisible(false);
    m_currentFAT = "";
  }
}

void
PanoramicDialog::onGainChanged(QString name, float val)
{
  Suscan::Source::Device dev;

  if (getSelectedDevice(dev))
    m_dialogConfig->setGain(dev.getDriver(), name.toStdString(), val);

  emit gainChanged(name, val);
}

void
PanoramicDialog::onSampleRateSpinChanged(void)
{
  if (!m_running)
    m_dialogConfig->sampRate = static_cast<int>(
        m_ui->sampleRateSpin->value());
}
