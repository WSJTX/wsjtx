#include <QApplication>
#include <QCommandLineParser>
#include <QtGlobal>
// No windows.h needed anymore

#include "MMTTYIF.hpp"
#include "MainWindow.hpp"

#include <QDateTime>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName("mmtty_interface");
  QCoreApplication::setApplicationVersion("1.0");



  QCommandLineParser parser;
  parser.setApplicationDescription(
      "MMTTY Interface Utility Command Line Parser");
  // parser.addHelpOption();
  // parser.addVersionOption();

  // Boolean flags
  QStringList flags = {"s", "t", "u", "r", "f", "d", "m", "Z", "n", "p", "a"};
  for (const QString &flag : flags) {
    parser.addOption({flag, QString("Enable %1 flag.").arg(flag)});
  }

  // Options with parameters
  QCommandLineOption hexOption("h", "Hexadecimal value.", "hex");
  hexOption.setFlags(QCommandLineOption::ShortOptionStyle);
  QCommandLineOption stringOption("C", "String value.", "string");
  stringOption.setFlags(QCommandLineOption::ShortOptionStyle);
  QCommandLineOption decimalOption("T", "Decimal value.", "decimal");
  decimalOption.setFlags(QCommandLineOption::ShortOptionStyle);

  parser.addOption(hexOption);
  parser.addOption(stringOption);
  parser.addOption(decimalOption);

  parser.process(app);

  CommandLineOptions opts;
  for (const QString &flag : flags) {
    opts.flags[flag] = parser.isSet(flag);
  }
  opts.hexValue = parser.value(hexOption);
  opts.stringValue = parser.value(stringOption);
  opts.decimalValue = parser.value(decimalOption).toInt();

  MainWindow window(opts);
  window.show();

  window.getMmttyIf()->initialize(61002); // Initialize with default port


  return app.exec();
}
