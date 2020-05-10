#-------------------------------------------------
#
# Project created by QtCreator 2011-07-07T08:39:24
#
#-------------------------------------------------

QT       += network multimedia
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets sql
CONFIG   += thread
#CONFIG   += console

TARGET = wsjtx
VERSION = "Not for Release"
TEMPLATE = app
DEFINES = QT5
QMAKE_CXXFLAGS += -std=c++11
DEFINES += PROJECT_MANUAL="'\"http://www.physics.princeton.edu/pulsar/K1JT/wsjtx-doc/wsjtx-main.html\"'"

isEmpty (DESTDIR) {
DESTDIR = ../wsjtx_exp_install
}

isEmpty (HAMLIB_DIR) {
HAMLIB_DIR = ../../hamlib3/mingw32
}

isEmpty (FFTW3_DIR) {
FFTW3_DIR = .
}

F90 = gfortran
gfortran.output = ${QMAKE_FILE_BASE}.o
gfortran.commands = $$F90 -c -O2 -o ${QMAKE_FILE_OUT} ${QMAKE_FILE_NAME}
gfortran.input = F90_SOURCES
QMAKE_EXTRA_COMPILERS += gfortran

win32 {
DEFINES += WIN32
QT += axcontainer
TYPELIBS = $$system(dumpcpp -getfile {4FE359C5-A58F-459D-BE95-CA559FB4F270})
}

unix {
DEFINES += UNIX
}

#
# Order matters here as the link is in this order so referrers need to be after referred
#
include(models/models.pri)
include(validators/validators.pri)
include(item_delegates/item_delegates.pri)
include(logbook/logbook.pri)
include(widgets/widgets.pri)
include(Decoder/decodedtext.pri)

SOURCES += \
  Radio.cpp NetworkServerLookup.cpp revision_utils.cpp \
  Transceiver.cpp TransceiverBase.cpp TransceiverFactory.cpp \
  PollingTransceiver.cpp EmulateSplitTransceiver.cpp \
  HRDTransceiver.cpp DXLabSuiteCommanderTransceiver.cpp \
  HamlibTransceiver.cpp FrequencyLineEdit.cpp \
  Configuration.cpp psk_reporter.cpp AudioDevice.cpp \
  Modulator.cpp Detector.cpp \
  getfile.cpp soundout.cpp soundin.cpp \
  WFPalette.cpp \
  WsprTxScheduler.cpp \
  main.cpp decodedtext.cpp wsprnet.cpp \
  WSPRBandHopping.cpp MessageAggregator.cpp SampleDownloader.cpp qt_helpers.cpp\
  MultiSettings.cpp PhaseEqualizationDialog.cpp \
  EqualizationToolsDialog.cpp \
  LotWUsers.cpp TraceFile.cpp

HEADERS  += qt_helpers.hpp qt_db_helpers.hpp \
  pimpl_h.hpp pimpl_impl.hpp \
  Radio.hpp NetworkServerLookup.hpp revision_utils.hpp \
  soundin.h soundout.h \
  WFPalette.hpp getfile.h decodedtext.h \
  commons.h sleep.h \
  AudioDevice.hpp Detector.hpp Modulator.hpp psk_reporter.h \
  Transceiver.hpp TransceiverBase.hpp TransceiverFactory.hpp PollingTransceiver.hpp \
  EmulateSplitTransceiver.hpp DXLabSuiteCommanderTransceiver.hpp HamlibTransceiver.hpp \
  Configuration.hpp wsprnet.h \
  WSPRBandHopping.hpp \
  WsprTxScheduler.h SampleDownloader.hpp MultiSettings.hpp PhaseEqualizationDialog.hpp \
  EqualizationToolsDialog.hpp \
  LotWUsers.h TraceFile.hpp

INCLUDEPATH += qmake_only

win32 {
SOURCES += killbyname.cpp OmniRigTransceiver.cpp
HEADERS += OmniRigTransceiver.hpp
}

FORMS    += \
  Configuration.ui \
  wf_palette_design_dialog.ui

RC_FILE = wsjtx.rc
RESOURCES = wsjtx.qrc

unix {
LIBS += -L lib -ljt9
LIBS += -lhamlib
LIBS += -lfftw3f $$system($$F90 -print-file-name=libgfortran.so)
}

win32 {
INCLUDEPATH += $${HAMLIB_DIR}/include
INCLUDEPATH += C:\JTSDK\wsjtx_exp\build\Release
INCLUDEPATH += C:\JTSDK\hamlib3\include
INCLUDEPATH += C:\JTSDK\qt5\5.2.1\mingw48_32\include\QtSerialPort

LIBS += -L$${HAMLIB_DIR}/lib -lhamlib
LIBS += -L./lib -lastro -ljt9
LIBS += -L$${FFTW3_DIR} -lfftw3f-3
LIBS += -lws2_32
LIBS += $$system($$F90 -print-file-name=libgfortran.a)
}
