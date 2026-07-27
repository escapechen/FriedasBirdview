#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class AutostartManager final : public QObject {
    Q_OBJECT

public:
    explicit AutostartManager(QObject *parent = nullptr);

    bool isSupported() const;
    bool isEnabled() const;
    void setEnabled(bool enabled, const QString &parentWindowId = {});

signals:
    void changeFinished(bool enabled, const QString &error);

private slots:
    void handlePortalResponse(uint response, const QVariantMap &results);

private:
    bool setNativeEnabled(bool enabled, QString *error) const;
    QString desktopEntryPath() const;
    QString desktopEntryContents() const;
    static QString escapeExecArgument(const QString &argument);
    static bool isPortalAvailable();
    static QString portalRequestPath(const QString &token);
    void finishPortalRequest();

    bool m_portalRequestPending = false;
    bool m_requestedEnabled = false;
    QString m_portalRequestPath;
};
