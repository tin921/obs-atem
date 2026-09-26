/*
 * atem-harness — standalone host for the ATEM panels.
 *
 * Stands up a bare QMainWindow and docks the *same* AtemMacroDock and
 * AtemPipDock widgets that obs-atem.dll registers with OBS, on the same
 * AtemSession, so the panels can be run, debugged and hot-iterated under a
 * normal debugger without launching OBS Studio. If something misbehaves in
 * OBS but not here, the problem is in the OBS integration (plugin-main.cpp);
 * if it misbehaves here too, it is in the panels or the SDK layer.
 *
 * Nothing in this file is compiled into the plugin, and the panel sources are
 * compiled here unchanged — only the blog() shim in obs-log.h differs.
 *
 * Differences from the plugin, all for debugging:
 *   - an "ATEM log" dock shows every trace line, and every SDK call the PiP
 *     panel sends (AtemPip::setLogCalls)
 *   - blog() output goes to the console
 *
 * Connects to the real ATEM the same way the plugin does (last-used USB or
 * IP) and shares its settings: connection, camera names/colours/pictures and
 * presets live under HKCU\Software\obs-atem.
 */

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTimer>

#include <objbase.h>

#include "atem-session.h"
#include "macro-dock.h"
#include "pip-dock.h"

static QDockWidget* addDock(QMainWindow& window, QMenu* docksMenu, const char* id,
                            const char* title, QWidget* panel, Qt::DockWidgetArea area) {
    auto* dock = new QDockWidget(title, &window);
    dock->setObjectName(id);
    dock->setWidget(panel);
    window.addDockWidget(area, dock);
    docksMenu->addAction(dock->toggleViewAction());
    return dock;
}

int main(int argc, char** argv) {
    // The plugin runs inside OBS's already-initialised STA; do the same here so
    // the BMDSwitcherAPI COM objects see an identical apartment.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::fprintf(stderr, "CoInitializeEx failed: 0x%08lx\n", (unsigned long)hr);
        return 1;
    }

    int rc = 0;
    {
        QApplication app(argc, argv);
        app.setApplicationName("ATEM Panels Harness");

        QMainWindow window;
        window.resize(1200, 820);
        window.setWindowTitle("ATEM panels — harness (not OBS)");

        // Stand-in for the OBS canvas, so docking/resize behaves like the real thing.
        auto* canvas = new QLabel("OBS preview area (stub)");
        canvas->setAlignment(Qt::AlignCenter);
        canvas->setStyleSheet("background:#1e1e1e; color:#555; font-size:14px;");
        window.setCentralWidget(canvas);

        AtemSession session;
        session.pip().setLogCalls(true);

        auto* docksMenu = window.menuBar()->addMenu("&Docks");
        auto* macros = addDock(window, docksMenu, "AtemMacroDock", "ATEM Macros",
                               new AtemMacroDock(&session), Qt::RightDockWidgetArea);
        auto* pip = addDock(window, docksMenu, "AtemPipDock", "ATEM PiP",
                            new AtemPipDock(&session), Qt::RightDockWidgetArea);
        window.splitDockWidget(macros, pip, Qt::Horizontal);
        pip->setMinimumWidth(320);

        auto* log = new QPlainTextEdit();
        log->setReadOnly(true);
        log->setMaximumBlockCount(5000);
        log->setStyleSheet("background:#111; color:#bbb; font-family:Consolas,monospace; font-size:11px;");
        QObject::connect(&session, &AtemSession::traceMessage, log, &QPlainTextEdit::appendPlainText);
        addDock(window, docksMenu, "AtemLog", "ATEM log (trace + SDK calls)", log, Qt::BottomDockWidgetArea);

        window.statusBar()->showMessage("Harness — connects to the ATEM via last-used USB/IP");
        window.show();

        // Same moment as the plugin's OBS_FRONTEND_EVENT_FINISHED_LOADING.
        QTimer::singleShot(0, &session, [&session]() { session.autoConnect(); });

        rc = app.exec();
        session.shutdown();
    }

    // No CoUninitialize: doing it while BMDSwitcherAPI's network threads wind
    // down crashes the process (same reason as in atem-cli). Exit cleans up.
    return rc;
}
