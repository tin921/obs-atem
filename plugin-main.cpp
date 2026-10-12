/*
 * obs-atem
 *
 * OBS Studio plugin with dockable panels for a Blackmagic ATEM Mini:
 *   - ATEM Macros: trigger the macros stored on the switcher
 *   - ATEM PiP:    main/PiP input, position, size and crop of the DVE key
 *   - ATEM Views:  recall the PiP panel's saved views
 *
 * No middleware server required — the plugin talks to the ATEM hardware
 * directly through the official Blackmagic SDK (BMDSwitcherAPI COM).
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QFileSystemWatcher>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QSignalBlocker>
#include <QTimer>

#include "atem-session.h"
#include "macro-dock.h"
#include "pip-dock.h"
#include "pip-settings.h"
#include "views-dock.h"

OBS_DECLARE_MODULE()
// No locale files: the plugin is English only, so its text is in the code and
// OBS has no data/locale/*.ini to look for.

// Shared by the panels; lives until module unload.
static AtemSession* session = nullptr;
static PipSettings* pipSettings = nullptr;
static QMainWindow* mainWindow = nullptr;

const char* obs_module_name(void) {
    return "ATEM Panels";
}

const char* obs_module_description(void) {
    return "Dockable panels to trigger Blackmagic ATEM macros and control picture-in-picture";
}

// ── One OBS instance controls the ATEM ───────────────────────
//
// The ATEM Mini takes one control connection over USB: with two OBS
// instances running (one per streaming profile), the one that connects last
// takes the ATEM from the other. settings.json in the plugin's config folder
// (shared by every OBS instance of this Windows user) names the profile that
// uses the ATEM, key "target_profile":
//   - no key:  every profile (how the plugin worked before a choice was made)
//   - "":      no profile
//   - a name:  only that profile; in every other one the panels are removed
//              and the plugin never connects
// Tools > "ATEM Control (this profile)" sets it. The other instances watch
// the folder, so they let go of the ATEM as soon as it is ticked elsewhere.

static const char* const kToolsItem = "ATEM Control (this profile)";
static QPointer<QAction> toolsAction;
static QFileSystemWatcher* settingsWatcher = nullptr;
static bool docksAdded = false;

static QString settingsFile() {
    char* path = obs_module_config_path("settings.json");
    QString file = QString::fromUtf8(path ? path : "");
    bfree(path);
    return file;
}

static QString currentProfile() {
    char* name = obs_frontend_get_current_profile();
    QString profile = QString::fromUtf8(name ? name : "");
    bfree(name);
    return profile;
}

// settings.json as it is now (empty if there is none yet); release it.
static obs_data_t* loadSettings() {
    obs_data_t* data = obs_data_create_from_json_file_safe(settingsFile().toUtf8().constData(), "bak");
    return data ? data : obs_data_create();
}

static bool saveSettings(obs_data_t* data) {
    char* dir = obs_module_config_path("");
    if (dir) os_mkdirs(dir);
    bfree(dir);
    const bool ok = obs_data_save_json_safe(data, settingsFile().toUtf8().constData(), "tmp", "bak");
    if (!ok) blog(LOG_ERROR, "[ATEM] could not save %s", settingsFile().toUtf8().constData());
    return ok;
}

// The chosen profile; *chosen is false when no choice was ever made.
static QString targetProfile(bool* chosen) {
    obs_data_t* data = loadSettings();
    *chosen = obs_data_has_user_value(data, "target_profile");
    const QString target = QString::fromUtf8(obs_data_get_string(data, "target_profile"));
    obs_data_release(data);
    return target;
}

static bool saveTargetProfile(const QString& target) {
    obs_data_t* data = loadSettings();
    obs_data_set_string(data, "target_profile", target.toUtf8().constData());
    const bool ok = saveSettings(data);
    obs_data_release(data);
    return ok;
}

// ── The panels' place in the window ──────────────────────────
//
// OBS keeps one window layout for all its instances (user.ini, DockState),
// saved by whichever instance closes last. When that is an instance without
// the ATEM panels, the instance that uses the ATEM starts with them hidden
// (the layout keeps their place, not that they were shown). So that instance
// saves its own layout here when it exits with a panel shown, and puts it
// back at the next start when OBS's layout shows none of them.

static const char* const kDockIds[] = { "AtemMacroDock", "AtemPipDock", "AtemViewsDock" };

static bool anyPanelShown() {
    for (const char* id : kDockIds)
        if (auto* dock = mainWindow->findChild<QDockWidget*>(id); dock && !dock->isHidden()) return true;
    return false;
}

static void saveOwnLayout() {
    obs_data_t* data = loadSettings();
    if (anyPanelShown()) {
        obs_data_set_string(data, "layout", mainWindow->saveState().toBase64().constData());
        obs_data_set_string(data, "layout_profile", currentProfile().toUtf8().constData());
    } else {
        // All closed on purpose: nothing to bring back.
        obs_data_erase(data, "layout");
        obs_data_erase(data, "layout_profile");
    }
    saveSettings(data);
    obs_data_release(data);
}

static void restoreOwnLayoutIfHidden() {
    if (anyPanelShown()) return;
    obs_data_t* data = loadSettings();
    const QString profile = QString::fromUtf8(obs_data_get_string(data, "layout_profile"));
    const QByteArray layout = QByteArray::fromBase64(obs_data_get_string(data, "layout"));
    obs_data_release(data);
    if (layout.isEmpty() || profile != currentProfile()) return;
    const bool ok = mainWindow->restoreState(layout);
    blog(LOG_INFO, "[ATEM] OBS's window layout showed no ATEM panels (another instance closed last): "
                   "this profile's own layout %s", ok ? "restored" : "could not be restored");
}

static void addDock(const char* id, const char* title, QWidget* panel) {
#if LIBOBS_API_MAJOR_VER >= 30
    // OBS wraps the widget in its own dock, lists it under Docks and saves
    // its position with the scene collection layout.
    if (!obs_frontend_add_dock_by_id(id, title, panel)) {
        // OBS didn't take the panel, so nothing else will delete it.
        blog(LOG_WARNING, "[ATEM] dock id '%s' already registered", id);
        delete panel;
    }
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

static void addDocks() {
    if (docksAdded) return;
    addDock("AtemMacroDock", "ATEM Macros", new AtemMacroDock(session));
    addDock("AtemPipDock", "ATEM PiP", new AtemPipDock(session, pipSettings));
    addDock("AtemViewsDock", "ATEM Views", new AtemViewsDock(session, pipSettings));
    docksAdded = true;
}

static void removeDocks() {
#if LIBOBS_API_MAJOR_VER >= 30
    if (!docksAdded) return;
    // OBS deletes the docks and the panels in them; the panels only hold
    // Qt connections to the session, which go with them.
    obs_frontend_remove_dock("AtemMacroDock");
    obs_frontend_remove_dock("AtemPipDock");
    obs_frontend_remove_dock("AtemViewsDock");
    docksAdded = false;
#endif
}

// Shows the panels and connects in the chosen profile; removes them and lets
// go of the ATEM in any other. connect: also connect now if this profile
// uses the ATEM (not during module load: OBS connects once it has loaded).
static void applyProfile(bool connect) {
    if (!session) return;
    if (session->isBusy()) {
        // A ConnectTo is pumping messages: decide once it has returned.
        QTimer::singleShot(500, [connect]() { applyProfile(connect); });
        return;
    }
    bool chosen = false;
    const QString target = targetProfile(&chosen);
    const QString profile = currentProfile();
    const bool uses = !chosen || target == profile;

    if (toolsAction) {
        QSignalBlocker block(toolsAction.data());
        toolsAction->setChecked(chosen && target == profile);
    }

    if (uses) {
        addDocks();
        if (connect && session->state() == AtemState::Disconnected) session->autoConnect();
    } else {
        if (session->state() != AtemState::Disconnected) session->disconnect();
        removeDocks();
    }
    blog(LOG_INFO, "[ATEM] profile '%s': %s (ATEM control %s)", profile.toUtf8().constData(),
         uses ? "uses the ATEM" : "leaves the ATEM to another profile",
         !chosen ? "in every profile" : target.isEmpty() ? "in no profile"
                                                         : ("in '" + target + "'").toUtf8().constData());
}

static void setupToolsMenu() {
    toolsAction = static_cast<QAction*>(obs_frontend_add_tools_menu_qaction(kToolsItem));
    if (!toolsAction) return;
    toolsAction->setCheckable(true);
    QObject::connect(toolsAction.data(), &QAction::toggled, [](bool checked) {
        saveTargetProfile(checked ? currentProfile() : QString());
        applyProfile(true);
    });

    // Another OBS instance may change the choice: follow it. (The file is
    // replaced, not written in place, so the folder is watched. Saving the
    // layout at exit changes it too; applying again then changes nothing.)
    char* dir = obs_module_config_path("");
    if (dir) os_mkdirs(dir);
    settingsWatcher = new QFileSystemWatcher(session);
    settingsWatcher->addPath(QString::fromUtf8(dir ? dir : ""));
    bfree(dir);
    // A save is several changes (temporary file, backup, rename): one apply for the burst.
    static bool pending = false;
    QObject::connect(settingsWatcher, &QFileSystemWatcher::directoryChanged, [](const QString&) {
        if (pending) return;
        pending = true;
        QTimer::singleShot(300, []() {
            pending = false;
            applyProfile(true);
        });
    });
}

static void frontend_event_handler(enum obs_frontend_event event, void*) {
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
        // OBS has restored its window layout by now.
        if (docksAdded) restoreOwnLayoutIfHidden();
        // Connect once the main window is up (ConnectTo blocks the UI thread
        // for a few seconds when no ATEM answers), if this profile uses it.
        applyProfile(true);
        break;
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
        applyProfile(true);
        break;
    case OBS_FRONTEND_EVENT_EXIT:
        // The instance with the ATEM panels keeps its own layout (see above).
        if (docksAdded && mainWindow) saveOwnLayout();
        // Release the BMD COM objects while COM is still alive; the panels
        // themselves are owned and destroyed by OBS.
        if (session) session->shutdown();
        break;
    default:
        break;
    }
}

bool obs_module_load(void) {
    mainWindow = static_cast<QMainWindow*>(obs_frontend_get_main_window());
    if (!mainWindow) {
        blog(LOG_ERROR, "[ATEM] Could not get OBS main window");
        return false;
    }

    session = new AtemSession();

    // Camera looks and saved PiP presets, shared by the PiP and Views panels.
    pipSettings = new PipSettings(session);

    // The profile is already loaded here, so the panels of the profile that
    // uses the ATEM are added now and OBS restores where they were docked.
    // Nothing connects yet.
    setupToolsMenu();
    applyProfile(false);

    obs_frontend_add_event_callback(frontend_event_handler, nullptr);

    blog(LOG_INFO, "[ATEM] Plugin loaded (libobs API %d)", LIBOBS_API_MAJOR_VER);
    return true;
}

void obs_module_unload(void) {
    obs_frontend_remove_event_callback(frontend_event_handler, nullptr);
    // shutdown() already ran at EXIT; deleting only frees the Qt object
    // (and the settings watcher, its child).
    delete session;
    session = nullptr;
    pipSettings = nullptr;
    blog(LOG_INFO, "[ATEM] Plugin unloaded");
}
