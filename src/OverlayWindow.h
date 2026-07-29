#pragma once

#include <QList>
#include <QPixmap>
#include <QSslCertificate>
#include <QWidget>

#include "FrigateMonitor.h"

class QLabel;
class QScreen;
class QStackedLayout;
class QTimer;
class QToolButton;
class StreamView;

class OverlayWindow final : public QWidget {
    Q_OBJECT

public:
    explicit OverlayWindow(const QList<QSslCertificate> &customCaCertificates, QWidget *parent = nullptr);

    void setMonitor(FrigateMonitor *monitor);
    void present();
    void dismiss();

protected:
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void restoreSavedGeometry();
    void saveGeometry();
    void updateActivity(const FrigateMonitor::Activity &activity);
    void configureFeed(bool force = false);
    void updateSnapshotPixmap();
    void applyAspectRatio(double aspectRatio);
    void toggleZoom();

    FrigateMonitor *m_monitor = nullptr;
    QLabel *m_activityLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    QLabel *m_snapshotLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QLabel *m_countdownLabel = nullptr;
    QToolButton *m_zoomButton = nullptr;
    QStackedLayout *m_feedStack = nullptr;
    StreamView *m_streamView = nullptr;
    QTimer *m_snapshotTimer = nullptr;
    QTimer *m_countdownTimer = nullptr;
    QPixmap m_snapshot;
    QString m_shownCamera;
    QString m_shownStream;
    FrigateMonitor::FeedMode m_shownMode = FrigateMonitor::FeedMode::Jpeg;
    double m_aspectRatio = 16.0 / 9.0;
    bool m_hasAppliedAspectRatio = false;
    int m_collapsedWidth = 480;
    bool m_expanded = false;
    bool m_geometryRestored = false;
};
