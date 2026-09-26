/*
 * obs-atem
 *
 * OBS Studio plugin with dockable panels for a Blackmagic ATEM Mini:
 *   - ATEM Macros: trigger the macros stored on the switcher
 *   - ATEM PiP:    main/PiP input, position, size and crop of the DVE key
 *
 * No middleware server required — the plugin talks to the ATEM hardware
 * directly through the official Blackmagic SDK (BMDSwitcherAPI COM).
 */

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>

#include "atem-session.h"
#include "macro-dock.h"
#include "pip-dock.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-atem", "en-US")

// Shared by both panels; lives until module unload.
static AtemSession* session = nullptr;

const char* obs_module_name(void) {
    return "ATEM Panels";
}

const char* obs_module_description(void) {
    return "Dockable panels to trigger Blackmagic ATEM macros and control picture-in-picture";
}

// Text from data/locale/<lang>.ini, or the fallback if the file is missing.
static const char* text(const char* key, const char* fallback) {
    const char* out = nullptr;
    return obs_module_get_string(key, &out) ? out : fallback;
}

static void addDock(QMainWindow* mainWindow, const char* id, const char* title, QWidget* panel) {
#if LIBOBS_API_MAJOR_VER >= 30
    // OBS wraps the widget in its own dock, lists it under Docks and saves
    // its position with the scene collection layout.
    (void)mainWindow;
    if (!obs_frontend_add_dock_by_id(id, title, panel))
        blog(LOG_WARNING, "[ATEM] dock id '%s' already registered", id);
#else
    auto* dock = new QDockWidget(title, mainWindow);
    dock->setObjectName(id);
    dock->setWidget(panel);
    dock->setFloating(true);
    dock->setVisible(false);
    mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);
    if (auto* docksMenu = mainWindow->findChild<QMenu*>("menuDocks"))
        docksMenu->addAction(dock->toggleViewAction());
#endif
}

static void frontend_event_handler(enum obs_frontend_event event, void*) {
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
        // Connect once the main window is up: ConnectTo blocks the UI thread
        // for a few seconds when no ATEM answers.
        if (session) session->autoConnect();
        break;
    case OBS_FRONTEND_EVENT_EXIT:
        // Release the BMD COM objects while COM is still alive; the panels
        // themselves are owned and destroyed by OBS.
        if (session) session->shutdown();
        break;
    default:
        break;
    }
}

bool obs_module_load(void) {
    auto* mainWindow = static_cast<QMainWindow*>(obs_frontend_get_main_window());
    if (!mainWindow) {
        blog(LOG_ERROR, "[ATEM] Could not get OBS main window");
        return false;
    }

    session = new AtemSession();

    addDock(mainWindow, "AtemMacroDock", text("ATEM.MacrosDock", "ATEM Macros"), new AtemMacroDock(session));
    addDock(mainWindow, "AtemPipDock", text("ATEM.PipDock", "ATEM PiP"), new AtemPipDock(session));

    obs_frontend_add_event_callback(frontend_event_handler, nullptr);

    blog(LOG_INFO, "[ATEM] Plugin loaded (libobs API %d)", LIBOBS_API_MAJOR_VER);
    return true;
}

void obs_module_unload(void) {
    obs_frontend_remove_event_callback(frontend_event_handler, nullptr);
    // shutdown() already ran at EXIT; deleting only frees the Qt object.
    delete session;
    session = nullptr;
    blog(LOG_INFO, "[ATEM] Plugin unloaded");
}
