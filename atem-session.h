#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

#include "atem-controller.h"
#include "pip-presets.h"

// One ATEM connection shared by every panel (macros, PiP).
//
// Owns the AtemController and turns its callbacks, which arrive on BMD SDK
// threads, into Qt signals delivered on the UI thread. Panels never talk to
// the SDK callbacks directly.
//
// Remembers how the last successful connection was made (USB or IP) so
// autoConnect() reconnects the same way on the next OBS start.
class AtemSession : public QObject {
    Q_OBJECT
public:
    explicit AtemSession(QObject* parent = nullptr);
    ~AtemSession() override;

    AtemController& atem() { return m_atem; }
    AtemPip& pip() { return m_atem.pip(); }
    AtemState state() const { return m_atem.state(); }
    bool isConnected() const { return m_atem.state() == AtemState::Connected; }
    bool isBusy() const { return m_busy; }

    QString lastIP() const;
    bool lastWasIP() const;

    // The connection log so far (newest last, at most kTraceHistory lines),
    // for the settings dialog; traceMessage() delivers new lines.
    const QStringList& traceHistory() const { return m_traceHistory; }

    // All blocking: BMD's ConnectTo runs on the calling (UI) thread.
    // Recalls a saved view on the switcher. presetRecalling() goes out first,
    // so a panel with its own edits in flight drops them and can't send the
    // old values over the recalled view afterwards.
    PipRecallResult recallPreset(const PipPreset& preset);

    bool connectUSB();
    bool connectIP(const QString& address);
    bool autoConnect();
    void disconnect();
    void shutdown();

signals:
    void connectionChanged(AtemState state);
    void macrosChanged();
    void pipChanged();
    void presetRecalling();
    void traceMessage(const QString& line);

private:
    bool finishConnect(bool ok, bool viaIP, const QString& address);
    void queuePipChanged();

    static constexpr int kTraceHistory = 500;

    AtemController m_atem;
    QStringList m_traceHistory;
    bool m_busy = false;
    bool m_shutdownPending = false;   // asked to shut down during a ConnectTo
    std::atomic<bool> m_pipChangePending{false};
};
