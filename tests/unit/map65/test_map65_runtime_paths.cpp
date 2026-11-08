#include "runtime_paths.h"
#include "sample_rates.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <string>
#include <vector>

namespace
{
bool writeFile(QString const& path, QByteArray const& contents)
{
  QFile file {path};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(contents) == contents.size();
}

QByteArray readFile(QString const& path)
{
  QFile file {path};
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

class CurrentDirectoryGuard
{
public:
  explicit CurrentDirectoryGuard(QString const& path)
    : previous_ {QDir::currentPath()}
    , changed_ {QDir::setCurrent(path)}
  {
  }

  ~CurrentDirectoryGuard()
  {
    if (changed_) {
      QDir::setCurrent(previous_);
    }
  }

  bool changed() const { return changed_; }

private:
  QString previous_;
  bool changed_;
};
}

class TestMap65RuntimePaths final : public QObject
{
  Q_OBJECT

private slots:
  void sampleRateSettingsRespectAddressSpace()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings {dir.filePath("map65.ini"), QSettings::IniFormat};
    int const expected[][2] = {{0, 95238}, {1, 96000}, {2, 192000}, {3, 128000}, {4, 256000}};
    for (auto const& e : expected) {
      settings.setValue("FSam96000", e[0]);
      int const expectedCode = sizeof(void*) == 4 && e[0] == 4 ? 2 : e[0];
      QCOMPARE(readFSam96000(settings, 1), expectedCode);
      QCOMPARE(sampleRateForCode(e[0]), e[1]);
    }
    settings.setValue("FSam96000", 5);
    QCOMPARE(readFSam96000(settings, 1), 1);
    settings.setValue("FSam96000", "false");
    QCOMPARE(readFSam96000(settings, 1), 0);
  }

  void sampleRateCodeThatCannotBeAddressedFallsBack()
  {
    // 32-bit: 256 kHz would need more than the process can address.
    QVERIFY(!sampleRateCodeFitsAddressSpace(4, 4));
    QCOMPARE(usableSampleRateCode(4, 4), 2);
    for (int code : {0, 1, 2, 3}) {
      QVERIFY(sampleRateCodeFitsAddressSpace(code, 4));
      QCOMPARE(usableSampleRateCode(code, 4), code);
    }
    for (int code = 0; code <= kMaxSampleRateCode; ++code)
      QCOMPARE(usableSampleRateCode(code, 8), code);
  }

  void blankingPercentScalesWithRate()
  {
    // 50% of a half-symbol is 8900 samples at 96 kHz and scales with the rate.
    QCOMPARE(zapPercent(8915, 96000), 50.0);
    QVERIFY(qAbs(zapPercent(8915 * 8 / 3, 256000) - 50.0) < 0.1);
    QVERIFY(qAbs(zapPercent(8915 * 2, 192000) - 50.0) < 0.1);
    // ~20% blanking at 256 kHz must not read as 53%.
    QVERIFY(zapPercent(9500, 256000) < 21.0);
  }

  void freqSpanFitsSampledBandwidth()
  {
    QCOMPARE(maxFreqSpanKHz(96000), 90);
    QCOMPARE(maxFreqSpanKHz(95238), 90);
    QCOMPARE(maxFreqSpanKHz(128000), 125);
    QCOMPARE(maxFreqSpanKHz(192000), 190);
    QCOMPARE(maxFreqSpanKHz(256000), 250);
  }

  void reopeningSettingsKeepsAPendingRate()
  {
    int const shown = dialogSampleRateCode(1, 2);
    QCOMPARE(shown, 2);
    QCOMPARE(pendingSampleRateCode(1, shown), 2);   // OK with nothing touched
    QCOMPARE(pendingSampleRateCode(1, 1), -1);      // back to the running rate
    QCOMPARE(pendingSampleRateCode(1, 4), 4);       // a different rate
    QCOMPARE(pendingSampleRateCode(1, 0), 0);       // 95238 is not 96000
    QCOMPARE(pendingSampleRateCode(0, 1), 1);
    QCOMPARE(dialogSampleRateCode(1, -1), 1);
    QCOMPARE(dialogSampleRateCode(2, 0), 0);
  }

  void restartNoticeAppearsOnlyWhenThePendingRateChanges()
  {
    QVERIFY(restartNoticeNeeded(-1, 2));    // first choice
    QVERIFY(!restartNoticeNeeded(2, 2));    // OK again, nothing changed
    QVERIFY(restartNoticeNeeded(2, 4));     // a third rate
    QVERIFY(!restartNoticeNeeded(2, -1));   // back to the running rate
    QVERIFY(!restartNoticeNeeded(-1, -1));  // never changed
    QVERIFY(restartNoticeNeeded(-1, 0));    // 95238 is a real code
  }

  void inputListProbesOnlyAt128And256()
  {
    int probes = 0;
    auto yes = [&] { ++probes; return RateProbe::Supported; };
    auto no = [&] { ++probes; return RateProbe::NotSupported; };
    auto busy = [&] { ++probes; return RateProbe::Busy; };
    // In range at 128 kHz (accepts 96 and 192) but the device refuses 128.
    QCOMPARE(inputDeviceFit(128000, 2, 2, 96000, 192000, no), InputFit::RateNotSupported);
    QCOMPARE(inputDeviceFit(128000, 2, 2, 96000, 192000, yes), InputFit::Listed);
    QCOMPARE(inputDeviceFit(256000, 2, 2, 96000, 256000, busy), InputFit::Busy);
    QCOMPARE(probes, 3);
    // Other rates use the range alone and never ask the device.
    QCOMPARE(inputDeviceFit(96000, 2, 2, 44100, 192000, no), InputFit::Listed);
    QCOMPARE(inputDeviceFit(192000, 2, 2, 96000, 192000, busy), InputFit::Listed);
    QCOMPARE(probes, 3);
    // Out of range is out, without a probe.
    QCOMPARE(inputDeviceFit(256000, 2, 2, 44100, 192000, yes), InputFit::RateNotSupported);
    QCOMPARE(inputDeviceFit(96000, 2, 2, 192000, 192000, yes), InputFit::RateNotSupported);
    QCOMPARE(probes, 3);
  }

  void inputListAsksAtEveryRateWhenTheRangeIsUnknown()
  {
    // getDev leaves a range of 0-0 for an ALSA device a stream has open; the
    // probe (or the answers kept from before SoundIn opened it) decides.
    int probes = 0;
    auto yes = [&] { ++probes; return RateProbe::Supported; };
    auto no = [&] { ++probes; return RateProbe::NotSupported; };
    auto busy = [&] { ++probes; return RateProbe::Busy; };
    QCOMPARE(inputDeviceFit(96000, 2, 2, 0, 0, yes), InputFit::Listed);
    QCOMPARE(inputDeviceFit(192000, 2, 2, 0, 0, no), InputFit::RateNotSupported);
    QCOMPARE(inputDeviceFit(128000, 2, 2, 0, 0, no), InputFit::RateNotSupported);
    QCOMPARE(inputDeviceFit(96000, 2, 2, 0, 0, busy), InputFit::Busy);
    QCOMPARE(probes, 4);
    QCOMPARE(inputDeviceFit(96000, 4, 2, 0, 0, yes), InputFit::TooFewChannels);
    QCOMPARE(probes, 4);
  }

  void inputListNamesChannelsBeforeRate()
  {
    int probes = 0;
    auto yes = [&] { ++probes; return RateProbe::Supported; };
    // A 2-channel card with Xpol (4 channels) is short of channels at any
    // rate, including one it supports; a 1-channel microphone without Xpol too.
    QCOMPARE(inputDeviceFit(96000, 4, 2, 44100, 192000, yes), InputFit::TooFewChannels);
    QCOMPARE(inputDeviceFit(256000, 4, 2, 44100, 192000, yes), InputFit::TooFewChannels);
    QCOMPARE(inputDeviceFit(96000, 2, 1, 44100, 96000, yes), InputFit::TooFewChannels);
    QCOMPARE(inputDeviceFit(128000, 4, 16, 44100, 192000, yes), InputFit::Listed);
    QCOMPARE(probes, 1);
  }

  void deviceThatCantRunNowWaitsForTheRestart()
  {
    int pending = 99;
    // The new device runs at the running rate: it applies now, rate pending
    // or not.
    QCOMPARE(inputDeviceToApply(3, 7, true, false, -1, pending), 7);
    QCOMPARE(pending, -1);
    QCOMPARE(inputDeviceToApply(3, 7, true, true, -1, pending), 7);
    QCOMPARE(pending, -1);
    // Listed only for the pending rate: SoundIn keeps device 3; 7 waits.
    QCOMPARE(inputDeviceToApply(3, 7, false, true, -1, pending), 3);
    QCOMPARE(pending, 7);
    // Back to the running device while the rate is still pending.
    QCOMPARE(inputDeviceToApply(3, 3, true, true, 7, pending), 3);
    QCOMPARE(pending, -1);
    // The pending rate cancelled: the waiting device is listed for the
    // running rate again and applies now.
    QCOMPARE(inputDeviceToApply(3, 7, true, false, 7, pending), 7);
    QCOMPARE(pending, -1);
    // Nothing selected (network mode, empty list): keep what was waiting
    // while the rate is pending, drop it once nothing is.
    QCOMPARE(inputDeviceToApply(3, -1, true, true, 7, pending), 3);
    QCOMPARE(pending, 7);
    QCOMPARE(inputDeviceToApply(3, -1, true, false, 7, pending), 3);
    QCOMPARE(pending, -1);
  }

  void waitingInputDeviceIsSavedAndShown()
  {
    QCOMPARE(savedInputDevice(3, -1), 3);
    QCOMPARE(savedInputDevice(3, 7), 7);
    QCOMPARE(savedInputDevice(3, 0), 0);   // device 0 is a real device
    // The notice appears when a waiting device is new or changed.
    QVERIFY(restartNoticeNeeded(-1, 7));
    QVERIFY(!restartNoticeNeeded(7, 7));
    QVERIFY(restartNoticeNeeded(7, 5));
    QVERIFY(!restartNoticeNeeded(7, -1));
  }

  void savedDeviceIsFoundByNameWhenNumbersMove()
  {
    // This run numbers the devices one lower than the run that saved them (as
    // when pulse moved from 27 to 26 on a real PC): pulse was 3 and is now 2;
    // 3 is now an output-only device (empty name).
    std::vector<std::string> const now {"hw0", "lavrate", "pulse", "", "default"};
    auto nameOf = [&](int i) { return now[i]; };
    int const n = int(now.size());
    QCOMPARE(resolveSavedDevice(2, "pulse", n, nameOf), 2);     // number still right
    QCOMPARE(resolveSavedDevice(3, "pulse", n, nameOf), 2);     // moved: found by name
    QCOMPARE(resolveSavedDevice(-1, "pulse", n, nameOf), 2);    // saved while missing
    QCOMPARE(resolveSavedDevice(3, "USB (hw:2,0)", n, nameOf), -1);  // gone: open nothing
    QCOMPARE(resolveSavedDevice(9, "pulse", n, nameOf), 2);     // number out of range
    QCOMPARE(resolveSavedDevice(3, "", n, nameOf), 3);          // old map65.ini: number only
    // Two devices with the same name: the number decides if it matches,
    // otherwise nothing is chosen.
    std::vector<std::string> const twins {"USB Audio", "pulse", "USB Audio"};
    auto twinOf = [&](int i) { return twins[i]; };
    QCOMPARE(resolveSavedDevice(2, "USB Audio", 3, twinOf), 2);
    QCOMPARE(resolveSavedDevice(1, "USB Audio", 3, twinOf), -1);
  }

  void savedDeviceSurvivesACardNumberOrWindowsPrefixChange()
  {
    QCOMPARE(stableDeviceName("USB Audio: - (hw:2,0)"), std::string("USB Audio: - (hw:,0)"));
    QCOMPARE(stableDeviceName("USB Audio: - (plughw:10,1)"), std::string("USB Audio: - (plughw:,1)"));
    QCOMPARE(stableDeviceName("Microphone (2- USB Audio Device)"),
             std::string("Microphone (USB Audio Device)"));
    QCOMPARE(stableDeviceName("pulse"), std::string("pulse"));
    QCOMPARE(stableDeviceName("Odd (hw:x)"), std::string("Odd (hw:x)"));
    QCOMPARE(stableDeviceName("Name (12 kHz)"), std::string("Name (12 kHz)"));

    // The USB card was hw:2,0 and now comes up as hw:3,0.
    std::vector<std::string> const alsa {"HDA Intel: ALC1220 (hw:0,0)", "pulse",
                                         "USB Audio: - (hw:3,0)"};
    auto alsaOf = [&](int i) { return alsa[i]; };
    QCOMPARE(resolveSavedDevice(2, "USB Audio: - (hw:2,0)", 3, alsaOf), 2);
    QCOMPARE(resolveSavedDevice(7, "USB Audio: - (hw:2,0)", 3, alsaOf), 2);
    QCOMPARE(resolveSavedDevice(2, "Other USB: - (hw:2,0)", 3, alsaOf), -1);

    // The two endpoints of one card differ only in the PCM number. The card
    // number changed from 2 to 3 and the saved endpoint is (hw:2,1); the device
    // now at the saved PortAudio index is (hw:3,0), which must not be taken
    // for it.
    std::vector<std::string> const loop {"Loopback: PCM (hw:3,0)", "Loopback: PCM (hw:3,1)"};
    auto loopOf = [&](int i) { return loop[i]; };
    QCOMPARE(resolveSavedDevice(0, "Loopback: PCM (hw:2,1)", 2, loopOf), 1);
    QCOMPARE(resolveSavedDevice(0, "Loopback: PCM (hw:2,0)", 2, loopOf), 0);

    // Two identical cards that came back in the other order: the saved
    // number picks the one at that position, never a second choice.
    std::vector<std::string> const twins {"USB Audio: - (hw:1,0)", "USB Audio: - (hw:2,0)"};
    auto twinOf = [&](int i) { return twins[i]; };
    QCOMPARE(resolveSavedDevice(1, "USB Audio: - (hw:5,0)", 2, twinOf), 1);
    QCOMPARE(resolveSavedDevice(9, "USB Audio: - (hw:5,0)", 2, twinOf), -1);

    // Windows: the other identical device was unplugged, so the remaining one
    // lost its "2- ".
    std::vector<std::string> const win {"Microphone (USB Audio Device)", "Speakers (Realtek)"};
    auto winOf = [&](int i) { return win[i]; };
    QCOMPARE(resolveSavedDevice(4, "Microphone (2- USB Audio Device)", 2, winOf), 0);
  }

  void aSoundCardWaitsForTheRestartWhile95238Runs()
  {
    // Network input at 95238 Hz switching to a card: wait, whatever the device.
    QVERIFY(!inputRunsNow(true, 0, InputFit::Listed));
    // Network input with no rate change pending runs now.
    QVERIFY(inputRunsNow(false, 0, InputFit::Listed));
    // Network input that arrives at a different rate than the running one
    // (from a card, with a rate change pending) waits too.
    QVERIFY(!inputRunsNow(false, 1, InputFit::Listed, true));
    // At the other rates a card runs now only if the device fits.
    QVERIFY(inputRunsNow(true, 1, InputFit::Listed));
    QVERIFY(inputRunsNow(true, 4, InputFit::Listed));
    QVERIFY(!inputRunsNow(true, 2, InputFit::TooFewChannels));
    QVERIFY(!inputRunsNow(true, 2, InputFit::RateNotSupported));
    QVERIFY(!inputRunsNow(true, 2, InputFit::Busy));
  }

  void networkInputWaitsOnlyWhenItWouldArriveAtAnotherRate()
  {
    // networkInputAtOtherRate(networkChosen, ratePending, runningNetwork, stopped)
    QVERIFY(networkInputAtOtherRate(true, true, false, false));   // card -> network, rate pending
    QVERIFY(networkInputAtOtherRate(true, true, true, true));     // network, stopped since then
    QVERIFY(!networkInputAtOtherRate(true, true, true, false));   // network already running
    QVERIFY(!networkInputAtOtherRate(true, false, false, false)); // card -> network, no rate pending
    QVERIFY(!networkInputAtOtherRate(false, true, false, false)); // a card is chosen
  }

  void aSoundCardIniWithCode0StartsAt96k()
  {
    // A sound card can't capture at 95238 Hz; network input keeps code 0, so
    // switching back to network later is unaffected.
    QCOMPARE(startupSampleRateCode(false, 0), 1);
    QCOMPARE(startupSampleRateCode(true, 0), 0);
    for (int code = 1; code <= kMaxSampleRateCode; ++code) {
      QCOMPARE(startupSampleRateCode(false, code), code);
      QCOMPARE(startupSampleRateCode(true, code), code);
    }
  }

  void inputListFollowsTheSelectedRate()
  {
    QCOMPARE(inputListSampleRate(0), 96000);   // 95238 listed like 96 kHz
    QCOMPARE(inputListSampleRate(1), 96000);
    QCOMPARE(inputListSampleRate(2), 192000);
    QCOMPARE(inputListSampleRate(3), 128000);
    QCOMPARE(inputListSampleRate(4), 256000);
  }

  void deviceIsFoundByNumberNotByRow()
  {
    // The 96 kHz list, then the shorter 128 kHz list without device 2:
    // device 5 is row 2 in the first and row 1 in the second.
    int const at96[] = {1, 2, 5, 7};
    int const at128[] = {1, 5, 7};
    QCOMPARE(rowOfDevice(at96, 4, 5), 2);
    QCOMPARE(rowOfDevice(at128, 3, 5), 1);
    QCOMPARE(rowOfDevice(at128, 3, 2), -1);   // dropped: nothing selected
    QCOMPARE(rowOfDevice(at128, 0, 5), -1);   // empty list
  }

  void canonicalSourcePrecedesLegacyCandidates()
  {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QDir dir {root.path()};
    QVERIFY(dir.mkpath("app/resources"));
    QVERIFY(dir.mkpath("installed"));
    QVERIFY(dir.mkpath("writable"));
    QString const canonical = dir.absoluteFilePath("installed/eclipse.txt");
    QString const legacy = dir.absoluteFilePath("app/resources/eclipse.txt");
    QVERIFY(writeFile(canonical, "canonical"));
    QVERIFY(writeFile(legacy, "legacy"));

    QCOMPARE(map65RuntimeSourceFile(dir.absoluteFilePath("app"),
                                    dir.absoluteFilePath("installed"),
                                    dir.absoluteFilePath("writable"), "eclipse.txt"),
             QFileInfo {canonical}.absoluteFilePath());
  }

  void legacySourceRemainsAvailable()
  {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QDir dir {root.path()};
    QVERIFY(dir.mkpath("app/resources"));
    QVERIFY(dir.mkpath("installed"));
    QVERIFY(dir.mkpath("writable"));
    QString const legacy = dir.absoluteFilePath("app/resources/eclipse.txt");
    QVERIFY(writeFile(legacy, "legacy"));

    QCOMPARE(map65RuntimeSourceFile(dir.absoluteFilePath("app"),
                                    dir.absoluteFilePath("installed"),
                                    dir.absoluteFilePath("writable"), "eclipse.txt"),
             QFileInfo {legacy}.absoluteFilePath());
  }

  void writableEclipseCannotShadowPackagedData()
  {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QDir dir {root.path()};
    QVERIFY(dir.mkpath("app"));
    QVERIFY(dir.mkpath("installed"));
    QVERIFY(dir.mkpath("writable"));
    QString const canonical = dir.absoluteFilePath("installed/eclipse.txt");
    QString const stale = dir.absoluteFilePath("writable/eclipse.txt");
    QVERIFY(writeFile(canonical, "current package"));
    QVERIFY(writeFile(stale, "stale copy"));
    CurrentDirectoryGuard cwd {dir.absoluteFilePath("writable")};
    QVERIFY(cwd.changed());

    QCOMPARE(map65RuntimeSourceFile(dir.absoluteFilePath("app"),
                                    dir.absoluteFilePath("installed"),
                                    dir.absoluteFilePath("writable"), "eclipse.txt"),
             QFileInfo {canonical}.absoluteFilePath());
    QCOMPARE(readFile(stale), QByteArray {"stale copy"});
  }

  void writableDirectoryIsNotASourceFallback()
  {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QDir dir {root.path()};
    QVERIFY(dir.mkpath("app"));
    QVERIFY(dir.mkpath("installed"));
    QVERIFY(dir.mkpath("writable"));
    QString const stale = dir.absoluteFilePath("writable/unique-runtime-data.txt");
    QVERIFY(writeFile(stale, "stale copy"));
    CurrentDirectoryGuard cwd {dir.absoluteFilePath("writable")};
    QVERIFY(cwd.changed());

    QCOMPARE(map65RuntimeSourceFile(dir.absoluteFilePath("app"),
                                    dir.absoluteFilePath("installed"),
                                    dir.absoluteFilePath("writable"),
                                    "unique-runtime-data.txt"),
             dir.absoluteFilePath("installed/unique-runtime-data.txt"));
  }

  void writableDirectoryAliasIsNotASourceFallback()
  {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QDir dir {root.path()};
    QVERIFY(dir.mkpath("app"));
    QVERIFY(dir.mkpath("installed"));
    QVERIFY(dir.mkpath("writable"));
    QString const writable = dir.absoluteFilePath("writable");
    QString const writableAlias = dir.absoluteFilePath("writable-alias");
    if (!QFile::link(writable, writableAlias)) {
      QSKIP("Directory links are unavailable");
    }
    QString const stale = QDir {writable}.absoluteFilePath("unique-runtime-data.txt");
    QVERIFY(writeFile(stale, "stale copy"));
    QString const aliasedStale =
      QDir {writableAlias}.absoluteFilePath("unique-runtime-data.txt");
    if (QFileInfo {aliasedStale}.canonicalFilePath()
        != QFileInfo {stale}.canonicalFilePath()) {
      QSKIP("Traversable directory links are unavailable");
    }
    CurrentDirectoryGuard cwd {writable};
    QVERIFY(cwd.changed());

    QCOMPARE(map65RuntimeSourceFile(dir.absoluteFilePath("app"),
                                    dir.absoluteFilePath("installed"), writableAlias,
                                    "unique-runtime-data.txt"),
             dir.absoluteFilePath("installed/unique-runtime-data.txt"));
  }

  void seedsMissingWritableFile()
  {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QDir dir {root.path()};
    QVERIFY(dir.mkpath("app"));
    QVERIFY(dir.mkpath("installed"));
    QVERIFY(dir.mkpath("writable"));
    QVERIFY(writeFile(dir.absoluteFilePath("installed/CALL3.TXT"), "packaged calls"));

    QString const writable = ensureMap65RuntimeFile(dir.absoluteFilePath("app"),
                                                     dir.absoluteFilePath("installed"),
                                                     dir.absoluteFilePath("writable"),
                                                     "CALL3.TXT");
    QCOMPARE(writable, dir.absoluteFilePath("writable/CALL3.TXT"));
    QCOMPARE(readFile(writable), QByteArray {"packaged calls"});
  }

  void replacesEmptyWritableFile()
  {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QDir dir {root.path()};
    QVERIFY(dir.mkpath("app"));
    QVERIFY(dir.mkpath("installed"));
    QVERIFY(dir.mkpath("writable"));
    QString const writable = dir.absoluteFilePath("writable/CALL3.TXT");
    QVERIFY(writeFile(dir.absoluteFilePath("installed/CALL3.TXT"), "packaged calls"));
    QVERIFY(writeFile(writable, {}));

    QCOMPARE(ensureMap65RuntimeFile(dir.absoluteFilePath("app"),
                                    dir.absoluteFilePath("installed"),
                                    dir.absoluteFilePath("writable"), "CALL3.TXT"),
             writable);
    QCOMPARE(readFile(writable), QByteArray {"packaged calls"});
  }

  void preservesNonemptyWritableFile()
  {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QDir dir {root.path()};
    QVERIFY(dir.mkpath("app"));
    QVERIFY(dir.mkpath("installed"));
    QVERIFY(dir.mkpath("writable"));
    QString const writable = dir.absoluteFilePath("writable/CALL3.TXT");
    QVERIFY(writeFile(dir.absoluteFilePath("installed/CALL3.TXT"), "packaged calls"));
    QVERIFY(writeFile(writable, "user calls"));

    QCOMPARE(ensureMap65RuntimeFile(dir.absoluteFilePath("app"),
                                    dir.absoluteFilePath("installed"),
                                    dir.absoluteFilePath("writable"), "CALL3.TXT"),
             writable);
    QCOMPARE(readFile(writable), QByteArray {"user calls"});
  }
};

QTEST_APPLESS_MAIN(TestMap65RuntimePaths)

#include "test_map65_runtime_paths.moc"
