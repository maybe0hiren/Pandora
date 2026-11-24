#pragma once

#include <QAudioOutput>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QMediaPlayer>
#include <QMovie>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVideoWidget>
#include <QWheelEvent>

namespace pandora {

class MediaViewerDialog final : public QDialog {
    Q_OBJECT
public:
    enum class MediaKind {
        Image,
        Video
    };

    explicit MediaViewerDialog(const QString& fileId,
                               const QString& fileName,
                               MediaKind kind,
                               qint64 fileSize,
                               QWidget* parent = nullptr);
    ~MediaViewerDialog() override;

    QString fileId() const { return fileId_; }
    void loadFromReplicaBlob(const QString& replicaBlobPath);
    void notifyRemoteFetchFailed(const QString& reason);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    // Image actions
    void zoomIn();
    void zoomOut();
    void zoomOriginal();
    void toggleFitToWindow(bool fit);
    void rotateLeft();
    void rotateRight();

    // Video actions
    void togglePlayPause();
    void stopVideo();
    void onVideoPositionChanged(qint64 positionMs);
    void onVideoDurationChanged(qint64 durationMs);
    void onSeekSliderMoved(int value);
    void onVolumeChanged(int value);
    void toggleMute();
    void onSpeedChanged(int index);

private:
    static QString formatDuration(qint64 ms);
    static QString formatBytes(qint64 bytes);
    void updateRenderedImage();

    QString fileId_;
    QString fileName_;
    MediaKind kind_;
    qint64 fileSize_{0};
    QString loadedBlobPath_;
    QFile* videoStreamFile_{nullptr};

    // Top bar
    QLabel* titleLabel_{nullptr};
    QLabel* badgeLabel_{nullptr};
    QLabel* infoLabel_{nullptr};

    // Image viewer widgets
    QScrollArea* imageScroll_{nullptr};
    QLabel* imageLabel_{nullptr};
    QPixmap originalPixmap_;
    QMovie* gifMovie_{nullptr};
    double zoomFactor_{1.0};
    int rotationDegrees_{0};
    bool fitToWindow_{true};
    QPushButton* zoomInBtn_{nullptr};
    QPushButton* zoomOutBtn_{nullptr};
    QPushButton* zoom100Btn_{nullptr};
    QCheckBox* fitWindowCheck_{nullptr};
    QPushButton* rotateLeftBtn_{nullptr};
    QPushButton* rotateRightBtn_{nullptr};

    // Video player widgets
    QVideoWidget* videoWidget_{nullptr};
    QMediaPlayer* mediaPlayer_{nullptr};
    QAudioOutput* audioOutput_{nullptr};
    QPushButton* playPauseBtn_{nullptr};
    QPushButton* stopBtn_{nullptr};
    QSlider* seekSlider_{nullptr};
    QLabel* timeLabel_{nullptr};
    QPushButton* muteBtn_{nullptr};
    QSlider* volumeSlider_{nullptr};
    QComboBox* speedCombo_{nullptr};
    QCheckBox* loopCheck_{nullptr};
    bool userSeeking_{false};

    // Bottom status bar
    QLabel* statusLabel_{nullptr};
};

} // namespace pandora
