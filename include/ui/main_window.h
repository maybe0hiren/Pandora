#pragma once

#include "core/pandora_controller.h"
#include "ui/media_viewer_dialog.h"
#include "ui/text_editor_dialog.h"
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>

namespace pandora {

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void createSession();
    void joinSelectedSession();
    void onDiscoveredSessionClicked();
    void uploadFile();
    void uploadFolder();
    void editSelectedTextFile();
    void onFileTableCellClicked(int row, int column);
    void downloadSelected();
    void downloadAll();
    void deleteSelected();
    void initiateShutdownCountdown();
    void deactivateImmediate();
    void applySettings();

    void refreshDashboard();
    void refreshDiscoveredSessions();
    void onTransferProgress(const pandora::TransferProgress& progress);
    void onFileContentReady(const QString& fileId, const QByteArray& content);
    void onFileReplicaReadyForViewing(const QString& fileId, const QString& replicaBlobPath);
    void onFileContentFetchFailed(const QString& fileId, const QString& reason);
    void onShutdownCountdownUpdated(int secondsRemaining);
    void appendLog(const QString& line);

private:
    static QString formatBytes(qint64 bytes);
    QString selectedFileId() const;
    QString selectedFileName() const;
    void openTextEditorForFile(const QString& fileId);
    void openMediaViewerForFile(const QString& fileId);

    PandoraController controller_;
    QHash<QString, QPointer<TextEditorDialog>> openEditors_;
    QHash<QString, QPointer<MediaViewerDialog>> openMediaViewers_;

    // Session tabs (Create / Join / Settings)
    QTabWidget* sessionTabs_{nullptr};

    // Create Tab
    QLineEdit* pandoraName_{nullptr};
    QLineEdit* userName_{nullptr};
    QLineEdit* key_{nullptr};
    QPushButton* createButton_{nullptr};

    // Join Tab
    QListWidget* discoveredList_{nullptr};
    QLineEdit* joinUserName_{nullptr};
    QLineEdit* joinKey_{nullptr};
    QLineEdit* joinHostAddress_{nullptr};
    QSpinBox* joinHostPort_{nullptr};
    QLineEdit* joinPandoraId_{nullptr};
    QPushButton* joinButton_{nullptr};

    // Settings Tab
    QSpinBox* replicationFactorSpin_{nullptr};
    QDoubleSpinBox* quotaGiBSpin_{nullptr};
    QSpinBox* tcpPortSpin_{nullptr};
    QPushButton* applySettingsButton_{nullptr};

    // Status & Shutdown Banner
    QLabel* status_{nullptr};
    QLabel* roleBadge_{nullptr};
    QLabel* nodeCount_{nullptr};
    QLabel* cloudStorageSummary_{nullptr};
    QWidget* shutdownBanner_{nullptr};
    QLabel* shutdownBannerLabel_{nullptr};
    QPushButton* cancelShutdownButton_{nullptr};

    // Files Table & Transfer Progress
    QTableWidget* filesTable_{nullptr};
    QPushButton* uploadButton_{nullptr};
    QPushButton* uploadFolderButton_{nullptr};
    QPushButton* editTextButton_{nullptr};
    QPushButton* downloadButton_{nullptr};
    QPushButton* downloadAllButton_{nullptr};
    QPushButton* deleteButton_{nullptr};
    QProgressBar* transferProgressBar_{nullptr};
    QLabel* transferStatusLabel_{nullptr};

    // Peers Table & Logs
    QTableWidget* peersTable_{nullptr};
    QPlainTextEdit* logs_{nullptr};

    // Deactivation Controls
    QPushButton* countdownShutdownButton_{nullptr};
    QPushButton* deactivateButton_{nullptr};
};

} // namespace pandora
