#include "ui/main_window.h"
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

namespace pandora {
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Pandora — Temporary Peer-to-Peer Cloud");
    resize(1000, 680);
    auto* root = new QWidget(this); auto* main = new QVBoxLayout(root);
    auto* title = new QLabel("<h1>Pandora</h1><span>Temporary peer-to-peer cloud storage</span>");
    main->addWidget(title);
    auto* formBox = new QGroupBox("Create Pandora"); auto* form = new QFormLayout(formBox);
    pandoraName_ = new QLineEdit("My Pandora"); userName_ = new QLineEdit; key_ = new QLineEdit;
    key_->setEchoMode(QLineEdit::Password);
    form->addRow("Pandora name", pandoraName_); form->addRow("Your Na'vi name", userName_); form->addRow("Pandora Key (6+ characters)", key_);
    createButton_ = new QPushButton("Create Pandora"); form->addRow(createButton_);
    main->addWidget(formBox);
    auto* statusRow = new QHBoxLayout; status_ = new QLabel("Pandora Inactive"); nodeCount_ = new QLabel("Na'vi Online: 0");
    statusRow->addWidget(status_); statusRow->addStretch(); statusRow->addWidget(nodeCount_); main->addLayout(statusRow);
    auto* split = new QSplitter;
    auto* fileBox = new QWidget; auto* fileLayout = new QVBoxLayout(fileBox); fileLayout->addWidget(new QLabel("Pandora Files"));
    files_ = new QListWidget; fileLayout->addWidget(files_);
    auto* buttons = new QHBoxLayout; uploadButton_ = new QPushButton("Upload"); downloadButton_ = new QPushButton("Download"); deleteButton_ = new QPushButton("Delete");
    buttons->addWidget(uploadButton_); buttons->addWidget(downloadButton_); buttons->addWidget(deleteButton_); fileLayout->addLayout(buttons);
    split->addWidget(fileBox);
    auto* logBox = new QWidget; auto* logLayout = new QVBoxLayout(logBox); logLayout->addWidget(new QLabel("Session Logs"));
    logs_ = new QPlainTextEdit; logs_->setReadOnly(true); logLayout->addWidget(logs_); split->addWidget(logBox);
    main->addWidget(split, 1);
    deactivateButton_ = new QPushButton("Deactivate Pandora"); deactivateButton_->setEnabled(false); main->addWidget(deactivateButton_);
    setCentralWidget(root);
    setStyleSheet("QMainWindow,QWidget{background:#17191f;color:#e8eaf0} QLineEdit,QListWidget,QPlainTextEdit{background:#22252d;border:1px solid #3b404b;padding:6px} QPushButton{background:#35445d;border:0;padding:8px 14px;border-radius:4px} QPushButton:hover{background:#465b7b} QGroupBox{border:1px solid #3b404b;margin-top:8px;padding:10px} QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 4px}");
    connect(createButton_, &QPushButton::clicked, this, &MainWindow::createSession);
    connect(uploadButton_, &QPushButton::clicked, this, &MainWindow::uploadFile);
    connect(downloadButton_, &QPushButton::clicked, this, &MainWindow::downloadSelected);
    connect(deleteButton_, &QPushButton::clicked, this, &MainWindow::deleteSelected);
    connect(deactivateButton_, &QPushButton::clicked, this, &MainWindow::deactivate);
    connect(&controller_, &PandoraController::statusChanged, status_, &QLabel::setText);
    connect(&controller_, &PandoraController::logLine, this, &MainWindow::appendLog);
    connect(controller_.metadata(), &MetadataStore::changed, this, &MainWindow::refreshFiles);
    connect(controller_.network(), &PeerNode::statusChanged, this, &MainWindow::appendLog);
}
void MainWindow::createSession() {
    QString error;
    if (!controller_.createPandora(pandoraName_->text(), userName_->text(), key_->text(), &error)) { QMessageBox::warning(this, "Could not create Pandora", error); return; }
    createButton_->setEnabled(false); deactivateButton_->setEnabled(true); refreshFiles();
}
void MainWindow::uploadFile() {
    const auto path = QFileDialog::getOpenFileName(this, "Upload file"); if (path.isEmpty()) return;
    QString error; if (!controller_.uploadFile(path, &error)) QMessageBox::warning(this, "Upload failed", error);
}
void MainWindow::downloadSelected() {
    auto* item = files_->currentItem(); if (!item) return;
    const auto id = item->data(Qt::UserRole).toString();
    const auto dest = QFileDialog::getSaveFileName(this, "Download file", item->text()); if (dest.isEmpty()) return;
    QString error; if (!controller_.downloadFile(id, dest, &error)) QMessageBox::warning(this, "Download failed", error);
}
void MainWindow::deleteSelected() {
    auto* item = files_->currentItem(); if (!item) return;
    if (QMessageBox::question(this, "Delete file", "Delete this file from Pandora?") != QMessageBox::Yes) return;
    QString error; if (!controller_.deleteFile(item->data(Qt::UserRole).toString(), &error)) QMessageBox::warning(this, "Delete failed", error);
}
void MainWindow::deactivate() {
    if (QMessageBox::question(this, "Deactivate Pandora", "This immediately deletes local session replicas. Continue?") != QMessageBox::Yes) return;
    controller_.deactivatePandora(); createButton_->setEnabled(true); deactivateButton_->setEnabled(false);
    files_->clear(); pandoraName_->setEnabled(true);
}
void MainWindow::refreshFiles() {
    files_->clear();
    for (const auto& f : controller_.metadata()->files()) {
        auto* item = new QListWidgetItem(QString("%1  (%2 bytes)").arg(f.name).arg(f.size));
        item->setData(Qt::UserRole, f.id.toString(QUuid::WithoutBraces)); files_->addItem(item);
    }
    nodeCount_->setText(QString("Na'vi Online: %1").arg(controller_.metadata()->nodes().size()));
}
void MainWindow::appendLog(const QString& line) { logs_->appendPlainText(line); }
}
