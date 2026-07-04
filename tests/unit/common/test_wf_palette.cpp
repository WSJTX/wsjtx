#include <QtTest>

#include <stdexcept>

#include <QColor>
#include <QFile>
#include <QString>
#include <QTemporaryFile>
#include <QTextStream>

#include "WFPalette.hpp"

class TestWFPalette
  : public QObject
{
  Q_OBJECT

private:
  QString write_palette (QString const& body)
  {
    QTemporaryFile file;
    file.setAutoRemove (false);
    if (!file.open ())
      {
        throw std::runtime_error {"failed to open temporary palette file"};
      }

    QTextStream out {&file};
    out << body;
    out.flush ();
    if (out.status () != QTextStream::Ok)
      {
        throw std::runtime_error {"failed to write temporary palette file"};
      }

    auto const name = file.fileName ();
    file.close ();
    return name;
  }

  QString palette_body (int lines)
  {
    QString body;
    QTextStream out {&body};
    for (int i {0}; i < lines; ++i)
      {
        out << i % 256 << ';' << (i + 1) % 256 << ';' << (i + 2) % 256 << '\n';
      }
    return body;
  }

  void remove_file (QString const& file_name)
  {
    QVERIFY (QFile::remove (file_name));
  }

private Q_SLOTS:
  void constructor_accepts_256_colour_palette ()
  {
    auto const file_name = write_palette (palette_body (256));

    WFPalette palette {file_name};

    QCOMPARE (palette.colours ().size (), 256);
    QCOMPARE (palette.colours ().front (), QColor (0, 1, 2));
    QCOMPARE (palette.colours ().back (), QColor (255, 0, 1));
    QCOMPARE (palette.interpolate ().size (), 256);

    remove_file (file_name);
  }

  void constructor_rejects_257_colour_palette ()
  {
    auto const file_name = write_palette (palette_body (257));

    QVERIFY_EXCEPTION_THROWN (WFPalette {file_name}, std::runtime_error);

    remove_file (file_name);
  }

  void constructor_rejects_invalid_triplet ()
  {
    auto const file_name = write_palette ("0;1\n");

    QVERIFY_EXCEPTION_THROWN (WFPalette {file_name}, std::runtime_error);

    remove_file (file_name);
  }

  void constructor_rejects_invalid_colour_component ()
  {
    auto const file_name = write_palette ("0;1;256\n");

    QVERIFY_EXCEPTION_THROWN (WFPalette {file_name}, std::runtime_error);

    remove_file (file_name);
  }

  void constructor_rejects_missing_file ()
  {
    QTemporaryFile file;
    QVERIFY (file.open ());
    auto const file_name = file.fileName ();
    file.close ();
    QVERIFY (QFile::remove (file_name));

    QVERIFY_EXCEPTION_THROWN (WFPalette {file_name}, std::runtime_error);
  }
};

QTEST_MAIN (TestWFPalette);

#include "test_wf_palette.moc"
