// SPDX-License-Identifier: GPL-3.0-only

// Linux-only test instrumentation. Loaded only into fixture processes via
// LD_PRELOAD; no test switch or instrumentation is added to the application.
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <dlfcn.h>

namespace
{
  void record_and_close (QWidget * window, QSpinBox * spin)
  {
    QJsonObject state;
    state["value"] = spin->value ();
    state["minimum"] = spin->minimum ();
    state["maximum"] = spin->maximum ();
    QJsonArray labels;
    if (auto status = window->findChild<QStatusBar *> ())
      for (auto label : status->findChildren<QLabel *> ()) labels.append (label->text ());
    state["status_labels"] = labels;
    QFile output {qEnvironmentVariable ("WSJT_SUBMODE_PROBE_OUTPUT")};
    if (!output.open (QIODevice::WriteOnly))
      {
        qApp->exit (91);
        return;
      }
    output.write (QJsonDocument {state}.toJson ());
    output.close ();
    window->close (); // normal application settings save and decoder shutdown
  }
}

// Interpose the normal QApplication event loop, after MainWindow construction.
int QApplication::exec ()
{
  using Exec = int (*) ();
  auto real_exec = reinterpret_cast<Exec> (dlsym (RTLD_NEXT, "_ZN12QApplication4execEv"));
  if (!real_exec) return 92;
  QTimer::singleShot (1000, qApp, [] {
    for (auto window : QApplication::topLevelWidgets ())
      {
        auto spin = window->findChild<QSpinBox *> ("sbSubmode");
        if (!spin) continue;
        auto edit = qEnvironmentVariable ("WSJT_SUBMODE_PROBE_EDIT");
        if (!edit.isEmpty ()) spin->setValue (edit.toInt ());
        auto mode = qEnvironmentVariable ("WSJT_SUBMODE_PROBE_MODE");
        if (!mode.isEmpty ())
          {
            auto slot = QString {"on_action%1_triggered"}.arg (mode).toLatin1 ();
            if (!QMetaObject::invokeMethod (window, slot.constData (), Qt::DirectConnection))
              {
                qApp->exit (93);
                return;
              }
          }
        if (qEnvironmentVariableIsSet ("WSJT_SUBMODE_PROBE_ACCEPT_SETTINGS"))
          {
            QTimer::singleShot (50, qApp, [] {
              for (auto widget : QApplication::topLevelWidgets ())
                if (qobject_cast<QDialog *> (widget)
                    && widget->findChild<QWidget *> ("enable_VHF_features_check_box"))
                  {
                    // Exercise MainWindow's accepted-settings path with the
                    // existing configuration. Calling accept() validates a
                    // physical audio input, unavailable on headless builders.
                    // done(Accepted) changes no station/radio settings.
                    if (!QMetaObject::invokeMethod (widget, "done", Qt::DirectConnection,
                                                    Q_ARG (int, QDialog::Accepted)))
                      qApp->exit (95);
                    return;
                  }
              qApp->exit (96);
            });
            QMetaObject::invokeMethod (window, "on_actionSettings_triggered", Qt::DirectConnection);
          }
        // Include the delayed 50/200 ms mode restore/save callbacks.
        QTimer::singleShot (500, window, [window, spin] { record_and_close (window, spin); });
        return;
      }
    qApp->exit (94);
  });
  return real_exec ();
}
