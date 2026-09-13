#include "runtime_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

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
