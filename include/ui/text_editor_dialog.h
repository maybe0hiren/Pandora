#pragma once

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextDocument>
#include <QVector>
#include <QWidget>
#include <functional>

namespace pandora {

class CodeEditorWidget final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditorWidget(QWidget* parent = nullptr);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int lineNumberAreaWidth() const;
    void adjustFontSize(int delta);
    void resetFontSize();
    int currentFontPointSize() const { return fontPointSize_; }

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect& rect, int dy);

private:
    QWidget* lineNumberArea_{nullptr};
    int fontPointSize_{11};
};

class CodeSyntaxHighlighter final : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit CodeSyntaxHighlighter(const QString& fileName, QTextDocument* parent = nullptr);
    QString languageName() const { return languageName_; }

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<HighlightRule> rules_;
    QTextCharFormat multiLineCommentFormat_;
    QRegularExpression commentStartExpr_;
    QRegularExpression commentEndExpr_;
    bool hasMultiLineComments_{false};
    QString languageName_{"Plain Text"};
};

class TextEditorDialog final : public QDialog {
    Q_OBJECT
public:
    using SaveCallback = std::function<bool(const QByteArray& newUtf8Content, QString* errorOut)>;

    explicit TextEditorDialog(const QString& fileId,
                              const QString& fileName,
                              quint64 initialVersion,
                              SaveCallback saveCallback,
                              QWidget* parent = nullptr);

    QString fileId() const { return fileId_; }
    void setInitialContent(const QByteArray& utf8Content, quint64 version);
    void notifyRemoteFetchFailed(const QString& reason);
    void notifyExternalUpdate(quint64 newVersion);

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void saveChanges();
    void toggleFindBar();
    void findNext(bool backward = false);
    void replaceOne();
    void replaceAll();
    void toggleWordWrap(bool enabled);
    void updateStatusMetrics();
    void onDocumentModified();
    void goToLineDialog();

private:
    void updateWindowTitle();

    QString fileId_;
    QString fileName_;
    quint64 version_{1};
    SaveCallback saveCallback_;
    bool loading_{true};
    bool isDirty_{false};

    // Toolbar controls
    QLabel* fileTitleLabel_{nullptr};
    QLabel* languageBadge_{nullptr};
    QPushButton* saveButton_{nullptr};
    QPushButton* undoButton_{nullptr};
    QPushButton* redoButton_{nullptr};
    QPushButton* findToggleButton_{nullptr};
    QPushButton* gotoLineButton_{nullptr};
    QPushButton* zoomInButton_{nullptr};
    QPushButton* zoomOutButton_{nullptr};
    QPushButton* zoomResetButton_{nullptr};
    QCheckBox* wordWrapCheck_{nullptr};

    // Find & Replace Bar
    QWidget* findBar_{nullptr};
    QLineEdit* findInput_{nullptr};
    QLineEdit* replaceInput_{nullptr};
    QCheckBox* caseSensitiveCheck_{nullptr};
    QPushButton* findPrevButton_{nullptr};
    QPushButton* findNextButton_{nullptr};
    QPushButton* replaceButton_{nullptr};
    QPushButton* replaceAllButton_{nullptr};
    QPushButton* closeFindButton_{nullptr};

    // Editor & Syntax Highlighter
    CodeEditorWidget* editor_{nullptr};
    CodeSyntaxHighlighter* highlighter_{nullptr};

    // Status Bar
    QLabel* cursorPosLabel_{nullptr};
    QLabel* docStatsLabel_{nullptr};
    QLabel* syncStateLabel_{nullptr};
    QLabel* zoomLabel_{nullptr};
};

} // namespace pandora
