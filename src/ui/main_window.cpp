#include "ui/main_window.h"
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

namespace pandora {

QString MainWindow::formatBytes(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024) return QString("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    return QString("%1 GiB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Pandora — Decentralized Peer-to-Peer LAN Cloud");
    resize(1240, 800);

    auto* root = new QWidget(this);
    auto* main = new QVBoxLayout(root);
    main->setContentsMargins(14, 12, 14, 12);
    main->setSpacing(10);

    // 1. Header Row (Title + Offline/Online Role Badge)
    auto* headerLayout = new QHBoxLayout;
    auto* title = new QLabel(
        "<h1 style='margin:0;color:#66b2ff;font-size:22px;'>Pandora</h1>"
        "<span style='color:#9aa4b8;font-size:12px;'>Session-based decentralized peer-to-peer LAN storage cloud</span>");
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    roleBadge_ = new QLabel("Offline");
    roleBadge_->setStyleSheet("background:#2b303c;color:#aab2c0;padding:6px 14px;border-radius:4px;font-weight:bold;");
    headerLayout->addWidget(roleBadge_);
    main->addLayout(headerLayout);

    // 2. Status Summary Bar (Directly under headers)
    auto* statusFrame = new QWidget;
    statusFrame->setStyleSheet("background:#1a1e26;border:1px solid #2b313d;border-radius:5px;");
    auto* statusRow = new QHBoxLayout(statusFrame);
    statusRow->setContentsMargins(12, 6, 12, 6);
    status_ = new QLabel("Pandora Inactive");
    status_->setStyleSheet("font-weight:bold;color:#7ec8ff;border:none;");
    nodeCount_ = new QLabel("Peers Online: 0");
    nodeCount_->setStyleSheet("color:#c8d0e0;font-weight:600;border:none;");
    cloudStorageSummary_ = new QLabel("Cloud Used: 0 B / Allocated Capacity: 0 B");
    cloudStorageSummary_->setStyleSheet("color:#aab2c0;border:none;");
    statusRow->addWidget(status_);
    statusRow->addStretch();
    statusRow->addWidget(cloudStorageSummary_);
    statusRow->addSpacing(24);
    statusRow->addWidget(nodeCount_);
    main->addWidget(statusFrame);

    // 3. Shutdown Countdown Warning Banner (hidden unless shutdown is active)
    shutdownBanner_ = new QWidget;
    shutdownBanner_->setStyleSheet("background:#5a1e24;border:1px solid #d9534f;border-radius:6px;padding:6px;");
    auto* bannerLayout = new QHBoxLayout(shutdownBanner_);
    shutdownBannerLabel_ = new QLabel("Shutdown countdown active!");
    shutdownBannerLabel_->setStyleSheet("color:#ffd6d6;font-weight:bold;");
    cancelShutdownButton_ = new QPushButton("Cancel Shutdown");
    cancelShutdownButton_->setStyleSheet("background:#8c2d35;color:white;padding:5px 12px;");
    bannerLayout->addWidget(shutdownBannerLabel_, 1);
    bannerLayout->addWidget(cancelShutdownButton_);
    shutdownBanner_->setVisible(false);
    main->addWidget(shutdownBanner_);

    // 4. Main Horizontal Split:
    //    Left Half  = Pandora Distributed Files (Full height below headers & status)
    //    Right Half = Top: Create / Join / Settings Tabs
    //                 Middle: Connected Peers & Storage Utilization
    //                 Bottom: Session Audit & Diagnostic Logs
    auto* mainSplit = new QSplitter(Qt::Horizontal);
    mainSplit->setHandleWidth(8);

    // ==================== LEFT HALF: Pandora Distributed Files ====================
    auto* fileBox = new QWidget;
    auto* fileLayout = new QVBoxLayout(fileBox);
    fileLayout->setContentsMargins(0, 0, 6, 0);
    fileLayout->setSpacing(8);

    auto* filesHeaderRow = new QHBoxLayout;
    auto* filesTitle = new QLabel("<b style='font-size:13px;color:#e8eaf0;'>Pandora Distributed Files</b>");
    auto* filesHint = new QLabel("<span style='color:#8792a6;font-size:11px;'>Click any text/code file to edit, or image/video file to view in-app</span>");
    filesHeaderRow->addWidget(filesTitle);
    filesHeaderRow->addStretch();
    filesHeaderRow->addWidget(filesHint);
    fileLayout->addLayout(filesHeaderRow);

    filesTable_ = new QTableWidget(0, 5);
    filesTable_->setHorizontalHeaderLabels({"File / Folder Path", "Size", "Replicas", "Version", "SHA-256"});
    filesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    filesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    filesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    filesTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    filesTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    filesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    filesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    filesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    filesTable_->setAlternatingRowColors(true);
    filesTable_->verticalHeader()->setVisible(false);
    fileLayout->addWidget(filesTable_, 1);

    auto* fileButtons = new QHBoxLayout;
    fileButtons->setSpacing(6);
    uploadButton_ = new QPushButton("Upload File");
    uploadFolderButton_ = new QPushButton("Upload Folder");
    editTextButton_ = new QPushButton("Open / Edit Selected");
    downloadButton_ = new QPushButton("Download Selected");
    downloadAllButton_ = new QPushButton("Download All");
    deleteButton_ = new QPushButton("Delete Selected");
    fileButtons->addWidget(uploadButton_);
    fileButtons->addWidget(uploadFolderButton_);
    fileButtons->addWidget(editTextButton_);
    fileButtons->addWidget(downloadButton_);
    fileButtons->addWidget(downloadAllButton_);
    fileButtons->addWidget(deleteButton_);
    fileLayout->addLayout(fileButtons);

    auto* progressRow = new QHBoxLayout;
    transferStatusLabel_ = new QLabel("Transfer Status: Idle");
    transferStatusLabel_->setStyleSheet("color:#9aa4b8;font-size:12px;");
    transferProgressBar_ = new QProgressBar;
    transferProgressBar_->setRange(0, 100);
    transferProgressBar_->setValue(0);
    transferProgressBar_->setFixedHeight(18);
    progressRow->addWidget(transferStatusLabel_);
    progressRow->addWidget(transferProgressBar_, 1);
    fileLayout->addLayout(progressRow);

    mainSplit->addWidget(fileBox);

    // ==================== RIGHT HALF: Controls + Peers + Logs ====================
    auto* rightSplit = new QSplitter(Qt::Vertical);
    rightSplit->setHandleWidth(8);

    // Right-Top: Session Control Tabs (Create | Join | Settings & Quotas)
    auto* topRightBox = new QWidget;
    topRightBox->setMinimumHeight(90);
    topRightBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* topRightLayout = new QVBoxLayout(topRightBox);
    topRightLayout->setContentsMargins(6, 0, 0, 4);
    topRightLayout->setSpacing(0);
    sessionTabs_ = new QTabWidget;
    sessionTabs_->setMinimumHeight(80);
    sessionTabs_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto wrapScroll = [](QWidget* content) -> QScrollArea* {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setMinimumHeight(40);
        scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        scroll->setWidget(content);
        return scroll;
    };

    // Tab 1: Create Pandora
    auto* createTab = new QWidget;
    auto* createForm = new QFormLayout(createTab);
    createForm->setContentsMargins(12, 10, 12, 10);
    createForm->setSpacing(6);
    pandoraName_ = new QLineEdit("My Pandora");
    pandoraName_->setPlaceholderText("Enter a session name (e.g. TeamStorage)");
    userName_ = new QLineEdit("Peer-1");
    userName_->setPlaceholderText("Enter your display name");
    key_ = new QLineEdit;
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText("Minimum 6 characters (PBKDF2-HMAC-SHA256)");
    createButton_ = new QPushButton("Create Pandora Session");
    createForm->addRow("Pandora Name:", pandoraName_);
    createForm->addRow("Your Peer Name:", userName_);
    createForm->addRow("Pandora Key:", key_);
    createForm->addRow("", createButton_);
    sessionTabs_->addTab(wrapScroll(createTab), "Create Pandora");

    // Tab 2: Join Pandora
    auto* joinTab = new QWidget;
    auto* joinLayout = new QHBoxLayout(joinTab);
    joinLayout->setContentsMargins(10, 8, 10, 8);
    joinLayout->setSpacing(10);

    auto* discBox = new QVBoxLayout;
    discBox->addWidget(new QLabel("Discovered LAN Sessions (Click to select):"));
    discoveredList_ = new QListWidget;
    discoveredList_->setMinimumHeight(40);
    discBox->addWidget(discoveredList_, 1);
    joinLayout->addLayout(discBox, 1);

    auto* joinFormWidget = new QWidget;
    auto* joinForm = new QFormLayout(joinFormWidget);
    joinForm->setContentsMargins(4, 0, 0, 0);
    joinForm->setSpacing(4);
    joinUserName_ = new QLineEdit("Peer-2");
    joinUserName_->setPlaceholderText("Enter your display name");
    joinKey_ = new QLineEdit;
    joinKey_->setEchoMode(QLineEdit::Password);
    joinKey_->setPlaceholderText("Enter session Pandora Key");
    joinHostAddress_ = new QLineEdit("127.0.0.1");
    joinHostPort_ = new QSpinBox;
    joinHostPort_->setRange(1, 65535);
    joinHostPort_->setValue(43170);
    joinPandoraId_ = new QLineEdit;
    joinPandoraId_->setPlaceholderText("Optional (auto-filled on click)");
    joinButton_ = new QPushButton("Authenticate && Join Pandora");

    joinForm->addRow("Your Peer Name:", joinUserName_);
    joinForm->addRow("Pandora Key:", joinKey_);
    joinForm->addRow("Host IP Address:", joinHostAddress_);
    joinForm->addRow("Host TCP Port:", joinHostPort_);
    joinForm->addRow("Session ID:", joinPandoraId_);
    joinForm->addRow("", joinButton_);
    joinLayout->addWidget(joinFormWidget, 1);
    sessionTabs_->addTab(wrapScroll(joinTab), "Join Pandora (LAN)");

    // Tab 3: Settings & Quotas
    auto* settingsTab = new QWidget;
    auto* settingsForm = new QFormLayout(settingsTab);
    settingsForm->setContentsMargins(12, 10, 12, 10);
    settingsForm->setSpacing(6);
    replicationFactorSpin_ = new QSpinBox;
    replicationFactorSpin_->setRange(1, 10);
    replicationFactorSpin_->setValue(controller_.replicationFactor());
    quotaGiBSpin_ = new QDoubleSpinBox;
    quotaGiBSpin_->setRange(0.1, 1024.0);
    quotaGiBSpin_->setDecimals(2);
    quotaGiBSpin_->setSuffix(" GiB");
    quotaGiBSpin_->setValue(5.0);
    tcpPortSpin_ = new QSpinBox;
    tcpPortSpin_->setRange(1024, 65535);
    tcpPortSpin_->setValue(43170);
    applySettingsButton_ = new QPushButton("Apply Storage && Network Settings");

    settingsForm->addRow("Target Replication Factor:", replicationFactorSpin_);
    settingsForm->addRow("Local Storage Quota Allocation:", quotaGiBSpin_);
    settingsForm->addRow("Preferred Local TCP Port:", tcpPortSpin_);
    settingsForm->addRow("", applySettingsButton_);
    sessionTabs_->addTab(wrapScroll(settingsTab), "Settings && Quotas");

    topRightLayout->addWidget(sessionTabs_);
    rightSplit->addWidget(topRightBox);

    // Right-Middle: Connected Peers & Storage Utilization
    auto* peerBox = new QWidget;
    peerBox->setMinimumHeight(110);
    auto* peerLayout = new QVBoxLayout(peerBox);
    peerLayout->setContentsMargins(6, 4, 0, 4);
    peerLayout->setSpacing(6);
    peerLayout->addWidget(new QLabel("<b style='font-size:13px;color:#e8eaf0;'>Connected Peers && Storage Utilization</b>"));

    peersTable_ = new QTableWidget(0, 5);
    peersTable_->setMinimumHeight(70);
    peersTable_->setHorizontalHeaderLabels({"Peer Name", "Role", "Status / Endpoint", "Used / Quota", "Free Disk"});
    peersTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    peersTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    peersTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    peersTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    peersTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    peersTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    peersTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    peersTable_->setAlternatingRowColors(true);
    peersTable_->verticalHeader()->setVisible(false);
    peerLayout->addWidget(peersTable_, 1);
    rightSplit->addWidget(peerBox);

    // Right-Bottom: Session Audit & Diagnostic Logs
    auto* logBox = new QWidget;
    logBox->setMinimumHeight(100);
    auto* logLayout = new QVBoxLayout(logBox);
    logLayout->setContentsMargins(6, 4, 0, 0);
    logLayout->setSpacing(6);
    logLayout->addWidget(new QLabel("<b style='font-size:13px;color:#e8eaf0;'>Session Audit && Diagnostic Logs</b>"));
    logs_ = new QPlainTextEdit;
    logs_->setMinimumHeight(60);
    logs_->setReadOnly(true);
    logLayout->addWidget(logs_, 1);
    rightSplit->addWidget(logBox);

    rightSplit->setChildrenCollapsible(false);
    rightSplit->setStretchFactor(0, 1);
    rightSplit->setStretchFactor(1, 1);
    rightSplit->setStretchFactor(2, 1);
    rightSplit->setSizes({240, 220, 200});

    mainSplit->addWidget(rightSplit);
    mainSplit->setStretchFactor(0, 1);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setSizes({600, 600});
    main->addWidget(mainSplit, 1);

    // 5. Bottom Deactivation Bar
    auto* bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(10);
    countdownShutdownButton_ = new QPushButton("Guardian 2-Min Shutdown Countdown");
    countdownShutdownButton_->setEnabled(false);
    deactivateButton_ = new QPushButton("Leave / Deactivate Immediately");
    deactivateButton_->setEnabled(false);
    deactivateButton_->setStyleSheet(
        "QPushButton { background:#7a2e36; color:white; padding:8px 14px; border-radius:4px; font-weight:600; }"
        "QPushButton:hover { background:#943842; }"
        "QPushButton:disabled { background:#232730; color:#5e6675; }");
    bottomRow->addWidget(countdownShutdownButton_);
    bottomRow->addWidget(deactivateButton_);
    main->addLayout(bottomRow);

    setCentralWidget(root);

    setStyleSheet(
        "QMainWindow, QWidget { background:#14171d; color:#e8eaf0; font-family:'Segoe UI', sans-serif; font-size:12px; }"
        "QTabWidget::pane { border:1px solid #2e3440; background:#1b1f27; border-radius:4px; }"
        "QTabBar::tab { background:#1b1f27; color:#a3adbf; padding:8px 16px; border:1px solid #2e3440; border-bottom:none; border-top-left-radius:4px; border-top-right-radius:4px; margin-right:2px; }"
        "QTabBar::tab:selected { background:#2b3342; color:#ffffff; font-weight:bold; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QListWidget, QPlainTextEdit, QTableWidget { background:#1e222b; alternate-background-color:#232833; border:1px solid #353c4a; padding:5px; border-radius:4px; color:#e8eaf0; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border:1px solid #589bf5; }"
        "QHeaderView::section { background:#252b37; color:#c8d0e0; padding:6px; border:1px solid #353c4a; font-weight:600; }"
        "QPushButton { background:#2d4a73; border:0; padding:8px 14px; border-radius:4px; color:#ffffff; font-weight:600; }"
        "QPushButton:hover { background:#3b6094; }"
        "QPushButton:disabled { background:#232730; color:#5e6675; }"
        "QSplitter::handle { background:#232834; border-radius:2px; }"
        "QSplitter::handle:hover { background:#3b6094; }"
        "QProgressBar { border:1px solid #353c4a; background:#1e222b; text-align:center; border-radius:4px; color:#ffffff; }"
        "QProgressBar::chunk { background:#3b7dd8; border-radius:3px; }");

    // Wire signals
    connect(createButton_, &QPushButton::clicked, this, &MainWindow::createSession);
    connect(joinButton_, &QPushButton::clicked, this, &MainWindow::joinSelectedSession);
    connect(discoveredList_, &QListWidget::itemClicked, this, &MainWindow::onDiscoveredSessionClicked);
    connect(applySettingsButton_, &QPushButton::clicked, this, &MainWindow::applySettings);

    connect(uploadButton_, &QPushButton::clicked, this, &MainWindow::uploadFile);
    connect(uploadFolderButton_, &QPushButton::clicked, this, &MainWindow::uploadFolder);
    connect(editTextButton_, &QPushButton::clicked, this, &MainWindow::editSelectedTextFile);
    connect(filesTable_, &QTableWidget::cellClicked, this, &MainWindow::onFileTableCellClicked);
    connect(downloadButton_, &QPushButton::clicked, this, &MainWindow::downloadSelected);
    connect(downloadAllButton_, &QPushButton::clicked, this, &MainWindow::downloadAll);
    connect(deleteButton_, &QPushButton::clicked, this, &MainWindow::deleteSelected);

    connect(countdownShutdownButton_, &QPushButton::clicked, this, &MainWindow::initiateShutdownCountdown);
    connect(cancelShutdownButton_, &QPushButton::clicked, this, [this]() {
        QString err;
        if (!controller_.cancelGuardianShutdown(&err)) {
            QMessageBox::warning(this, "Cancel Shutdown", err);
        }
    });
    connect(deactivateButton_, &QPushButton::clicked, this, &MainWindow::deactivateImmediate);

    connect(&controller_, &PandoraController::statusChanged, status_, &QLabel::setText);
    connect(&controller_, &PandoraController::logLine, this, &MainWindow::appendLog);
    connect(&controller_, &PandoraController::discoveredSessionsChanged, this, &MainWindow::refreshDiscoveredSessions);
    connect(&controller_, &PandoraController::transferProgressUpdated, this, &MainWindow::onTransferProgress);
    connect(&controller_, &PandoraController::fileContentReady, this, &MainWindow::onFileContentReady);
    connect(&controller_, &PandoraController::fileReplicaReadyForViewing, this, &MainWindow::onFileReplicaReadyForViewing);
    connect(&controller_, &PandoraController::fileContentFetchFailed, this, &MainWindow::onFileContentFetchFailed);
    connect(&controller_, &PandoraController::shutdownCountdownUpdated, this, &MainWindow::onShutdownCountdownUpdated);

    connect(&controller_, &PandoraController::joinSucceeded, this, [this](const QString& name) {
        Q_UNUSED(name)
        createButton_->setEnabled(false);
        joinButton_->setEnabled(false);
        deactivateButton_->setEnabled(true);
        refreshDashboard();
    });
    connect(&controller_, &PandoraController::joinFailed, this, [this](const QString& reason) {
        QMessageBox::warning(this, "Could not join Pandora", reason);
        createButton_->setEnabled(true);
        joinButton_->setEnabled(true);
        deactivateButton_->setEnabled(false);
        refreshDashboard();
    });

    connect(controller_.metadata(), &MetadataStore::changed, this, &MainWindow::refreshDashboard);
    connect(controller_.network(), &PeerNode::statusChanged, this, &MainWindow::appendLog);

    refreshDashboard();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (controller_.isActive()) {
        const auto res = QMessageBox::question(
            this, "Exit Pandora",
            "Closing Pandora will leave the session and permanently wipe local temporary replicas.\n\nContinue?",
            QMessageBox::Yes | QMessageBox::No);
        if (res != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        controller_.deactivatePandora(true);
    }
    event->accept();
}

void MainWindow::createSession() {
    applySettings();
    QString error;
    if (!controller_.createPandora(pandoraName_->text(), userName_->text(), key_->text(), &error)) {
        QMessageBox::warning(this, "Could not create Pandora", error);
        return;
    }
    createButton_->setEnabled(false);
    joinButton_->setEnabled(false);
    deactivateButton_->setEnabled(true);
    refreshDashboard();
}

void MainWindow::onDiscoveredSessionClicked() {
    auto* item = discoveredList_->currentItem();
    if (!item) return;
    const QString pandoraId = item->data(Qt::UserRole).toString();
    for (const auto& ds : controller_.discoveredSessions()) {
        if (ds.pandoraId == pandoraId) {
            joinHostAddress_->setText(ds.address);
            joinHostPort_->setValue(ds.port);
            joinPandoraId_->setText(ds.pandoraId);
            break;
        }
    }
}

void MainWindow::joinSelectedSession() {
    applySettings();
    QString error;
    if (!controller_.joinPandoraDirect(
            joinUserName_->text(),
            joinKey_->text(),
            joinHostAddress_->text(),
            static_cast<quint16>(joinHostPort_->value()),
            joinPandoraId_->text(),
            &error)) {
        QMessageBox::warning(this, "Could not start Join", error);
        return;
    }
    createButton_->setEnabled(false);
    joinButton_->setEnabled(false);
    deactivateButton_->setEnabled(true);
}

void MainWindow::applySettings() {
    controller_.setReplicationFactor(replicationFactorSpin_->value());
    const qint64 quotaBytes = static_cast<qint64>(quotaGiBSpin_->value() * 1024.0 * 1024.0 * 1024.0);
    controller_.setQuotaBytes(quotaBytes);
    if (!controller_.isActive()) {
        controller_.setPreferredPorts(static_cast<quint16>(tcpPortSpin_->value()), 43171);
    }
    appendLog(QString("Settings applied: Replication Factor = %1, Local Quota = %2, Preferred TCP Port = %3")
              .arg(replicationFactorSpin_->value())
              .arg(formatBytes(quotaBytes))
              .arg(tcpPortSpin_->value()));
}

QString MainWindow::selectedFileId() const {
    const int row = filesTable_->currentRow();
    if (row < 0) return {};
    auto* item = filesTable_->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString MainWindow::selectedFileName() const {
    const int row = filesTable_->currentRow();
    if (row < 0) return {};
    auto* item = filesTable_->item(row, 0);
    return item ? item->text() : QString();
}

void MainWindow::uploadFile() {
    const auto path = QFileDialog::getOpenFileName(this, "Upload file to Pandora");
    if (path.isEmpty()) return;
    QString error;
    if (!controller_.uploadFile(path, &error)) {
        QMessageBox::warning(this, "Upload failed", error);
    }
}

void MainWindow::uploadFolder() {
    const auto folderPath = QFileDialog::getExistingDirectory(this, "Select Folder to Upload to Pandora");
    if (folderPath.isEmpty()) return;
    int count = 0;
    QString error;
    if (!controller_.uploadFolder(folderPath, &count, &error)) {
        QMessageBox::warning(this, "Folder upload error", error);
    }
}

void MainWindow::onFileTableCellClicked(int row, int /*column*/) {
    if (row < 0) return;
    auto* item = filesTable_->item(row, 0);
    if (!item) return;
    const QString fileId = item->data(Qt::UserRole).toString();
    const QString fileName = item->text();
    if (fileId.isEmpty()) return;

    if (PandoraController::isEditableTextFileName(fileName)) {
        openTextEditorForFile(fileId);
    } else if (PandoraController::isMediaFileName(fileName)) {
        openMediaViewerForFile(fileId);
    }
}

void MainWindow::editSelectedTextFile() {
    const QString id = selectedFileId();
    const QString name = selectedFileName();
    if (id.isEmpty()) {
        QMessageBox::information(this, "Open / Edit File", "Select a text, code, image, or video file in the table first.");
        return;
    }
    if (PandoraController::isEditableTextFileName(name)) {
        openTextEditorForFile(id);
        return;
    }
    if (PandoraController::isMediaFileName(name)) {
        openMediaViewerForFile(id);
        return;
    }
    QMessageBox::information(
        this, "Unsupported File Type",
        QString("'%1' is not recognized as an in-app editable text file or playable image/video.\n\n"
                "• Editable text/code: .txt, .c, .cpp, .h, .py, .js, .ts, .java, .json, .md, .html, .css, .sql, .sh, .ps1\n"
                "• Viewable images: .png, .jpg, .jpeg, .gif, .bmp, .webp, .ico, .tiff\n"
                "• Playable videos: .mp4, .mkv, .avi, .mov, .webm, .wmv, .m4v, .mpg")
            .arg(name));
}

void MainWindow::openMediaViewerForFile(const QString& fileId) {
    bool found = false;
    const FileInfo f = controller_.metadata()->file(fileId, &found);
    if (!found || f.tombstone) return;

    if (openMediaViewers_.contains(fileId) && !openMediaViewers_[fileId].isNull()) {
        auto* existing = openMediaViewers_[fileId].data();
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    const auto kind = PandoraController::isVideoFileName(f.name)
        ? MediaViewerDialog::MediaKind::Video
        : MediaViewerDialog::MediaKind::Image;

    auto* dlg = new MediaViewerDialog(fileId, f.name, kind, f.size, this);
    openMediaViewers_.insert(fileId, dlg);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();

    QString immediateBlobPath;
    bool waitingRemote = false;
    QString err;
    if (!controller_.fetchFileReplicaForMediaViewing(fileId, &immediateBlobPath, &waitingRemote, &err)) {
        dlg->notifyRemoteFetchFailed(err);
        return;
    }
    if (!waitingRemote && !immediateBlobPath.isEmpty()) {
        dlg->loadFromReplicaBlob(immediateBlobPath);
    }
}

void MainWindow::openTextEditorForFile(const QString& fileId) {
    bool found = false;
    const FileInfo f = controller_.metadata()->file(fileId, &found);
    if (!found || f.tombstone) return;

    // If an editor window is already open for this file, bring it to front
    if (openEditors_.contains(fileId) && !openEditors_[fileId].isNull()) {
        auto* existing = openEditors_[fileId].data();
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto* dlg = new TextEditorDialog(
        fileId,
        f.name,
        f.version,
        [this, fileId](const QByteArray& newUtf8Content, QString* errorOut) -> bool {
            return controller_.updateTextFileContent(fileId, newUtf8Content, errorOut);
        },
        this);
    openEditors_.insert(fileId, dlg);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();

    QByteArray immediateBytes;
    bool waitingRemote = false;
    QString err;
    if (!controller_.fetchFileContentForViewing(fileId, &immediateBytes, &waitingRemote, &err)) {
        dlg->notifyRemoteFetchFailed(err);
        return;
    }
    if (!waitingRemote) {
        dlg->setInitialContent(immediateBytes, f.version);
    }
}

void MainWindow::onFileContentReady(const QString& fileId, const QByteArray& content) {
    if (!openEditors_.contains(fileId) || openEditors_[fileId].isNull()) return;
    const FileInfo f = controller_.metadata()->file(fileId);
    openEditors_[fileId]->setInitialContent(content, qMax<quint64>(1, f.version));
}

void MainWindow::onFileReplicaReadyForViewing(const QString& fileId, const QString& replicaBlobPath) {
    if (!openMediaViewers_.contains(fileId) || openMediaViewers_[fileId].isNull()) return;
    openMediaViewers_[fileId]->loadFromReplicaBlob(replicaBlobPath);
}

void MainWindow::onFileContentFetchFailed(const QString& fileId, const QString& reason) {
    if (openEditors_.contains(fileId) && !openEditors_[fileId].isNull()) {
        openEditors_[fileId]->notifyRemoteFetchFailed(reason);
    }
    if (openMediaViewers_.contains(fileId) && !openMediaViewers_[fileId].isNull()) {
        openMediaViewers_[fileId]->notifyRemoteFetchFailed(reason);
    }
}

void MainWindow::downloadSelected() {
    const QString id = selectedFileId();
    const QString name = selectedFileName();
    if (id.isEmpty()) {
        QMessageBox::information(this, "Download File", "Select a file in the table first.");
        return;
    }
    const QString baseName = QFileInfo(name).fileName();
    const auto dest = QFileDialog::getSaveFileName(this, "Download file", baseName);
    if (dest.isEmpty()) return;
    QString error;
    if (!controller_.downloadFile(id, dest, &error)) {
        QMessageBox::warning(this, "Download failed", error);
    }
}

void MainWindow::downloadAll() {
    const auto dir = QFileDialog::getExistingDirectory(this, "Choose Destination Folder for Download All");
    if (dir.isEmpty()) return;
    int count = 0;
    QString error;
    if (!controller_.downloadAllFiles(dir, &count, &error)) {
        QMessageBox::warning(this, "Download All failed", error);
    } else {
        QMessageBox::information(this, "Download All",
                                 QString("Exported or queued %1 file(s) into:\n%2").arg(count).arg(dir));
    }
}

void MainWindow::deleteSelected() {
    const QString id = selectedFileId();
    const QString name = selectedFileName();
    if (id.isEmpty()) return;
    if (QMessageBox::question(this, "Delete File",
                              QString("Delete '%1' across all Pandora replicas?").arg(name)) != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!controller_.deleteFile(id, &error)) {
        QMessageBox::warning(this, "Delete failed", error);
    }
}

void MainWindow::initiateShutdownCountdown() {
    if (QMessageBox::question(
            this, "Initiate 2-Minute Shutdown",
            "This will warn all connected peers and start a 120-second countdown before deactivating Pandora.\n\nProceed?")
        != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!controller_.initiateGuardianShutdown(120, &error)) {
        QMessageBox::warning(this, "Shutdown Countdown", error);
    }
}

void MainWindow::deactivateImmediate() {
    const QString prompt = controller_.isGuardian()
        ? "As Guardian, deactivating immediately will terminate the Pandora session for all peers and securely wipe local replicas. Continue?"
        : "Leaving Pandora will disconnect this node and securely wipe local temporary replicas. Continue?";
    if (QMessageBox::question(this, "Deactivate Pandora", prompt) != QMessageBox::Yes) return;

    controller_.deactivatePandora(true);
    createButton_->setEnabled(true);
    joinButton_->setEnabled(true);
    deactivateButton_->setEnabled(false);
    countdownShutdownButton_->setEnabled(false);
    shutdownBanner_->setVisible(false);
    refreshDashboard();
}

void MainWindow::refreshDiscoveredSessions() {
    discoveredList_->clear();
    for (const auto& ds : controller_.discoveredSessions()) {
        auto* item = new QListWidgetItem(
            QString("%1  —  Host: %2 (%3:%4)  [%5 Peers]")
            .arg(ds.name, ds.hostName.isEmpty() ? "Guardian" : ds.hostName, ds.address)
            .arg(ds.port)
            .arg(ds.nodeCount));
        item->setData(Qt::UserRole, ds.pandoraId);
        discoveredList_->addItem(item);
    }
}

void MainWindow::refreshDashboard() {
    const bool active = controller_.isActive();
    const bool guardian = controller_.isGuardian();
    const QString myId = controller_.localNodeIdString();
    const QString guardianId = controller_.metadata()->guardian();

    createButton_->setEnabled(!active);
    joinButton_->setEnabled(!active);
    deactivateButton_->setEnabled(active);
    countdownShutdownButton_->setEnabled(active && guardian);
    cancelShutdownButton_->setVisible(active && guardian && controller_.isShuttingDown());

    uploadButton_->setEnabled(active);
    uploadFolderButton_->setEnabled(active);
    editTextButton_->setEnabled(active);
    downloadButton_->setEnabled(active);
    downloadAllButton_->setEnabled(active);
    deleteButton_->setEnabled(active);

    if (!active) {
        roleBadge_->setText("Offline");
        roleBadge_->setStyleSheet("background:#2b303c;color:#aab2c0;padding:6px 14px;border-radius:4px;font-weight:bold;");
    } else if (guardian) {
        roleBadge_->setText(QString("Online — Guardian (Port %1)").arg(controller_.tcpPort()));
        roleBadge_->setStyleSheet("background:#1e5631;color:#a4fba6;padding:6px 14px;border-radius:4px;font-weight:bold;");
    } else {
        roleBadge_->setText(QString("Online — Connected Peer (Port %1)").arg(controller_.tcpPort()));
        roleBadge_->setStyleSheet("background:#1e3f66;color:#99ccff;padding:6px 14px;border-radius:4px;font-weight:bold;");
    }

    // Populate Files Table
    const auto files = controller_.metadata()->files(false);
    filesTable_->setRowCount(files.size());
    const int targetRep = qMax(1, controller_.metadata()->session().replicationFactor);

    for (int i = 0; i < files.size(); ++i) {
        const FileInfo& f = files[i];
        const QString fid = f.id.toString(QUuid::WithoutBraces);
        const bool hasLocal = controller_.storage()->hasReplica(fid);

        auto* nameItem = new QTableWidgetItem(f.name);
        nameItem->setData(Qt::UserRole, fid);
        if (PandoraController::isEditableTextFileName(f.name)) {
            nameItem->setToolTip("Click to open in Pandora In-App Text Editor (no local download required)");
            nameItem->setForeground(QColor("#8ce99a"));
        } else if (PandoraController::isImageFileName(f.name)) {
            nameItem->setToolTip("Click to view in Pandora In-App Image Viewer (view-only, no local download)");
            nameItem->setForeground(QColor("#74c0fc"));
        } else if (PandoraController::isVideoFileName(f.name)) {
            nameItem->setToolTip("Click to play in Pandora In-App Video Player (view-only, no local download)");
            nameItem->setForeground(QColor("#e599f7"));
        }
        filesTable_->setItem(i, 0, nameItem);

        if (openEditors_.contains(fid) && !openEditors_[fid].isNull()) {
            openEditors_[fid]->notifyExternalUpdate(f.version);
        }

        filesTable_->setItem(i, 1, new QTableWidgetItem(formatBytes(f.size)));

        const QString repText = QString("%1 / %2%3")
            .arg(f.replicas.size())
            .arg(targetRep)
            .arg(hasLocal ? " (Local)" : " (Remote)");
        filesTable_->setItem(i, 2, new QTableWidgetItem(repText));
        filesTable_->setItem(i, 3, new QTableWidgetItem(QString("v%1").arg(f.version)));
        filesTable_->setItem(i, 4, new QTableWidgetItem(f.sha256.left(12) + "..."));
    }

    // Populate Peers Table
    const auto nodes = controller_.metadata()->nodes(false);
    peersTable_->setRowCount(nodes.size());
    int onlineCount = 0;
    qint64 totalUsed = 0;
    qint64 totalQuota = 0;

    for (int i = 0; i < nodes.size(); ++i) {
        const NodeInfo& n = nodes[i];
        const QString nid = n.id.toString(QUuid::WithoutBraces);
        if (n.connected) {
            ++onlineCount;
            totalUsed += n.managedBytes;
            totalQuota += n.quotaBytes;
        }

        const QString displayName = (nid == myId) ? (n.name + " (You)") : n.name;
        const QString roleStr = (nid == guardianId) ? "Guardian" : "Peer";
        const QString statusStr = QString("%1 (%2:%3)")
            .arg(n.connected ? "Online" : "Offline", n.address.isEmpty() ? "LAN" : n.address)
            .arg(n.port);
        const QString quotaStr = QString("%1 / %2").arg(formatBytes(n.managedBytes), formatBytes(n.quotaBytes));
        const QString diskStr = n.availableDiskBytes > 0 ? formatBytes(n.availableDiskBytes) : QString("N/A");

        peersTable_->setItem(i, 0, new QTableWidgetItem(displayName));
        peersTable_->setItem(i, 1, new QTableWidgetItem(roleStr));
        peersTable_->setItem(i, 2, new QTableWidgetItem(statusStr));
        peersTable_->setItem(i, 3, new QTableWidgetItem(quotaStr));
        peersTable_->setItem(i, 4, new QTableWidgetItem(diskStr));
    }

    nodeCount_->setText(QString("Peers Online: %1 / %2").arg(onlineCount).arg(nodes.size()));
    cloudStorageSummary_->setText(QString("Cloud Used: %1 / Allocated Capacity: %2")
                                  .arg(formatBytes(totalUsed), formatBytes(totalQuota)));
}

void MainWindow::onTransferProgress(const TransferProgress& progress) {
    const int pct = progress.totalBytes > 0
        ? static_cast<int>((progress.bytesTransferred * 100) / progress.totalBytes)
        : 100;
    transferProgressBar_->setValue(qBound(0, pct, 100));
    transferStatusLabel_->setText(QString("%1: %2 (%3%)")
                                  .arg(progress.direction, progress.fileName)
                                  .arg(pct));
}

void MainWindow::onShutdownCountdownUpdated(int secondsRemaining) {
    if (secondsRemaining > 0) {
        shutdownBanner_->setVisible(true);
        shutdownBannerLabel_->setText(
            QString("CRITICAL: Pandora will deactivate in %1 seconds! Click 'Download All' now to save files.")
            .arg(secondsRemaining));
        cancelShutdownButton_->setVisible(controller_.isGuardian());
    } else {
        shutdownBanner_->setVisible(false);
    }
}

void MainWindow::appendLog(const QString& line) {
    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    logs_->appendPlainText(QString("[%1] %2").arg(ts, line));
}

} // namespace pandora
