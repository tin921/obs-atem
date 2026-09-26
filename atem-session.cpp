#include "atem-session.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QSettings>

namespace {

constexpr const char* kSettingsOrg = "obs-atem";
constexpr const char* kSettingsApp = "obs-atem";
constexpr const char* kModeKey     = "connection/mode";
constexpr const char* kIPKey       = "connection/ip";
constexpr const char* kDefaultIP   = "192.168.10.240";

} // namespace

AtemSession::AtemSession(QObject* parent)
    : QObject(parent)
{
    // Every callback below can arrive on a BMD SDK thread. Hop to the UI
    // thread before touching anything; queued calls to a destroyed session
    // are dropped by Qt.
    m_atem.setTraceCallback([this](const std::string& msg) {
        QString line = QString("[%1] %2")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"),
                 QString::fromStdString(msg));
        QMetaObject::invokeMethod(this, [this, line]() { emit traceMessage(line); },
                                  Qt::QueuedConnection);
    });
    m_atem.setMacroUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() { emit macrosChanged(); }, Qt::QueuedConnection);
    });
    m_atem.setConnectionLostCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            m_atem.handleConnectionLost();
            emit connectionChanged(m_atem.state());
        }, Qt::QueuedConnection);
    });
    m_atem.pip().setChangeCallback([this]() { queuePipChanged(); });
}

AtemSession::~AtemSession() {
    shutdown();
}

QString AtemSession::lastIP() const {
    QSettings settings(kSettingsOrg, kSettingsApp);
    return settings.value(kIPKey, kDefaultIP).toString();
}

bool AtemSession::lastWasIP() const {
    QSettings settings(kSettingsOrg, kSettingsApp);
    return settings.value(kModeKey).toString() == "ip";
}

bool AtemSession::connectUSB() {
    if (m_busy) return false;
    m_busy = true;
    emit connectionChanged(AtemState::Connecting);
    // Paint "connecting…" before ConnectTo blocks the UI thread.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    return finishConnect(m_atem.connectUSB(), false, QString());
}

bool AtemSession::connectIP(const QString& address) {
    QString ip = address.trimmed();
    if (m_busy || ip.isEmpty()) return false;
    m_busy = true;
    emit connectionChanged(AtemState::Connecting);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    return finishConnect(m_atem.connectIP(ip.toStdString()), true, ip);
}

bool AtemSession::autoConnect() {
    if (lastWasIP()) return connectIP(lastIP());
    return connectUSB();
}

bool AtemSession::finishConnect(bool ok, bool viaIP, const QString& address) {
    m_busy = false;
    if (ok) {
        QSettings settings(kSettingsOrg, kSettingsApp);
        settings.setValue(kModeKey, viaIP ? "ip" : "usb");
        if (viaIP) settings.setValue(kIPKey, address);
    }
    // Panels reload macros / PiP state on connectionChanged.
    emit connectionChanged(m_atem.state());
    return ok;
}

void AtemSession::disconnect() {
    if (m_busy) return;
    m_atem.disconnect();
    emit connectionChanged(m_atem.state());
}

void AtemSession::shutdown() {
    // Removing the SDK callbacks (inside shutdown) is what stops further
    // notifications; the std::function handlers can stay in place.
    bool wasConnected = m_atem.state() != AtemState::Disconnected;
    m_atem.shutdown();
    if (wasConnected) emit connectionChanged(m_atem.state());
}

void AtemSession::queuePipChanged() {
    // A slider drag produces a burst of fly/mask events; collapse each burst
    // into one pipChanged per trip through the event loop.
    if (m_pipChangePending.exchange(true)) return;
    QMetaObject::invokeMethod(this, [this]() {
        m_pipChangePending = false;
        emit pipChanged();
    }, Qt::QueuedConnection);
}
