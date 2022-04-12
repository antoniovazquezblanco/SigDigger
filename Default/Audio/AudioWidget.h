//
//    Default/Audio/AudioWidget.h: description
//    Copyright (C) 2022 Gonzalo José Carracedo Carballal
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
#ifndef AUDIOWIDGET_H
#define AUDIOWIDGET_H

#include <ToolWidgetFactory.h>
#include <ColorConfig.h>
#include <AudioFileSaver.h>

namespace Ui {
  class AudioPanel;
}

namespace SigDigger {
  class AudioWidgetFactory;
  class FrequencyCorrectionDialog;

  class AudioWidgetConfig : public Suscan::Serializable {
  public:
    bool enabled = false;
    bool collapsed = false;
    std::string demod;
    std::string savePath;
    unsigned int rate   = 44100;
    SUFLOAT cutOff      = 15000;
    SUFLOAT volume      = -6;

    bool squelch        = false;
    SUFLOAT amSquelch   = .1f;
    SUFLOAT ssbSquelch  = 1e-3f;

    bool tleCorrection  = false;
    bool isSatellite    = false;
    std::string satName = "ISS (ZARYA)";
    std::string tleData = "";

    // Overriden methods
    void deserialize(Suscan::Object const &conf) override;
    Suscan::Object &&serialize() override;
  };

  class AudioWidget : public ToolWidget
  {
    Q_OBJECT

    AudioWidgetConfig *panelConfig = nullptr;

    // Data
    SUFLOAT        bandwidth  = 200000;
    SUFREQ         demodFreq  = 0;
    bool           isRealTime = false;
    struct timeval timeStamp  = {0, 0};

    // UI members
    Ui::AudioPanel *ui = nullptr;
    ColorConfig colorConfig;
    FrequencyCorrectionDialog *fcDialog = nullptr;
    bool audioAllowed = true;

    // Private methods
    void connectAll();
    void populateRates();
    void refreshUi();
    void notifyOrbitReport(Suscan::OrbitReport const &);
    void notifyDisableCorrection();
    void applySourceInfo(Suscan::AnalyzerSourceInfo const &info);

    // Private settes
    void setBandwidth(SUFLOAT);
    void setDemodFreq(qint64);
    void setRealTime(bool);
    void setEnabled(bool);
    void setDemod(enum AudioDemod);
    void setSampleRate(unsigned int);
    void setTimeStamp(struct timeval const &);
    void setTimeLimits(
        struct timeval const &start,
        struct timeval const &end);
    void resetTimeStamp(struct timeval const &);
    void setCutOff(SUFLOAT);
    void setVolume(SUFLOAT);
    void setQth(xyz_t const &);
    void setMuted(bool);
    void setColorConfig(ColorConfig const &);
    void setSquelchEnabled(bool);
    void setSquelchLevel(SUFLOAT);
    void setDiskUsage(qreal);

    // Recorder state setters
    void refreshDiskUsage();
    void setRecordSavePath(std::string const &);
    void setSaveEnabled(bool enabled);
    void setCaptureSize(quint64);
    void setIORate(qreal);
    void setRecordState(bool state);

    // Private getters
    SUFLOAT getBandwidth() const;
    bool getEnabled() const;
    bool shouldOpenAudio() const;
    enum AudioDemod getDemod() const;
    unsigned int getSampleRate() const;
    SUFLOAT getCutOff() const;
    SUFLOAT getVolume() const;
    bool    isMuted() const;
    SUFLOAT getMuteableVolume() const;
    bool    isCorrectionEnabled() const;
    bool    getSquelchEnabled() const;
    SUFLOAT getSquelchLevel() const;
    Suscan::Orbit getOrbit() const;
    bool getRecordState(void) const;
    std::string getRecordSavePath(void) const;

    // Private static members
    static AudioDemod strToDemod(std::string const &str);
    static std::string demodToStr(AudioDemod);

  public:
    AudioWidget(AudioWidgetFactory *, UIMediator *, QWidget *parent = nullptr);
    ~AudioWidget() override;

    // Configuration methods
    Suscan::Serializable *allocConfig() override;
    void applyConfig() override;
    bool event(QEvent *) override;

  public slots:
    /*void onDemodChanged();
    void onSampleRateChanged();
    void onFilterChanged();
    void onVolumeChanged();
    void onMuteToggled(bool);
    void onEnabledChanged();
    void onAcceptCorrectionSetting();

    void onChangeSavePath();
    void onRecordStartStop();
    void onToggleSquelch();
    void onSquelchLevelChanged();
    void onOpenDopplerSettings(); */
  };
}

#endif // AUDIOWIDGET_H
