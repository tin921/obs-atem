/*
 * obs-atem
 *
 * OBS Studio plugin that provides a dockable panel for triggering
 * Blackmagic ATEM Mini macros directly via USB (BMDSwitcherAPI COM).
 *
 * No middleware server required — the plugin talks to the ATEM hardware
 * directly through the official Blackmagic SDK.
 */

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QMainWindow>
#include <QAction>
#include <QMenu>

#include "macro-dock.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-atem", "en-US")

static AtemMacroDock* macroDock = nullptr;

const char* obs_module_name(void) {
    return "ATEM Macro Panel";
}

const char* obs_module_description(void) {
    return "Dockable panel to trigger Blackmagic ATEM macros via USB";
}

static void frontend_event_handler(enum obs_frontend_event event, void*) {
    // Clean up on exit
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        if (macroDock) {
            delete macroDock;
            macroDock = nullptr;
        }
    }
}

bool obs_module_load(void) {
    blog(LOG_INFO, "[ATEM Macros] obs_module_load: start");

    // Get the OBS main window (Qt)
    auto* mainWindow = static_cast<QMainWindow*>(obs_frontend_get_main_window());
    blog(LOG_INFO, "[ATEM Macros] obs_module_load: mainWindow=%p", (void*)mainWindow);
    if (!mainWindow) {
        blog(LOG_ERROR, "[ATEM Macros] Could not get OBS main window");
        return false;
    }

    // Create the dock widget
    blog(LOG_INFO, "[ATEM Macros] obs_module_load: creating AtemMacroDock...");
    macroDock = new AtemMacroDock(mainWindow);
    blog(LOG_INFO, "[ATEM Macros] obs_module_load: AtemMacroDock created OK");

    blog(LOG_INFO, "[ATEM Macros] obs_module_load: setObjectName...");
    macroDock->setObjectName("AtemMacroDock");
    blog(LOG_INFO, "[ATEM Macros] obs_module_load: setFloating...");
    macroDock->setFloating(true);
    macroDock->resize(280, 400);
    macroDock->setVisible(false);

    // Register as an OBS dock
#if OBS_VERSION >= MAKE_SEMANTIC_VERSION(30, 0, 0)
    blog(LOG_INFO, "[ATEM Macros] obs_module_load: obs_frontend_add_dock_by_id...");
    obs_frontend_add_dock_by_id("AtemMacroDock", "ATEM Macros", macroDock);
#else
    mainWindow->addDockWidget(Qt::RightDockWidgetArea, macroDock);
    auto* viewMenu = mainWindow->findChild<QMenu*>("menuDocks");
    if (viewMenu) {
        viewMenu->addAction(macroDock->toggleViewAction());
    }
#endif

    obs_frontend_add_event_callback(frontend_event_handler, nullptr);

    blog(LOG_INFO, "[ATEM Macros] Plugin loaded successfully");
    return true;
}

void obs_module_unload(void) {
    obs_frontend_remove_event_callback(frontend_event_handler, nullptr);
    blog(LOG_INFO, "[ATEM Macros] Plugin unloaded");
}
