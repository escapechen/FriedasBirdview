#include "OverlayWindow.h"

#include "StreamView.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QtMath>

namespace {
class ClickableLabel final : public QLabel {
    Q_OBJECT

public:
    using QLabel::QLabel;

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
        }
        QLabel::mouseReleaseEvent(event);
    }
};

class DragButton final : public QToolButton {
    Q_OBJECT

public:
    using QToolButton::QToolButton;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragOrigin = event->globalPosition().toPoint();
            m_windowOrigin = window()->pos();
            m_usingSystemMove = false;
            // The normal Linux path deliberately runs via Xwayland so saved
            // coordinates can be restored. QWidget::move() is reliable there
            // and avoids starting KWin's compositor move protocol from this
            // frameless, non-activating tool window.
            if (QGuiApplication::platformName() == QStringLiteral("wayland")) {
                if (QWindow *windowHandle = window()->windowHandle()) {
                    m_usingSystemMove = windowHandle->startSystemMove();
                }
            }
            event->accept();
            return;
        }
        QToolButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!event->buttons().testFlag(Qt::LeftButton)) {
            event->accept();
            return;
        }
        if (m_usingSystemMove) {
            event->accept();
            return;
        }
        if (event->buttons().testFlag(Qt::LeftButton)) {
            window()->move(m_windowOrigin + event->globalPosition().toPoint() - m_dragOrigin);
            event->accept();
            return;
        }
        QToolButton::mouseMoveEvent(event);
    }

private:
    QPoint m_dragOrigin;
    QPoint m_windowOrigin;
    bool m_usingSystemMove = false;
};

class ResizeHandle final : public QToolButton {
    Q_OBJECT

public:
    using QToolButton::QToolButton;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QToolButton::mousePressEvent(event);
            return;
        }

        m_resizeOrigin = event->globalPosition().toPoint();
        m_initialSize = window()->size();
        m_usingSystemResize = false;
        if (QWindow *windowHandle = window()->windowHandle()) {
            m_usingSystemResize = windowHandle->startSystemResize(Qt::RightEdge | Qt::BottomEdge);
        }
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_usingSystemResize || !event->buttons().testFlag(Qt::LeftButton)) {
            event->accept();
            return;
        }

        const QPoint delta = event->globalPosition().toPoint() - m_resizeOrigin;
        const QSize requested = m_initialSize + QSize(delta.x(), delta.y());
        const QSize minimum = window()->minimumSize().expandedTo(QSize(320, 250));
        window()->resize(requested.expandedTo(minimum));
        event->accept();
    }

private:
    QPoint m_resizeOrigin;
    QSize m_initialSize;
    bool m_usingSystemResize = false;
};

QString shortTime(const QDateTime &time)
{
    return QLocale().toString(time.toLocalTime().time(), QLocale::ShortFormat);
}
}

OverlayWindow::OverlayWindow(QWidget *parent)
    : QWidget(parent)
    , m_snapshotTimer(new QTimer(this))
    , m_countdownTimer(new QTimer(this))
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setWindowTitle(QStringLiteral("FriedasBirdview feed"));
    setMinimumSize(320, 250);

    auto *frame = new QWidget(this);
    frame->setObjectName(QStringLiteral("feedFrame"));
    frame->setStyleSheet(QStringLiteral(
        "#feedFrame { background: #111820; border: 1px solid #344656; border-radius: 12px; }"
        "QToolButton { color: #f5f7fa; background: rgba(20, 28, 36, 220); border: 1px solid #627585; border-radius: 6px; padding: 3px; }"
        "QToolButton:hover { background: #33495e; }"));

    auto *windowLayout = new QVBoxLayout(this);
    windowLayout->setContentsMargins(4, 4, 4, 4);
    windowLayout->addWidget(frame);
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_activityLabel = new QLabel(frame);
    m_activityLabel->setStyleSheet(QStringLiteral("color: white; background: #bf3b3b; border-radius: 7px; padding: 6px 9px; font-weight: 700;"));
    m_activityLabel->setText(QStringLiteral("Live feed"));
    layout->addWidget(m_activityLabel, 0, Qt::AlignLeft);

    m_detailLabel = new QLabel(frame);
    m_detailLabel->setStyleSheet(QStringLiteral("color: #e7eff7; background: #276998; border-radius: 8px; padding: 4px 8px;"));
    layout->addWidget(m_detailLabel, 0, Qt::AlignLeft);

    auto *feedContainer = new QWidget(frame);
    feedContainer->setStyleSheet(QStringLiteral("background: #050505; border-radius: 8px;"));
    m_feedStack = new QStackedLayout(feedContainer);
    m_feedStack->setContentsMargins(0, 0, 0, 0);
    auto *snapshotLabel = new ClickableLabel(feedContainer);
    m_snapshotLabel = snapshotLabel;
    m_snapshotLabel->setAlignment(Qt::AlignCenter);
    m_snapshotLabel->setMinimumHeight(180);
    m_snapshotLabel->setText(QStringLiteral("Loading JPEG snapshot…"));
    m_snapshotLabel->setStyleSheet(QStringLiteral("color: #d5dde5;"));
    m_streamView = new StreamView(feedContainer);
    m_feedStack->addWidget(m_snapshotLabel);
    m_feedStack->addWidget(m_streamView);
    layout->addWidget(feedContainer, 1);

    m_errorLabel = new QLabel(frame);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #ffb4ab; background: #5a2020; border-radius: 6px; padding: 5px 8px;"));
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(6);
    m_countdownLabel = new QLabel(frame);
    m_countdownLabel->setStyleSheet(QStringLiteral("color: #d8e0e7; font-family: monospace;"));
    controls->addStretch();
    controls->addWidget(m_countdownLabel);
    m_zoomButton = new QToolButton(frame);
    m_zoomButton->setText(QStringLiteral("⤢"));
    m_zoomButton->setToolTip(QStringLiteral("Enlarge or restore feed"));
    controls->addWidget(m_zoomButton);
    auto *dragButton = new DragButton(frame);
    dragButton->setText(QStringLiteral("✋"));
    dragButton->setToolTip(QStringLiteral("Drag to move feed"));
    controls->addWidget(dragButton);
    auto *resizeHandle = new ResizeHandle(frame);
    resizeHandle->setText(QStringLiteral("↘"));
    resizeHandle->setToolTip(QStringLiteral("Drag to resize feed"));
    resizeHandle->setFixedSize(28, 28);
    controls->addWidget(resizeHandle);
    m_zoomButton->setFocusPolicy(Qt::NoFocus);
    dragButton->setFocusPolicy(Qt::NoFocus);
    resizeHandle->setFocusPolicy(Qt::NoFocus);
    layout->addLayout(controls);

    m_snapshotTimer->setInterval(500);
    connect(m_snapshotTimer, &QTimer::timeout, this, [this] {
        if (m_monitor && isVisible()) {
            m_monitor->requestSnapshot();
        }
    });
    m_countdownTimer->setInterval(250);
    connect(m_countdownTimer, &QTimer::timeout, this, [this] {
        if (!m_monitor || !m_monitor->isOverlayVisible()) {
            m_countdownLabel->clear();
            return;
        }
        const qint64 remaining = qMax<qint64>(0, QDateTime::currentDateTime().secsTo(m_monitor->overlayDismissalTime()));
        m_countdownLabel->setText(remaining > 0 ? QStringLiteral("%1s").arg(remaining) : QString());
    });

    connect(snapshotLabel, &ClickableLabel::clicked, this, &OverlayWindow::dismiss);
    connect(m_zoomButton, &QToolButton::clicked, this, &OverlayWindow::toggleZoom);
    connect(m_streamView, &StreamView::dismissRequested, this, &OverlayWindow::dismiss);
    connect(m_streamView, &StreamView::errorChanged, this, [this](const QString &message) {
        m_errorLabel->setText(message);
        m_errorLabel->show();
    });
    connect(m_streamView, &StreamView::streamConnected, this, [this] {
        m_errorLabel->hide();
    });
    connect(m_streamView, &StreamView::aspectRatioChanged, this, &OverlayWindow::applyAspectRatio);
}

void OverlayWindow::setMonitor(FrigateMonitor *monitor)
{
    m_monitor = monitor;
    connect(m_monitor, &FrigateMonitor::snapshotReady, this, [this](const QByteArray &data) {
        QPixmap snapshot;
        if (!snapshot.loadFromData(data)) {
            m_errorLabel->setText(QStringLiteral("Frigate returned an invalid JPEG snapshot."));
            m_errorLabel->show();
            return;
        }
        m_snapshot = snapshot;
        m_errorLabel->hide();
        applyAspectRatio(static_cast<double>(snapshot.width()) / static_cast<double>(snapshot.height()));
        updateSnapshotPixmap();
    });
    connect(m_monitor, &FrigateMonitor::snapshotFailed, this, [this](const QString &message) {
        if (m_monitor->feedMode() == FrigateMonitor::FeedMode::Jpeg && isVisible()) {
            m_errorLabel->setText(message);
            m_errorLabel->show();
        }
    });
    connect(m_monitor, &FrigateMonitor::activityChanged, this, [this](const FrigateMonitor::Activity &activity) {
        updateActivity(activity);
        if (isVisible()) {
            configureFeed();
        }
    });
    connect(m_monitor, &FrigateMonitor::settingsChanged, this, [this] {
        if (isVisible()) {
            configureFeed();
        }
    });
    connect(m_monitor, &FrigateMonitor::streamSessionChanged, this, [this] {
        if (isVisible() && m_monitor->feedMode() == FrigateMonitor::FeedMode::LiveStream) {
            configureFeed(true);
        }
    });
    connect(m_monitor, &FrigateMonitor::overlayDismissalTimeChanged, this, [this] {
        if (isVisible()) {
            m_countdownTimer->start();
        }
    });
}

void OverlayWindow::present()
{
    if (!m_monitor) {
        return;
    }
    restoreSavedGeometry();
    updateActivity(m_monitor->activity());
    configureFeed();
    show();
    m_countdownTimer->start();
}

void OverlayWindow::dismiss()
{
    if (m_monitor) {
        m_monitor->dismissFeed();
    }
}

void OverlayWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    saveGeometry();
}

void OverlayWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_expanded && event->size().width() != event->oldSize().width()) {
        m_collapsedWidth = qBound(320, event->size().width(), 1000);
    }
    updateSnapshotPixmap();
    saveGeometry();
}

void OverlayWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    dismiss();
}

void OverlayWindow::restoreSavedGeometry()
{
    if (m_geometryRestored) {
        return;
    }
    m_geometryRestored = true;

    QSettings settings;
    const QSize savedSize = settings.value("overlay/size", QSize(480, 360)).toSize();
    const int width = qBound(320, savedSize.width(), 1000);
    m_collapsedWidth = width;
    resize(width, qMax(250, savedSize.height()));

    QPoint savedPosition;
    if (settings.contains("overlay/position")) {
        savedPosition = settings.value("overlay/position").toPoint();
    } else if (QScreen *screen = QGuiApplication::primaryScreen()) {
        savedPosition = screen->availableGeometry().center() - QPoint(width / 2, height() / 2);
    }

    QRect savedFrame(savedPosition, size());
    bool onScreen = false;
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().intersects(savedFrame)) {
            onScreen = true;
            break;
        }
    }
    if (onScreen) {
        move(savedPosition);
    } else if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        move(available.center() - QPoint(width / 2, height() / 2));
    }
}

void OverlayWindow::saveGeometry()
{
    if (!m_geometryRestored || !isVisible()) {
        return;
    }
    QSettings settings;
    settings.setValue("overlay/size", size());
    settings.setValue("overlay/position", pos());
}

void OverlayWindow::updateActivity(const FrigateMonitor::Activity &activity)
{
    const QString camera = activity.camera.isEmpty() && m_monitor ? m_monitor->currentFeedCameraName() : activity.camera;
    if (activity.title.isEmpty()) {
        m_activityLabel->setText(QStringLiteral("Live feed"));
        m_detailLabel->setText(camera);
        return;
    }
    m_activityLabel->setText(activity.confidence.isEmpty()
        ? QStringLiteral("◉ %1").arg(activity.title)
        : QStringLiteral("◉ %1  %2").arg(activity.title, activity.confidence));
    m_detailLabel->setText(QStringLiteral("%1 • %2").arg(camera, shortTime(activity.timestamp)));
}

void OverlayWindow::configureFeed(bool force)
{
    if (!m_monitor) {
        return;
    }
    const FrigateMonitor::FeedMode mode = m_monitor->feedMode();
    const QString camera = m_monitor->currentFeedCameraName();
    const QString stream = m_monitor->currentFeedStreamName();
    if (!force && mode == m_shownMode && camera == m_shownCamera
        && (mode != FrigateMonitor::FeedMode::LiveStream || stream == m_shownStream)) {
        return;
    }

    m_shownMode = mode;
    m_shownCamera = camera;
    m_shownStream = stream;
    m_errorLabel->hide();
    if (mode == FrigateMonitor::FeedMode::Jpeg) {
        m_streamView->stop();
        m_feedStack->setCurrentWidget(m_snapshotLabel);
        m_snapshotLabel->setText(QStringLiteral("Loading JPEG snapshot…"));
        m_snapshotTimer->start();
        m_monitor->requestSnapshot();
    } else {
        m_snapshotTimer->stop();
        m_feedStack->setCurrentWidget(m_streamView);
        m_streamView->start(m_monitor->baseUrl(), stream, m_monitor->authenticationCookies());
    }
}

void OverlayWindow::updateSnapshotPixmap()
{
    if (m_snapshot.isNull() || !m_snapshotLabel) {
        return;
    }
    m_snapshotLabel->setPixmap(m_snapshot.scaled(m_snapshotLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void OverlayWindow::applyAspectRatio(double aspectRatio)
{
    if (!qIsFinite(aspectRatio) || aspectRatio < 0.25 || aspectRatio > 8.0) {
        return;
    }

    // MSE can report metadata more than once while new segments arrive. Reapplying
    // a top-level resize for the same stream dimensions makes KWin reposition the
    // embedded view, which looks like the picture is slowly drifting downward.
    if (m_hasAppliedAspectRatio && qAbs(m_aspectRatio - aspectRatio) < 0.01) {
        return;
    }
    m_aspectRatio = aspectRatio;
    m_hasAppliedAspectRatio = true;

    QWidget *feedContainer = m_feedStack->parentWidget();
    const int currentFeedHeight = feedContainer ? feedContainer->height() : 0;
    if (!isVisible() || currentFeedHeight <= 0) {
        return;
    }

    const int chromeHeight = qMax(86, height() - currentFeedHeight);
    const int feedHeight = qRound(width() / m_aspectRatio);
    const int newHeight = qMax(250, feedHeight + chromeHeight);
    if (qAbs(height() - newHeight) > 1) {
        resize(width(), newHeight);
    }
}

void OverlayWindow::toggleZoom()
{
    m_expanded = !m_expanded;
    m_zoomButton->setText(m_expanded ? QStringLiteral("⤡") : QStringLiteral("⤢"));
    const int width = m_expanded ? 800 : m_collapsedWidth;
    const int chromeHeight = qMax(86, height() - m_feedStack->geometry().height());
    resize(width, qMax(250, qRound(width / m_aspectRatio) + chromeHeight));
}

#include "OverlayWindow.moc"
