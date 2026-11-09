#pragma once
#include "core/pandora_controller.h"
#include <QMainWindow>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>

namespace pandora {
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
private slots:
    void createSession();
    void uploadFile();
    void downloadSelected();
    void deleteSelected();
    void deactivate();
    void refreshFiles();
    void appendLog(const QString& line);
private:
    PandoraController controller_;
    QLineEdit *pandoraName_, *userName_, *key_;
    QListWidget* files_;
    QLabel *status_, *nodeCount_;
    QPlainTextEdit* logs_;
    QPushButton *createButton_, *uploadButton_, *downloadButton_, *deleteButton_, *deactivateButton_;
};
}
