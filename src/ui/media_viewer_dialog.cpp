#include "ui/media_viewer_dialog.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>

namespace pandora {

QString MediaViewerDialog::formatDuration(qint64 ms) {
    if (ms < 0) ms = 0;
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QString("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString MediaViewerDialog::formatBytes(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024) return QString("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    return QString("%1 GiB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

MediaViewerDialog::MediaViewerDialog(const QString& fileId,
                                     const QString& fileName,
                                     MediaKind kind,
                                     qint64 fileSize,
                                     QWidget* parent)
    : QDialog(parent),
      fileId_(fileId),
      fileName_(fileName),
      kind_(kind),
      fileSize_(fileSize) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint);
    setWindowTitle(QString("Pandora %1 Viewer — %2")
                   .arg(kind_ == MediaKind::Image ? "Image" : "Video", fileName_));
    resize(980, 680);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 10);
    rootLayout->setSpacing(8);

    // 1. Top Header & Controls Bar
    auto* topFrame = new QWidget;
    topFrame->setStyleSheet("background:#1b1f28;border:1px solid #2d3442;border-radius:5px;");
    auto* topBar = new QHBoxLayout(topFrame);
    topBar->setContentsMargins(10, 6, 10, 6);
    topBar->setSpacing(8);

    titleLabel_ = new QLabel(fileName_);
    titleLabel_->setStyleSheet("font-weight:bold;font-size:13px;color:#7ec8ff;border:none;");
    topBar->addWidget(titleLabel_);

    const QString ext = QFileInfo(fileName_).suffix().toUpper();
    badgeLabel_ = new QLabel(QString("%1 (%2)").arg(kind_ == MediaKind::Image ? "IMAGE" : "VIDEO", ext));
    badgeLabel_->setStyleSheet("background:#263042;color:#b8c7e0;padding:3px 9px;border-radius:3px;font-size:11px;font-weight:600;border:none;");
    topBar->addWidget(badgeLabel_);

    infoLabel_ = new QLabel(formatBytes(fileSize_));
    infoLabel_->setStyleSheet("color:#9aa4b8;font-size:12px;border:none;");
    topBar->addWidget(infoLabel_);

    topBar->addStretch();

    if (kind_ == MediaKind::Image) {
        zoomOutBtn_ = new QPushButton("Zoom -");
        zoom100Btn_ = new QPushButton("100%");
        zoomInBtn_ = new QPushButton("Zoom +");
        fitWindowCheck_ = new QCheckBox("Fit to Window");
        fitWindowCheck_->setChecked(true);
        fitWindowCheck_->setStyleSheet("color:#c8d0e0;border:none;padding:0 4px;");
        rotateLeftBtn_ = new QPushButton("Rotate ⟲");
        rotateRightBtn_ = new QPushButton("Rotate ⟳");

        zoomOutBtn_->setEnabled(false);
        zoom100Btn_->setEnabled(false);
        zoomInBtn_->setEnabled(false);
        rotateLeftBtn_->setEnabled(false);
        rotateRightBtn_->setEnabled(false);

        topBar->addWidget(zoomOutBtn_);
        topBar->addWidget(zoom100Btn_);
        topBar->addWidget(zoomInBtn_);
        topBar->addWidget(fitWindowCheck_);
        topBar->addWidget(rotateLeftBtn_);
        topBar->addWidget(rotateRightBtn_);

        connect(zoomInBtn_, &QPushButton::clicked, this, &MediaViewerDialog::zoomIn);
        connect(zoomOutBtn_, &QPushButton::clicked, this, &MediaViewerDialog::zoomOut);
        connect(zoom100Btn_, &QPushButton::clicked, this, &MediaViewerDialog::zoomOriginal);
        connect(fitWindowCheck_, &QCheckBox::toggled, this, &MediaViewerDialog::toggleFitToWindow);
        connect(rotateLeftBtn_, &QPushButton::clicked, this, &MediaViewerDialog::rotateLeft);
        connect(rotateRightBtn_, &QPushButton::clicked, this, &MediaViewerDialog::rotateRight);
    }

    rootLayout->addWidget(topFrame);

    // 2. Center Viewport (Image ScrollArea OR Video Player)
    if (kind_ == MediaKind::Image) {
        imageScroll_ = new QScrollArea(this);
        imageScroll_->setWidgetResizable(true);
        imageScroll_->setAlignment(Qt::AlignCenter);
        imageScroll_->setStyleSheet("QScrollArea { background:#101319; border:1px solid #2b313d; border-radius:5px; }");

        imageLabel_ = new QLabel("Streaming image replica from Pandora session storage...");
        imageLabel_->setAlignment(Qt::AlignCenter);
        imageLabel_->setStyleSheet("color:#9aa4b8;font-size:13px;background:transparent;border:none;");
        imageScroll_->setWidget(imageLabel_);
        rootLayout->addWidget(imageScroll_, 1);
    } else {
        videoWidget_ = new QVideoWidget(this);
        videoWidget_->setStyleSheet("background:#0b0d12;border:1px solid #2b313d;border-radius:5px;");
        rootLayout->addWidget(videoWidget_, 1);

        mediaPlayer_ = new QMediaPlayer(this);
        audioOutput_ = new QAudioOutput(this);
        audioOutput_->setVolume(0.8f);
        mediaPlayer_->setAudioOutput(audioOutput_);
        mediaPlayer_->setVideoOutput(videoWidget_);

        // Video Transport Control Bar
        auto* controlsFrame = new QWidget;
        controlsFrame->setStyleSheet("background:#1b1f28;border:1px solid #2d3442;border-radius:5px;");
        auto* controlsBar = new QHBoxLayout(controlsFrame);
        controlsBar->setContentsMargins(10, 6, 10, 6);
        controlsBar->setSpacing(8);

        playPauseBtn_ = new QPushButton("Play");
        playPauseBtn_->setFixedWidth(74);
        playPauseBtn_->setEnabled(false);
        stopBtn_ = new QPushButton("Stop");
        stopBtn_->setFixedWidth(60);
        stopBtn_->setEnabled(false);

        seekSlider_ = new QSlider(Qt::Horizontal);
        seekSlider_->setRange(0, 0);
        seekSlider_->setEnabled(false);

        timeLabel_ = new QLabel("00:00 / 00:00");
        timeLabel_->setStyleSheet("color:#c8d0e0;font-family:'Consolas',monospace;font-size:12px;border:none;");

        muteBtn_ = new QPushButton("Vol");
        muteBtn_->setFixedWidth(48);
        volumeSlider_ = new QSlider(Qt::Horizontal);
        volumeSlider_->setRange(0, 100);
        volumeSlider_->setValue(80);
        volumeSlider_->setFixedWidth(90);

        speedCombo_ = new QComboBox;
        speedCombo_->addItem("0.5x", 0.5);
        speedCombo_->addItem("0.75x", 0.75);
        speedCombo_->addItem("1.0x", 1.0);
        speedCombo_->addItem("1.25x", 1.25);
        speedCombo_->addItem("1.5x", 1.5);
        speedCombo_->addItem("2.0x", 2.0);
        speedCombo_->setCurrentIndex(2); // 1.0x

        loopCheck_ = new QCheckBox("Loop");
        loopCheck_->setStyleSheet("color:#c8d0e0;border:none;");

        controlsBar->addWidget(playPauseBtn_);
        controlsBar->addWidget(stopBtn_);
        controlsBar->addWidget(seekSlider_, 1);
        controlsBar->addWidget(timeLabel_);
        controlsBar->addSpacing(8);
        controlsBar->addWidget(muteBtn_);
        controlsBar->addWidget(volumeSlider_);
        controlsBar->addSpacing(8);
        controlsBar->addWidget(new QLabel("Speed:"));
        controlsBar->addWidget(speedCombo_);
        controlsBar->addWidget(loopCheck_);

        rootLayout->addWidget(controlsFrame);

        connect(playPauseBtn_, &QPushButton::clicked, this, &MediaViewerDialog::togglePlayPause);
        connect(stopBtn_, &QPushButton::clicked, this, &MediaViewerDialog::stopVideo);
        connect(mediaPlayer_, &QMediaPlayer::positionChanged, this, &MediaViewerDialog::onVideoPositionChanged);
        connect(mediaPlayer_, &QMediaPlayer::durationChanged, this, &MediaViewerDialog::onVideoDurationChanged);
        connect(mediaPlayer_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState st) {
            playPauseBtn_->setText(st == QMediaPlayer::PlayingState ? "Pause" : "Play");
        });
        connect(mediaPlayer_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
            if (st == QMediaPlayer::EndOfMedia && loopCheck_->isChecked()) {
                mediaPlayer_->setPosition(0);
                mediaPlayer_->play();
            }
        });
        connect(mediaPlayer_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error /*err*/, const QString& errStr) {
            if (!errStr.isEmpty()) {
                statusLabel_->setText("Video playback notice: " + errStr);
                statusLabel_->setStyleSheet("color:#ffa94d;font-weight:600;border:none;");
            }
        });

        connect(seekSlider_, &QSlider::sliderPressed, this, [this]() { userSeeking_ = true; });
        connect(seekSlider_, &QSlider::sliderReleased, this, [this]() {
            userSeeking_ = false;
            mediaPlayer_->setPosition(seekSlider_->value());
        });
        connect(seekSlider_, &QSlider::sliderMoved, this, &MediaViewerDialog::onSeekSliderMoved);

        connect(volumeSlider_, &QSlider::valueChanged, this, &MediaViewerDialog::onVolumeChanged);
        connect(muteBtn_, &QPushButton::clicked, this, &MediaViewerDialog::toggleMute);
        connect(speedCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MediaViewerDialog::onSpeedChanged);
    }

    // 3. Bottom Status Bar
    auto* statusFrame = new QWidget;
    statusFrame->setStyleSheet("background:#191d25;border:1px solid #2b313d;border-radius:4px;");
    auto* statusLayout = new QHBoxLayout(statusFrame);
    statusLayout->setContentsMargins(10, 4, 10, 4);

    statusLabel_ = new QLabel("Streaming replica into temporary session viewer (no file downloaded to user disk)...");
    statusLabel_->setStyleSheet("color:#7ec8ff;font-weight:600;border:none;");
    auto* modeNote = new QLabel("View-Only Mode  |  Ephemeral Session Storage");
    modeNote->setStyleSheet("color:#8b95a7;border:none;");

    statusLayout->addWidget(statusLabel_);
    statusLayout->addStretch();
    statusLayout->addWidget(modeNote);
    rootLayout->addWidget(statusFrame);

    setStyleSheet(
        "QDialog { background:#13161c; color:#e8eaf0; font-family:'Segoe UI', sans-serif; font-size:12px; }"
        "QPushButton { background:#283447; color:#e8eaf0; border:1px solid #38465e; padding:5px 10px; border-radius:4px; font-weight:600; }"
        "QPushButton:hover { background:#354660; }"
        "QPushButton:disabled { background:#1f232c; color:#586070; border-color:#2a2f3a; }"
        "QComboBox { background:#1e222b; color:#e8eaf0; border:1px solid #38465e; padding:4px 8px; border-radius:4px; }"
        "QSlider::groove:horizontal { border:1px solid #2e3543; height:6px; background:#141820; border-radius:3px; }"
        "QSlider::sub-page:horizontal { background:#3b7dd8; border-radius:3px; }"
        "QSlider::handle:horizontal { background:#7ec8ff; border:1px solid #3b7dd8; width:14px; margin:-5px 0; border-radius:7px; }");
}

MediaViewerDialog::~MediaViewerDialog() {
    if (mediaPlayer_) {
        mediaPlayer_->stop();
        mediaPlayer_->setSourceDevice(nullptr);
        mediaPlayer_->setSource(QUrl());
    }
    if (videoStreamFile_) {
        if (videoStreamFile_->isOpen()) videoStreamFile_->close();
        delete videoStreamFile_;
        videoStreamFile_ = nullptr;
    }
}

void MediaViewerDialog::loadFromReplicaBlob(const QString& replicaBlobPath) {
    loadedBlobPath_ = replicaBlobPath;
    if (!QFileInfo::exists(replicaBlobPath)) {
        notifyRemoteFetchFailed("Local replica file is missing.");
        return;
    }

    if (kind_ == MediaKind::Image) {
        const QString ext = QFileInfo(fileName_).suffix().toLower();
        if (ext == "gif") {
            if (gifMovie_) {
                gifMovie_->stop();
                delete gifMovie_;
            }
            gifMovie_ = new QMovie(replicaBlobPath, QByteArray(), this);
            if (gifMovie_->isValid()) {
                imageLabel_->setMovie(gifMovie_);
                gifMovie_->start();
                const QSize sz = gifMovie_->frameRect().size();
                infoLabel_->setText(QString("%1 × %2 px  |  %3  |  Animated GIF")
                                    .arg(sz.width()).arg(sz.height()).arg(formatBytes(fileSize_)));
                statusLabel_->setText("Playing animated GIF in-app (no file downloaded to user disk)");
                statusLabel_->setStyleSheet("color:#8ce99a;font-weight:600;border:none;");
                return;
            }
        }

        QImageReader reader(replicaBlobPath);
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        if (img.isNull()) {
            notifyRemoteFetchFailed("Unable to decode image format: " + reader.errorString());
            return;
        }
        originalPixmap_ = QPixmap::fromImage(img);
        zoomFactor_ = 1.0;
        rotationDegrees_ = 0;

        zoomOutBtn_->setEnabled(true);
        zoom100Btn_->setEnabled(true);
        zoomInBtn_->setEnabled(true);
        rotateLeftBtn_->setEnabled(true);
        rotateRightBtn_->setEnabled(true);

        infoLabel_->setText(QString("%1 × %2 px  |  %3")
                            .arg(originalPixmap_.width())
                            .arg(originalPixmap_.height())
                            .arg(formatBytes(fileSize_)));
        statusLabel_->setText("Loaded image replica in-app — Use Ctrl+Wheel or Zoom buttons to inspect");
        statusLabel_->setStyleSheet("color:#8ce99a;font-weight:600;border:none;");
        updateRenderedImage();
    } else {
        if (mediaPlayer_) {
            mediaPlayer_->stop();
            mediaPlayer_->setSourceDevice(nullptr);
        }
        if (videoStreamFile_) {
            if (videoStreamFile_->isOpen()) videoStreamFile_->close();
            delete videoStreamFile_;
            videoStreamFile_ = nullptr;
        }

        videoStreamFile_ = new QFile(replicaBlobPath, this);
        if (!videoStreamFile_->open(QIODevice::ReadOnly)) {
            notifyRemoteFetchFailed("Could not open local video replica stream: " + videoStreamFile_->errorString());
            return;
        }

        playPauseBtn_->setEnabled(true);
        stopBtn_->setEnabled(true);
        seekSlider_->setEnabled(true);

        // Pass the open QFile QIODevice directly to QMediaPlayer, along with a virtual URL carrying the original
        // filename extension so FFmpeg knows the container format without touching or copying files on disk.
        mediaPlayer_->setSourceDevice(videoStreamFile_, QUrl::fromLocalFile(fileName_));
        mediaPlayer_->play();
        statusLabel_->setText("Playing video stream in-app (Space to Play/Pause, Left/Right to seek 5s)");
        statusLabel_->setStyleSheet("color:#8ce99a;font-weight:600;border:none;");
    }
}

void MediaViewerDialog::notifyRemoteFetchFailed(const QString& reason) {
    statusLabel_->setText("Could not load media replica: " + reason);
    statusLabel_->setStyleSheet("color:#ff8787;font-weight:600;border:none;");
    if (imageLabel_) {
        imageLabel_->setText("Failed to load image from Pandora peers:\n" + reason);
    }
}

void MediaViewerDialog::updateRenderedImage() {
    if (originalPixmap_.isNull() || !imageLabel_) return;

    QPixmap transformed = originalPixmap_;
    if (rotationDegrees_ != 0) {
        QTransform trans;
        trans.rotate(rotationDegrees_);
        transformed = originalPixmap_.transformed(trans, Qt::SmoothTransformation);
    }

    if (fitToWindow_ && imageScroll_) {
        const QSize viewportSize = imageScroll_->viewport()->size() - QSize(16, 16);
        if (viewportSize.width() > 32 && viewportSize.height() > 32) {
            if (transformed.width() > viewportSize.width() || transformed.height() > viewportSize.height()) {
                transformed = transformed.scaled(viewportSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        }
        imageLabel_->setPixmap(transformed);
    } else {
        const QSize targetSize = transformed.size() * zoomFactor_;
        const QPixmap scaled = transformed.scaled(
            targetSize.expandedTo(QSize(16, 16)),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        imageLabel_->setPixmap(scaled);
    }
}

void MediaViewerDialog::zoomIn() {
    if (originalPixmap_.isNull()) return;
    if (fitToWindow_) {
        fitToWindow_ = false;
        fitWindowCheck_->setChecked(false);
    }
    zoomFactor_ = qBound(0.1, zoomFactor_ * 1.25, 8.0);
    zoom100Btn_->setText(QString("%1%").arg(qRound(zoomFactor_ * 100)));
    updateRenderedImage();
}

void MediaViewerDialog::zoomOut() {
    if (originalPixmap_.isNull()) return;
    if (fitToWindow_) {
        fitToWindow_ = false;
        fitWindowCheck_->setChecked(false);
    }
    zoomFactor_ = qBound(0.1, zoomFactor_ / 1.25, 8.0);
    zoom100Btn_->setText(QString("%1%").arg(qRound(zoomFactor_ * 100)));
    updateRenderedImage();
}

void MediaViewerDialog::zoomOriginal() {
    if (originalPixmap_.isNull()) return;
    fitToWindow_ = false;
    fitWindowCheck_->setChecked(false);
    zoomFactor_ = 1.0;
    zoom100Btn_->setText("100%");
    updateRenderedImage();
}

void MediaViewerDialog::toggleFitToWindow(bool fit) {
    fitToWindow_ = fit;
    if (fit) {
        zoom100Btn_->setText("100%");
    }
    updateRenderedImage();
}

void MediaViewerDialog::rotateLeft() {
    rotationDegrees_ = (rotationDegrees_ - 90) % 360;
    updateRenderedImage();
}

void MediaViewerDialog::rotateRight() {
    rotationDegrees_ = (rotationDegrees_ + 90) % 360;
    updateRenderedImage();
}

void MediaViewerDialog::togglePlayPause() {
    if (!mediaPlayer_) return;
    if (mediaPlayer_->playbackState() == QMediaPlayer::PlayingState) {
        mediaPlayer_->pause();
    } else {
        mediaPlayer_->play();
    }
}

void MediaViewerDialog::stopVideo() {
    if (!mediaPlayer_) return;
    mediaPlayer_->stop();
}

void MediaViewerDialog::onVideoPositionChanged(qint64 positionMs) {
    if (!userSeeking_ && seekSlider_) {
        seekSlider_->setValue(static_cast<int>(positionMs));
    }
    if (timeLabel_ && mediaPlayer_) {
        timeLabel_->setText(QString("%1 / %2")
                            .arg(formatDuration(positionMs), formatDuration(mediaPlayer_->duration())));
    }
}

void MediaViewerDialog::onVideoDurationChanged(qint64 durationMs) {
    if (seekSlider_) {
        seekSlider_->setRange(0, static_cast<int>(durationMs));
    }
    if (timeLabel_ && mediaPlayer_) {
        timeLabel_->setText(QString("%1 / %2")
                            .arg(formatDuration(mediaPlayer_->position()), formatDuration(durationMs)));
    }
}

void MediaViewerDialog::onSeekSliderMoved(int value) {
    if (timeLabel_ && mediaPlayer_) {
        timeLabel_->setText(QString("%1 / %2")
                            .arg(formatDuration(value), formatDuration(mediaPlayer_->duration())));
    }
}

void MediaViewerDialog::onVolumeChanged(int value) {
    if (!audioOutput_) return;
    audioOutput_->setMuted(value == 0);
    audioOutput_->setVolume(qBound(0.0f, value / 100.0f, 1.0f));
    muteBtn_->setText(audioOutput_->isMuted() ? "Muted" : "Vol");
}

void MediaViewerDialog::toggleMute() {
    if (!audioOutput_) return;
    audioOutput_->setMuted(!audioOutput_->isMuted());
    muteBtn_->setText(audioOutput_->isMuted() ? "Muted" : "Vol");
}

void MediaViewerDialog::onSpeedChanged(int index) {
    if (!mediaPlayer_ || !speedCombo_) return;
    const qreal rate = speedCombo_->itemData(index).toReal();
    if (rate > 0.0) {
        mediaPlayer_->setPlaybackRate(rate);
    }
}

void MediaViewerDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (kind_ == MediaKind::Image && fitToWindow_) {
        updateRenderedImage();
    }
}

void MediaViewerDialog::wheelEvent(QWheelEvent* event) {
    if (kind_ == MediaKind::Image && (event->modifiers() & Qt::ControlModifier)) {
        if (event->angleDelta().y() > 0) zoomIn();
        else zoomOut();
        event->accept();
        return;
    }
    QDialog::wheelEvent(event);
}

void MediaViewerDialog::keyPressEvent(QKeyEvent* event) {
    if (kind_ == MediaKind::Video && mediaPlayer_) {
        if (event->key() == Qt::Key_Space) {
            togglePlayPause();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Left) {
            mediaPlayer_->setPosition(qMax<qint64>(0, mediaPlayer_->position() - 5000));
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right) {
            mediaPlayer_->setPosition(qMin<qint64>(mediaPlayer_->duration(), mediaPlayer_->position() + 5000));
            event->accept();
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

void MediaViewerDialog::closeEvent(QCloseEvent* event) {
    if (mediaPlayer_) {
        mediaPlayer_->stop();
        mediaPlayer_->setSourceDevice(nullptr);
        mediaPlayer_->setSource(QUrl());
    }
    if (videoStreamFile_) {
        if (videoStreamFile_->isOpen()) videoStreamFile_->close();
        delete videoStreamFile_;
        videoStreamFile_ = nullptr;
    }
    event->accept();
}

} // namespace pandora
