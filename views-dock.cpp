#include "views-dock.h"
#include "panel-common.h"
#include "pip-presets.h"
#include "pip-settings.h"
#include "pip-widgets.h"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {

constexpr int kColumns = 2;
constexpr int kMargin = 6;
constexpr int kSpacing = 4;

const char* const kViewsExtraStyle = R"(
    #settingsTitle {
        font-size: 12px;
        font-weight: 600;
        color: #e0e0e0;
    }
    QListWidget {
        background: #1e1e1e;
        border: 1px solid #3c3c3c;
        color: #cccccc;
        font-size: 11px;
    }
    QListWidget::item {
        padding: 2px;
    }
    QListWidget::item:selected {
        background: #094771;
    }
    QListWidget QScrollBar:vertical {
        width: 6px;
        background: #1e1e1e;
        margin: 0;
    }
    QListWidget QScrollBar::handle:vertical {
        background: #4a4a4a;
        border-radius: 3px;
        min-height: 20px;
    }
    QListWidget QScrollBar::add-line:vertical, QListWidget QScrollBar::sub-line:vertical {
        height: 0;
    }
    QListWidget QScrollBar::add-page:vertical, QListWidget QScrollBar::sub-page:vertical {
        background: none;
    }
)";

QIcon thumbnailIcon(const PipPreset& p) {
    QPixmap pix(64, 36);
    pix.fill(QColor("#181818"));
    if (p.valid && !p.thumbnail.isNull()) {
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(pix.rect(), p.thumbnail);
    }
    return QIcon(pix);
}

} // namespace

AtemViewsDock::AtemViewsDock(AtemSession* session, PipSettings* settings, QWidget* parent)
    : QWidget(parent), m_session(session), m_settings(settings)
{
    applyPanelStyle(this, kViewsExtraStyle);
    buildUI();

    connect(m_session, &AtemSession::connectionChanged, this, [this](AtemState) { updatePage(); });
    connect(m_session, &AtemSession::pipChanged, this, &AtemViewsDock::updateLit);
    connect(m_settings, &PipSettings::changed, this, [this]() {
        rebuildGrid();
        if (m_showingSettings && !m_fillingList) fillSettingsList();
    });

    rebuildGrid();
    updatePage();
}

void AtemViewsDock::buildUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_header = new PanelHeader("ATEM VIEWS", this);
    auto* settingsBtn = m_header->addToolButton("⚙", "Choose which views are shown, and their order");
    connect(settingsBtn, &QToolButton::clicked, this, [this]() {
        m_showingSettings = true;
        fillSettingsList();
        updatePage();
    });
    mainLayout->addWidget(m_header);

    m_pages = new QStackedWidget(this);
    mainLayout->addWidget(m_pages, 1);

    // Offline page (same connection as the other panels)
    m_offlinePage = new QWidget(m_pages);
    auto* offLayout = new QVBoxLayout(m_offlinePage);
    offLayout->setContentsMargins(14, 20, 14, 20);
    offLayout->setAlignment(Qt::AlignCenter);
    m_offlineText = new QLabel(m_offlinePage);
    m_offlineText->setObjectName("hintText");
    m_offlineText->setAlignment(Qt::AlignCenter);
    m_offlineText->setWordWrap(true);
    offLayout->addWidget(m_offlineText);
    m_connectBtn = new QPushButton(m_offlinePage);
    m_connectBtn->setObjectName("primaryBtn");
    connect(m_connectBtn, &QPushButton::clicked, this, [this]() { m_session->autoConnect(); });
    offLayout->addWidget(m_connectBtn);
    m_pages->addWidget(m_offlinePage);

    // Main page: the views in two columns
    auto* scroll = new QScrollArea(m_pages);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gridBody = new QWidget();
    m_gridBody->setObjectName("scrollBody");
    m_gridBody->installEventFilter(this);   // layoutGrid() on resize
    auto* bodyLayout = new QVBoxLayout(m_gridBody);
    bodyLayout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    bodyLayout->setSpacing(kSpacing);
    m_emptyHint = new QLabel("No views chosen.\nPick them with ⚙ — they are the buttons saved "
                             "in the PiP panel.", m_gridBody);
    m_emptyHint->setObjectName("hintText");
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    bodyLayout->addWidget(m_emptyHint);
    m_grid = new QGridLayout();
    m_grid->setSpacing(kSpacing);
    bodyLayout->addLayout(m_grid);
    bodyLayout->addStretch(1);
    scroll->setWidget(m_gridBody);
    m_mainPage = scroll;
    m_pages->addWidget(m_mainPage);

    m_settingsPage = buildSettingsPage();
    m_pages->addWidget(m_settingsPage);
}

QWidget* AtemViewsDock::buildSettingsPage() {
    auto* page = new QWidget(m_pages);
    page->setObjectName("scrollBody");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* head = new QHBoxLayout();
    auto* title = new QLabel("Views", page);
    title->setObjectName("settingsTitle");
    head->addWidget(title, 1);
    auto* doneBtn = new QPushButton("Done", page);
    connect(doneBtn, &QPushButton::clicked, this, [this]() {
        m_showingSettings = false;
        updatePage();
    });
    head->addWidget(doneBtn);
    layout->addLayout(head);

    auto* hint = new QLabel("Tick the PiP buttons to show here. Their order is the order in the "
                            "panel, left to right, top to bottom: drag a row, or use ▲ ▼.", page);
    hint->setObjectName("hintText");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* row = new QHBoxLayout();
    row->setSpacing(4);
    m_list = new QListWidget(page);
    m_list->setIconSize(QSize(64, 36));
    m_list->setWordWrap(true);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAccessibleName("Views to show");
    connect(m_list, &QListWidget::itemChanged, this, [this]() { if (!m_fillingList) storeSettingsList(); });
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this]() { if (!m_fillingList) storeSettingsList(); });
    row->addWidget(m_list, 1);

    auto* arrows = new QVBoxLayout();
    arrows->setSpacing(4);
    auto* up = new QToolButton(page);
    up->setText("▲");
    up->setToolTip("Move the selected view earlier");
    connect(up, &QToolButton::clicked, this, [this]() { moveSelected(-1); });
    auto* down = new QToolButton(page);
    down->setText("▼");
    down->setToolTip("Move the selected view later");
    connect(down, &QToolButton::clicked, this, [this]() { moveSelected(+1); });
    arrows->addWidget(up);
    arrows->addWidget(down);
    arrows->addStretch(1);
    row->addLayout(arrows);
    layout->addLayout(row, 1);
    return page;
}

// ── Main page ────────────────────────────────────────────────

void AtemViewsDock::rebuildGrid() {
    for (auto& [index, btn] : m_buttons) {
        m_grid->removeWidget(btn);
        btn->hide();
        btn->deleteLater();
    }
    m_buttons.clear();

    const QList<int>& order = m_settings->viewSlots();
    for (int i = 0; i < order.size(); ++i) {
        int index = order[i];
        auto* btn = new PresetButton(index + 1, m_gridBody);
        btn->setPreset(m_settings->preset(index));
        btn->setToolTip(describe(index));
        btn->setAccessibleName(QString("View %1").arg(index + 1));
        connect(btn, &QAbstractButton::clicked, this, [this, index]() {
            const PipPreset& p = m_settings->preset(index);
            if (!p.valid || !m_session->isConnected() || m_session->isBusy()) return;
            recallPipPreset(m_session->pip(), p, m_session->pip().state());
        });
        m_grid->addWidget(btn, i / kColumns, i % kColumns);
        m_buttons.emplace_back(index, btn);
    }
    m_emptyHint->setVisible(order.isEmpty());
    layoutGrid();
    updateLit();
}

void AtemViewsDock::layoutGrid() {
    int w = m_gridBody->width() - 2 * kMargin;
    if (w <= 0) return;
    int buttonWidth = (w - (kColumns - 1) * kSpacing) / kColumns;
    int buttonHeight = static_cast<int>(std::lround(buttonWidth * 9.0 / 16));
    for (auto& [index, btn] : m_buttons) btn->setFixedHeight(buttonHeight);
}

void AtemViewsDock::updateLit() {
    if (!m_session->isConnected() || m_session->isBusy()) {
        for (auto& [index, btn] : m_buttons) btn->setLit(false);
        return;
    }
    AtemPipState state = m_session->pip().state();
    for (auto& [index, btn] : m_buttons) btn->setLit(pipPresetMatches(m_settings->preset(index), state));
}

void AtemViewsDock::updatePage() {
    AtemState state = m_session->isBusy() ? AtemState::Connecting : m_session->state();
    m_header->setConnectionState(state, state == AtemState::Connected
        ? QString::fromStdString(m_session->atem().modelName())
        : state == AtemState::Connecting ? QString("connecting…") : QString());

    if (m_showingSettings) {
        m_pages->setCurrentWidget(m_settingsPage);
    } else if (state == AtemState::Connected) {
        m_pages->setCurrentWidget(m_mainPage);
        updateLit();
    } else {
        QString error = QString::fromStdString(m_session->atem().lastError());
        m_offlineText->setText(state == AtemState::Connecting ? "Connecting…"
                               : error.isEmpty() ? "ATEM not connected." : "ATEM not connected.\n" + error);
        m_connectBtn->setText(m_session->lastWasIP() ? "Connect to " + m_session->lastIP()
                                                     : QString("Connect via USB (auto-detect)"));
        m_connectBtn->setVisible(state != AtemState::Connecting);
        m_pages->setCurrentWidget(m_offlinePage);
    }
}

QString AtemViewsDock::describe(int index) const {
    const PipPreset& p = m_settings->preset(index);
    if (!p.valid) return QString("Button %1 — empty. Save it in the PiP panel.").arg(index + 1);
    return QString("Button %1: main %2 · PiP %3")
        .arg(index + 1).arg(m_settings->name(p.programInput))
        .arg(p.onAir ? m_settings->name(p.pipInput) : QString("off"));
}

bool AtemViewsDock::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_gridBody && event->type() == QEvent::Resize) layoutGrid();
    return QWidget::eventFilter(watched, event);
}

// ── Settings page ────────────────────────────────────────────

// Shown views first in their order, then the rest by number.
void AtemViewsDock::fillSettingsList() {
    m_fillingList = true;
    int selected = m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toInt() : -1;
    m_list->clear();
    QList<int> rows = m_settings->viewSlots();
    for (int i = 0; i < PipSettings::kPresetCount; ++i)
        if (!rows.contains(i)) rows.append(i);
    for (int index : rows) {
        const PipPreset& p = m_settings->preset(index);
        // Two short lines, so a narrow dock needs no sideways scrolling.
        QString text = p.valid
            ? QString("Button %1\n%2 · PiP %3").arg(index + 1).arg(m_settings->name(p.programInput))
                  .arg(p.onAir ? m_settings->name(p.pipInput) : QString("off"))
            : QString("Button %1\n(empty)").arg(index + 1);
        auto* item = new QListWidgetItem(thumbnailIcon(p), text);
        item->setToolTip(describe(index));
        item->setData(Qt::UserRole, index);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        item->setCheckState(m_settings->viewSlots().contains(index) ? Qt::Checked : Qt::Unchecked);
        m_list->addItem(item);
        if (index == selected) m_list->setCurrentItem(item);
    }
    m_fillingList = false;
}

void AtemViewsDock::storeSettingsList() {
    QList<int> order;
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* item = m_list->item(row);
        if (item->checkState() == Qt::Checked) order.append(item->data(Qt::UserRole).toInt());
    }
    m_fillingList = true;          // the change notification must not rebuild the list under the mouse
    m_settings->setViewSlots(order);
    m_fillingList = false;
}

void AtemViewsDock::moveSelected(int delta) {
    int row = m_list->currentRow();
    int to = row + delta;
    if (row < 0 || to < 0 || to >= m_list->count()) return;
    m_fillingList = true;
    QListWidgetItem* item = m_list->takeItem(row);
    m_list->insertItem(to, item);
    m_list->setCurrentRow(to);
    m_fillingList = false;
    storeSettingsList();
}
