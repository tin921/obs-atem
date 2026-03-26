"""
ATEM Macro Panel — OBS Studio Python Script

Connects to a Blackmagic ATEM Mini via USB or network using PyATEMMax
(pure Python, no BMD SDK needed). Displays macro buttons in the OBS
Scripts properties panel.

SETUP:
  1. pip install PyATEMMax
  2. In OBS: Tools → Scripts → Python Settings → set Python path
  3. Tools → Scripts → + → select this file
  4. Set connection mode (USB auto-detect or manual IP)
  5. Click "Connect to ATEM"
  6. Macro buttons appear automatically

UI lives in: Tools → Scripts → (select this script) → right panel
Note: OBS Python scripts cannot create custom dock widgets (C++ only).
      The properties panel is the available UI surface.

REQUIREMENTS:
  - Python 3.6+ (matching your OBS Python version)
  - PyATEMMax: pip install PyATEMMax
"""

import obspython as obs
import threading
import time

# ── Globals ───────────────────────────────────────────────────

switcher = None          # PyATEMMax.ATEMMax instance
atem_connected = False
atem_model = ""
atem_address = ""
macro_cache = []         # List of (index, name, description)
connection_error = ""
connect_mode = "usb"     # "usb" or "ip"
manual_ip = "192.168.10.240"
poll_active = False

# Status for display
status_text = "Disconnected"

# ── ATEM Connection ───────────────────────────────────────────

def get_switcher():
    """Lazy import and create PyATEMMax instance."""
    global switcher
    if switcher is None:
        try:
            import PyATEMMax
            switcher = PyATEMMax.ATEMMax()
        except ImportError:
            obs.script_log(obs.LOG_ERROR,
                "[ATEM] PyATEMMax not installed. Run: pip install PyATEMMax")
            return None
    return switcher


def find_usb_atem_ip():
    """
    When ATEM Mini is connected via USB, it creates a virtual ethernet
    adapter. The ATEM typically sits at a 169.254.x.x address or uses
    the IP configured in its settings. We try common approaches.
    """
    import socket
    import struct

    # Method 1: Try the configured IP from ATEM Software Control
    # The ATEM keeps its configured IP even over USB
    test_ips = [
        "192.168.10.240",   # Default ATEM IP
        "192.168.10.1",
    ]

    for ip in test_ips:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(0.5)
            # ATEM protocol port
            sock.sendto(b'\x10\x14\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00',
                        (ip, 9910))
            try:
                data, addr = sock.recvfrom(1024)
                sock.close()
                if len(data) > 0:
                    return ip
            except socket.timeout:
                sock.close()
                continue
        except Exception:
            continue

    return None


def do_connect():
    """Connect to ATEM in background thread."""
    global atem_connected, atem_model, atem_address
    global connection_error, status_text, macro_cache

    sw = get_switcher()
    if sw is None:
        connection_error = "PyATEMMax not installed"
        status_text = "ERROR: PyATEMMax not installed"
        return

    status_text = "Connecting..."
    connection_error = ""

    try:
        if connect_mode == "usb":
            # Try to find ATEM via USB
            obs.script_log(obs.LOG_INFO, "[ATEM] Scanning for USB-connected ATEM...")
            ip = find_usb_atem_ip()
            if ip:
                obs.script_log(obs.LOG_INFO, f"[ATEM] Found ATEM at {ip}")
                target = ip
            else:
                # Fall back to default
                obs.script_log(obs.LOG_WARNING,
                    "[ATEM] USB scan failed, trying default 192.168.10.240")
                target = "192.168.10.240"
        else:
            target = manual_ip

        atem_address = target
        sw.connect(target)
        sw.waitForConnection(timeout=5)

        if sw.connected:
            atem_connected = True
            # Try to get product name
            try:
                atem_model = str(sw.atemModel) if hasattr(sw, 'atemModel') else "ATEM"
            except Exception:
                atem_model = "ATEM"

            status_text = f"Connected — {atem_model} ({target})"
            obs.script_log(obs.LOG_INFO, f"[ATEM] Connected to {atem_model} at {target}")

            # Load macros
            refresh_macros()
        else:
            atem_connected = False
            connection_error = f"No response from {target}"
            status_text = f"Failed: No response from {target}"
            obs.script_log(obs.LOG_WARNING, f"[ATEM] Connection failed: {target}")

    except Exception as e:
        atem_connected = False
        connection_error = str(e)
        status_text = f"Error: {e}"
        obs.script_log(obs.LOG_ERROR, f"[ATEM] Connection error: {e}")


def do_disconnect():
    """Disconnect from ATEM."""
    global atem_connected, status_text, macro_cache, switcher

    if switcher and switcher.connected:
        try:
            switcher.disconnect()
        except Exception:
            pass

    atem_connected = False
    macro_cache = []
    status_text = "Disconnected"
    switcher = None
    obs.script_log(obs.LOG_INFO, "[ATEM] Disconnected")


def refresh_macros():
    """Read all macros from the ATEM."""
    global macro_cache

    if not switcher or not switcher.connected:
        macro_cache = []
        return

    macros = []
    try:
        # PyATEMMax macro count — ATEM Mini supports up to 100 macro slots
        max_macros = 100
        for i in range(max_macros):
            try:
                if switcher.macroProperties[i].isUsed:
                    name = switcher.macroProperties[i].name
                    if not name:
                        name = f"Macro {i + 1}"
                    desc = ""
                    try:
                        desc = switcher.macroProperties[i].description or ""
                    except Exception:
                        pass
                    macros.append((i, name, desc))
            except (IndexError, AttributeError, KeyError):
                continue

        macro_cache = macros
        obs.script_log(obs.LOG_INFO,
            f"[ATEM] Loaded {len(macros)} macros")
    except Exception as e:
        obs.script_log(obs.LOG_ERROR, f"[ATEM] Error reading macros: {e}")
        macro_cache = []


def run_macro(index):
    """Execute a macro by index."""
    if not switcher or not switcher.connected:
        obs.script_log(obs.LOG_WARNING, "[ATEM] Not connected")
        return

    try:
        switcher.setMacroAction(index, 0)  # 0 = run macro
        obs.script_log(obs.LOG_INFO, f"[ATEM] Running macro #{index + 1}")
    except Exception as e:
        obs.script_log(obs.LOG_ERROR, f"[ATEM] Error running macro #{index + 1}: {e}")


def stop_macro():
    """Stop the currently running macro."""
    if not switcher or not switcher.connected:
        return
    try:
        switcher.setMacroAction(0, 2)  # 2 = stop
        obs.script_log(obs.LOG_INFO, "[ATEM] Macro stopped")
    except Exception as e:
        obs.script_log(obs.LOG_ERROR, f"[ATEM] Error stopping macro: {e}")


# ── OBS Script Interface ─────────────────────────────────────

def script_description():
    return (
        '<h2>ATEM Macro Panel</h2>'
        '<p>Connect to a Blackmagic ATEM Mini and trigger macros.</p>'
        '<p><b>Requires:</b> <code>pip install PyATEMMax</code></p>'
        '<p style="color: #888;">Macros are stored on the ATEM hardware. '
        'Create them with ATEM Software Control.</p>'
    )


def script_properties():
    """Build the UI properties shown in the Scripts panel."""
    props = obs.obs_properties_create()

    # ── Connection Settings ───────────────────────────────────
    grp = obs.obs_properties_create()

    # Connection mode dropdown
    mode_list = obs.obs_properties_add_list(grp, "connect_mode",
        "Connection Mode",
        obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING)
    obs.obs_property_list_add_string(mode_list, "USB (auto-detect)", "usb")
    obs.obs_property_list_add_string(mode_list, "Manual IP", "ip")
    obs.obs_property_set_modified_callback(mode_list, on_mode_changed)

    # IP address field (hidden when USB mode)
    ip_prop = obs.obs_properties_add_text(grp, "manual_ip",
        "ATEM IP Address", obs.OBS_TEXT_DEFAULT)
    obs.obs_property_set_visible(ip_prop, connect_mode == "ip")

    # Status display
    obs.obs_properties_add_text(grp, "status_display",
        "Status", obs.OBS_TEXT_INFO)

    # Connect / Disconnect buttons
    obs.obs_properties_add_button(grp, "btn_connect",
        "Connect to ATEM", on_connect_clicked)
    obs.obs_properties_add_button(grp, "btn_disconnect",
        "Disconnect", on_disconnect_clicked)
    obs.obs_properties_add_button(grp, "btn_refresh",
        "↻ Refresh Macros", on_refresh_clicked)

    obs.obs_properties_add_group(props, "connection_group",
        "⚙  Connection", obs.OBS_GROUP_NORMAL, grp)

    # ── Macro Buttons ─────────────────────────────────────────
    macro_grp = obs.obs_properties_create()

    if not atem_connected:
        obs.obs_properties_add_text(macro_grp, "no_connection_msg",
            "Connect to ATEM to see macros", obs.OBS_TEXT_INFO)
    elif len(macro_cache) == 0:
        obs.obs_properties_add_text(macro_grp, "no_macros_msg",
            "No macros found on ATEM", obs.OBS_TEXT_INFO)
    else:
        for idx, name, desc in macro_cache:
            button_id = f"macro_btn_{idx}"
            button_label = f"#{idx + 1}  {name}"
            obs.obs_properties_add_button(macro_grp, button_id,
                button_label, make_macro_callback(idx))

        # Stop button
        obs.obs_properties_add_button(macro_grp, "btn_stop_macro",
            "■  STOP MACRO", on_stop_clicked)

    obs.obs_properties_add_group(props, "macro_group",
        "🎛  Macros", obs.OBS_GROUP_NORMAL, macro_grp)

    # ── Troubleshooting ───────────────────────────────────────
    trouble_grp = obs.obs_properties_create()

    tips = (
        "• Ensure ATEM is connected via USB or Ethernet\n"
        "• USB: ATEM creates a virtual network adapter\n"
        "• Run 'ipconfig' to find the Blackmagic adapter IP\n"
        "• Default ATEM IP: 192.168.10.240\n"
        "• PyATEMMax install: pip install PyATEMMax\n"
    )
    if connection_error:
        tips = f"LAST ERROR: {connection_error}\n\n" + tips

    info_prop = obs.obs_properties_add_text(trouble_grp, "troubleshoot_info",
        tips, obs.OBS_TEXT_INFO)

    obs.obs_properties_add_group(props, "trouble_group",
        "🔧  Troubleshooting", obs.OBS_GROUP_NORMAL, trouble_grp)

    return props


def script_defaults(settings):
    """Set default values."""
    obs.obs_data_set_default_string(settings, "connect_mode", "usb")
    obs.obs_data_set_default_string(settings, "manual_ip", "192.168.10.240")
    obs.obs_data_set_default_string(settings, "status_display", status_text)


def script_update(settings):
    """Called when user changes settings."""
    global connect_mode, manual_ip
    connect_mode = obs.obs_data_get_string(settings, "connect_mode")
    manual_ip = obs.obs_data_get_string(settings, "manual_ip")

    # Update status display
    obs.obs_data_set_string(settings, "status_display", status_text)


def script_load(settings):
    """Called when script is loaded."""
    global connect_mode, manual_ip
    connect_mode = obs.obs_data_get_string(settings, "connect_mode") or "usb"
    manual_ip = obs.obs_data_get_string(settings, "manual_ip") or "192.168.10.240"
    obs.script_log(obs.LOG_INFO, "[ATEM] Macro Panel script loaded")


def script_unload():
    """Called when script is unloaded."""
    do_disconnect()
    obs.script_log(obs.LOG_INFO, "[ATEM] Macro Panel script unloaded")


# ── UI Callbacks ──────────────────────────────────────────────

def on_mode_changed(props, prop, settings):
    """Show/hide IP field based on connection mode."""
    mode = obs.obs_data_get_string(settings, "connect_mode")
    ip_prop = obs.obs_properties_get(
        obs.obs_property_group_content(
            obs.obs_properties_get(props, "connection_group")),
        "manual_ip")
    if ip_prop:
        obs.obs_property_set_visible(ip_prop, mode == "ip")
    return True


def on_connect_clicked(props, prop):
    """Connect button handler — runs in background thread."""
    thread = threading.Thread(target=_connect_and_refresh, daemon=True)
    thread.start()
    return True


def _connect_and_refresh():
    """Background: connect then trigger UI refresh."""
    do_connect()
    # Give OBS a moment, then the next script_properties call
    # will show updated macros


def on_disconnect_clicked(props, prop):
    """Disconnect button handler."""
    do_disconnect()
    return True


def on_refresh_clicked(props, prop):
    """Refresh macros button handler."""
    if atem_connected:
        refresh_macros()
    return True


def on_stop_clicked(props, prop):
    """Stop macro button handler."""
    stop_macro()
    return True


def make_macro_callback(index):
    """Factory to create a callback for a specific macro index."""
    def callback(props, prop):
        run_macro(index)
        return False  # Don't refresh UI
    return callback
