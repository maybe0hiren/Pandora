#include "ui/text_editor_dialog.h"
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPainter>
#include <QScrollBar>
#include <QShortcut>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace pandora {

namespace {

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(CodeEditorWidget* editor) : QWidget(editor), editor_(editor) {}
    QSize sizeHint() const override { return QSize(editor_->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* event) override { editor_->lineNumberAreaPaintEvent(event); }

private:
    CodeEditorWidget* editor_;
};

} // namespace

// ============================================================================
// CodeEditorWidget
// ============================================================================

CodeEditorWidget::CodeEditorWidget(QWidget* parent) : QPlainTextEdit(parent) {
    lineNumberArea_ = new LineNumberArea(this);

    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setFamilies({"Cascadia Mono", "Consolas", "Courier New", "Monospace"});
    monoFont.setPointSize(fontPointSize_);
    setFont(monoFont);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);

    connect(this, &CodeEditorWidget::blockCountChanged, this, &CodeEditorWidget::updateLineNumberAreaWidth);
    connect(this, &CodeEditorWidget::updateRequest, this, &CodeEditorWidget::updateLineNumberArea);
    connect(this, &CodeEditorWidget::cursorPositionChanged, this, &CodeEditorWidget::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

int CodeEditorWidget::lineNumberAreaWidth() const {
    int digits = 1;
    int maxBlocks = qMax(1, blockCount());
    while (maxBlocks >= 10) {
        maxBlocks /= 10;
        ++digits;
    }
    return 18 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void CodeEditorWidget::updateLineNumberAreaWidth(int /*newBlockCount*/) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditorWidget::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy != 0) {
        lineNumberArea_->scroll(0, dy);
    } else {
        lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth(0);
    }
}

void CodeEditorWidget::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditorWidget::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(QColor("#1f2633"));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }
    setExtraSelections(extraSelections);
}

void CodeEditorWidget::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), QColor("#151921"));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int currentBlockNum = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const QString number = QString::number(blockNumber + 1);
            if (blockNumber == currentBlockNum) {
                painter.setPen(QColor("#7ec8ff"));
                QFont f = painter.font();
                f.setBold(true);
                painter.setFont(f);
            } else {
                painter.setPen(QColor("#5b6578"));
                QFont f = painter.font();
                f.setBold(false);
                painter.setFont(f);
            }
            painter.drawText(0, top, lineNumberArea_->width() - 8, fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignVCenter, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditorWidget::adjustFontSize(int delta) {
    fontPointSize_ = qBound(8, fontPointSize_ + delta, 28);
    QFont f = font();
    f.setPointSize(fontPointSize_);
    setFont(f);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    updateLineNumberAreaWidth(0);
}

void CodeEditorWidget::resetFontSize() {
    fontPointSize_ = 11;
    adjustFontSize(0);
}

void CodeEditorWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y() > 0 ? 1 : -1;
        adjustFontSize(delta);
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

void CodeEditorWidget::keyPressEvent(QKeyEvent* event) {
    // Tab / Shift+Tab multi-line or single-line 4-space indentation
    if (event->key() == Qt::Key_Tab && !(event->modifiers() & Qt::ControlModifier)) {
        QTextCursor cursor = textCursor();
        if (cursor.hasSelection()) {
            const int startPos = cursor.selectionStart();
            const int endPos = cursor.selectionEnd();
            cursor.beginEditBlock();
            cursor.setPosition(startPos);
            const int startBlock = cursor.blockNumber();
            cursor.setPosition(endPos, QTextCursor::KeepAnchor);
            const int endBlock = cursor.blockNumber();
            for (int b = startBlock; b <= endBlock; ++b) {
                QTextBlock blk = document()->findBlockByNumber(b);
                if (!blk.isValid()) continue;
                QTextCursor lineCur(blk);
                lineCur.insertText("    ");
            }
            cursor.endEditBlock();
        } else {
            cursor.insertText("    ");
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Backtab) {
        QTextCursor cursor = textCursor();
        const int startPos = cursor.selectionStart();
        const int endPos = cursor.selectionEnd();
        cursor.beginEditBlock();
        cursor.setPosition(startPos);
        const int startBlock = cursor.blockNumber();
        cursor.setPosition(endPos, QTextCursor::KeepAnchor);
        const int endBlock = cursor.blockNumber();
        for (int b = startBlock; b <= endBlock; ++b) {
            QTextBlock blk = document()->findBlockByNumber(b);
            if (!blk.isValid()) continue;
            const QString txt = blk.text();
            int removeCount = 0;
            if (txt.startsWith('\t')) {
                removeCount = 1;
            } else {
                while (removeCount < 4 && removeCount < txt.size() && txt[removeCount] == ' ') {
                    ++removeCount;
                }
            }
            if (removeCount > 0) {
                QTextCursor lineCur(blk);
                lineCur.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, removeCount);
                lineCur.removeSelectedText();
            }
        }
        cursor.endEditBlock();
        event->accept();
        return;
    }

    // Auto-indent on Enter/Return
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        !(event->modifiers() & Qt::ShiftModifier)) {
        QTextCursor cursor = textCursor();
        const QString currentLine = cursor.block().text();
        QString indent;
        for (const QChar& ch : currentLine) {
            if (ch == ' ' || ch == '\t') indent.append(ch);
            else break;
        }
        const QString trimmed = currentLine.trimmed();
        if (trimmed.endsWith('{') || trimmed.endsWith(':')) {
            indent.append("    ");
        }
        QPlainTextEdit::keyPressEvent(event);
        if (!indent.isEmpty()) {
            textCursor().insertText(indent);
        }
        return;
    }

    QPlainTextEdit::keyPressEvent(event);
}

// ============================================================================
// CodeSyntaxHighlighter
// ============================================================================

CodeSyntaxHighlighter::CodeSyntaxHighlighter(const QString& fileName, QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
    const QString ext = QFileInfo(fileName).suffix().toLower();
    const QString base = QFileInfo(fileName).fileName().toLower();

    QTextCharFormat keywordFmt;
    keywordFmt.setForeground(QColor("#c678dd"));
    keywordFmt.setFontWeight(QFont::Bold);

    QTextCharFormat typeFmt;
    typeFmt.setForeground(QColor("#56b6c2"));

    QTextCharFormat numberFmt;
    numberFmt.setForeground(QColor("#d19a66"));

    QTextCharFormat stringFmt;
    stringFmt.setForeground(QColor("#98c379"));

    QTextCharFormat commentFmt;
    commentFmt.setForeground(QColor("#636d83"));
    commentFmt.setFontItalic(true);
    multiLineCommentFormat_ = commentFmt;

    QTextCharFormat preprocFmt;
    preprocFmt.setForeground(QColor("#e06c75"));

    QTextCharFormat funcFmt;
    funcFmt.setForeground(QColor("#61afef"));

    // Determine language family
    const bool isCppFamily = QStringList{"c", "h", "cpp", "hpp", "cc", "cxx", "hxx", "inl", "java", "cs", "js", "jsx", "ts", "tsx", "go", "rs", "swift", "kt", "kts", "dart", "scala"}.contains(ext);
    const bool isPython = QStringList{"py", "pyw", "pyi"}.contains(ext);
    const bool isShellOrConf = QStringList{"sh", "bash", "zsh", "ps1", "yaml", "yml", "toml", "ini", "cfg", "conf", "env", "cmake", "dockerfile"}.contains(ext) ||
                               base == "cmakelists.txt" || base == "makefile" || base == "dockerfile";
    const bool isJson = (ext == "json" || ext == "jsonc");
    const bool isMarkdown = (ext == "md" || ext == "markdown");
    const bool isHtmlXml = QStringList{"html", "htm", "xml", "svg", "css", "scss"}.contains(ext);

    if (isCppFamily) {
        if (ext == "c" || ext == "h") languageName_ = "C";
        else if (ext == "py") languageName_ = "Python";
        else if (ext == "js" || ext == "jsx") languageName_ = "JavaScript";
        else if (ext == "ts" || ext == "tsx") languageName_ = "TypeScript";
        else if (ext == "java") languageName_ = "Java";
        else if (ext == "cs") languageName_ = "C#";
        else if (ext == "go") languageName_ = "Go";
        else if (ext == "rs") languageName_ = "Rust";
        else languageName_ = "C++";

        const QStringList keywords = {
            "alignas", "alignof", "and", "auto", "bool", "break", "case", "catch", "char", "class",
            "const", "constexpr", "consteval", "continue", "default", "delete", "do", "double", "else",
            "enum", "explicit", "export", "extern", "false", "final", "float", "for", "friend", "goto",
            "if", "import", "inline", "int", "interface", "let", "long", "mutable", "namespace", "new",
            "noexcept", "null", "nullptr", "operator", "override", "package", "private", "protected",
            "public", "register", "reinterpret_cast", "return", "short", "signed", "sizeof", "static",
            "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
            "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
            "var", "virtual", "void", "volatile", "wchar_t", "while", "async", "await", "fn", "mut",
            "pub", "impl", "trait", "match", "func", "defer"
        };
        for (const QString& kw : keywords) {
            rules_.append({QRegularExpression(QStringLiteral("\\b%1\\b").arg(kw)), keywordFmt});
        }
        rules_.append({QRegularExpression(QStringLiteral("\\bQ[A-Z][A-Za-z0-9_]*\\b")), typeFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\bstd::[A-Za-z0-9_]+\\b")), typeFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\b[A-Za-z0-9_]+(?=\\s*\\()")), funcFmt});
        rules_.append({QRegularExpression(QStringLiteral("^\\s*#[A-Za-z0-9_]+.*")), preprocFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\b0x[0-9A-Fa-f]+\\b|\\b\\d+(\\.\\d+)?\\b")), numberFmt});
        rules_.append({QRegularExpression(QStringLiteral("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'")), stringFmt});
        rules_.append({QRegularExpression(QStringLiteral("//[^\n]*")), commentFmt});

        hasMultiLineComments_ = true;
        commentStartExpr_ = QRegularExpression(QStringLiteral("/\\*"));
        commentEndExpr_ = QRegularExpression(QStringLiteral("\\*/"));
    } else if (isPython) {
        languageName_ = "Python";
        const QStringList pyKeywords = {
            "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del",
            "elif", "else", "except", "False", "finally", "for", "from", "global", "if", "import",
            "in", "is", "lambda", "None", "nonlocal", "not", "or", "pass", "raise", "return",
            "True", "try", "while", "with", "yield", "self", "match", "case"
        };
        for (const QString& kw : pyKeywords) {
            rules_.append({QRegularExpression(QStringLiteral("\\b%1\\b").arg(kw)), keywordFmt});
        }
        rules_.append({QRegularExpression(QStringLiteral("@[A-Za-z0-9_.]+")), preprocFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\b[A-Za-z0-9_]+(?=\\s*\\()")), funcFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\b0x[0-9A-Fa-f]+\\b|\\b\\d+(\\.\\d+)?\\b")), numberFmt});
        rules_.append({QRegularExpression(QStringLiteral("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'")), stringFmt});
        rules_.append({QRegularExpression(QStringLiteral("#[^\n]*")), commentFmt});
    } else if (isJson) {
        languageName_ = "JSON";
        rules_.append({QRegularExpression(QStringLiteral("\\b(true|false|null)\\b")), keywordFmt});
        rules_.append({QRegularExpression(QStringLiteral("-?\\b\\d+(\\.\\d+)?([eE][+-]?\\d+)?\\b")), numberFmt});
        rules_.append({QRegularExpression(QStringLiteral("\"([^\"\\\\]|\\\\.)*\"(?=\\s*:)")), funcFmt});
        rules_.append({QRegularExpression(QStringLiteral(":\\s*\"([^\"\\\\]|\\\\.)*\"")), stringFmt});
    } else if (isMarkdown) {
        languageName_ = "Markdown";
        rules_.append({QRegularExpression(QStringLiteral("^#{1,6}\\s+.*")), keywordFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\*\\*[^*]+\\*\\*|__[^_]+__")), typeFmt});
        rules_.append({QRegularExpression(QStringLiteral("`[^`]+`")), stringFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\[[^\\]]+\\]\\([^\\)]+\\)")), funcFmt});
    } else if (isHtmlXml) {
        languageName_ = ext.toUpper();
        rules_.append({QRegularExpression(QStringLiteral("</?[A-Za-z0-9_-]+|/?\\s*>")), keywordFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\b[A-Za-z0-9_-]+(?=\\=)")), typeFmt});
        rules_.append({QRegularExpression(QStringLiteral("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'")), stringFmt});
        hasMultiLineComments_ = true;
        commentStartExpr_ = QRegularExpression(QStringLiteral("<!--"));
        commentEndExpr_ = QRegularExpression(QStringLiteral("-->"));
    } else if (isShellOrConf) {
        languageName_ = ext.isEmpty() ? "Config / Script" : ext.toUpper();
        rules_.append({QRegularExpression(QStringLiteral("\\b(if|then|else|elif|fi|for|while|do|done|function|return|export|local|set|true|false)\\b")), keywordFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\$[A-Za-z0-9_{}]+")), typeFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\b\\d+(\\.\\d+)?\\b")), numberFmt});
        rules_.append({QRegularExpression(QStringLiteral("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'")), stringFmt});
        rules_.append({QRegularExpression(QStringLiteral("#[^\n]*")), commentFmt});
    } else {
        languageName_ = ext.isEmpty() ? "Plain Text" : QString("Text (.%1)").arg(ext);
        rules_.append({QRegularExpression(QStringLiteral("\"([^\"\\\\]|\\\\.)*\"")), stringFmt});
        rules_.append({QRegularExpression(QStringLiteral("\\b\\d+(\\.\\d+)?\\b")), numberFmt});
    }
}

void CodeSyntaxHighlighter::highlightBlock(const QString& text) {
    for (const HighlightRule& rule : rules_) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    if (!hasMultiLineComments_) return;

    setCurrentBlockState(0);
    int startIndex = 0;
    if (previousBlockState() != 1) {
        startIndex = text.indexOf(commentStartExpr_);
    }

    while (startIndex >= 0) {
        const QRegularExpressionMatch endMatch = commentEndExpr_.match(text, startIndex);
        int endIndex = endMatch.capturedStart();
        int commentLength = 0;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + endMatch.capturedLength();
        }
        setFormat(startIndex, commentLength, multiLineCommentFormat_);
        startIndex = text.indexOf(commentStartExpr_, startIndex + commentLength);
    }
}

// ============================================================================
// TextEditorDialog
// ============================================================================

TextEditorDialog::TextEditorDialog(const QString& fileId,
                                   const QString& fileName,
                                   quint64 initialVersion,
                                   SaveCallback saveCallback,
                                   QWidget* parent)
    : QDialog(parent),
      fileId_(fileId),
      fileName_(fileName),
      version_(initialVersion),
      saveCallback_(std::move(saveCallback)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint);
    resize(980, 680);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 10);
    rootLayout->setSpacing(8);

    // 1. Top Toolbar
    auto* toolbarFrame = new QWidget;
    toolbarFrame->setStyleSheet("background:#1b1f28;border:1px solid #2d3442;border-radius:5px;");
    auto* toolbar = new QHBoxLayout(toolbarFrame);
    toolbar->setContentsMargins(10, 6, 10, 6);
    toolbar->setSpacing(6);

    fileTitleLabel_ = new QLabel;
    fileTitleLabel_->setStyleSheet("font-weight:bold;font-size:13px;color:#7ec8ff;border:none;");
    toolbar->addWidget(fileTitleLabel_);

    languageBadge_ = new QLabel("Text");
    languageBadge_->setStyleSheet("background:#263042;color:#b8c7e0;padding:3px 9px;border-radius:3px;font-size:11px;font-weight:600;border:none;");
    toolbar->addWidget(languageBadge_);

    toolbar->addStretch();

    saveButton_ = new QPushButton("Save && Sync (Ctrl+S)");
    saveButton_->setStyleSheet(
        "QPushButton { background:#236841; color:#ffffff; padding:6px 13px; border-radius:4px; font-weight:bold; }"
        "QPushButton:hover { background:#2c8252; }"
        "QPushButton:disabled { background:#232730; color:#5e6675; }");
    saveButton_->setEnabled(false);

    undoButton_ = new QPushButton("Undo");
    redoButton_ = new QPushButton("Redo");
    findToggleButton_ = new QPushButton("Find / Replace (Ctrl+F)");
    gotoLineButton_ = new QPushButton("Go to Line (Ctrl+G)");
    zoomOutButton_ = new QPushButton("A-");
    zoomResetButton_ = new QPushButton("100%");
    zoomInButton_ = new QPushButton("A+");
    zoomOutButton_->setFixedWidth(34);
    zoomResetButton_->setFixedWidth(48);
    zoomInButton_->setFixedWidth(34);

    wordWrapCheck_ = new QCheckBox("Word Wrap");
    wordWrapCheck_->setChecked(false);
    wordWrapCheck_->setStyleSheet("color:#c8d0e0;border:none;padding-left:4px;");

    toolbar->addWidget(saveButton_);
    toolbar->addWidget(undoButton_);
    toolbar->addWidget(redoButton_);
    toolbar->addWidget(findToggleButton_);
    toolbar->addWidget(gotoLineButton_);
    toolbar->addWidget(zoomOutButton_);
    toolbar->addWidget(zoomResetButton_);
    toolbar->addWidget(zoomInButton_);
    toolbar->addWidget(wordWrapCheck_);
    rootLayout->addWidget(toolbarFrame);

    // 2. Collapsible Find & Replace Bar
    findBar_ = new QWidget;
    findBar_->setStyleSheet("background:#1e232e;border:1px solid #323a4a;border-radius:5px;");
    auto* findLayout = new QHBoxLayout(findBar_);
    findLayout->setContentsMargins(10, 6, 10, 6);
    findLayout->setSpacing(6);

    findInput_ = new QLineEdit;
    findInput_->setPlaceholderText("Find text...");
    replaceInput_ = new QLineEdit;
    replaceInput_->setPlaceholderText("Replace with...");
    caseSensitiveCheck_ = new QCheckBox("Match Case");
    caseSensitiveCheck_->setStyleSheet("color:#c8d0e0;border:none;");

    findPrevButton_ = new QPushButton("Prev");
    findNextButton_ = new QPushButton("Next");
    replaceButton_ = new QPushButton("Replace");
    replaceAllButton_ = new QPushButton("Replace All");
    closeFindButton_ = new QPushButton("Close");

    findLayout->addWidget(new QLabel("Find:"));
    findLayout->addWidget(findInput_, 1);
    findLayout->addWidget(findPrevButton_);
    findLayout->addWidget(findNextButton_);
    findLayout->addSpacing(8);
    findLayout->addWidget(new QLabel("Replace:"));
    findLayout->addWidget(replaceInput_, 1);
    findLayout->addWidget(replaceButton_);
    findLayout->addWidget(replaceAllButton_);
    findLayout->addWidget(caseSensitiveCheck_);
    findLayout->addWidget(closeFindButton_);
    findBar_->setVisible(false);
    rootLayout->addWidget(findBar_);

    // 3. Main Code Editor with Line Numbers & Syntax Highlighter
    editor_ = new CodeEditorWidget(this);
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor_->setReadOnly(true);
    editor_->setPlainText("Loading file content from Pandora session storage...");
    highlighter_ = new CodeSyntaxHighlighter(fileName_, editor_->document());
    languageBadge_->setText(highlighter_->languageName());
    rootLayout->addWidget(editor_, 1);

    // 4. Bottom Status Bar
    auto* statusFrame = new QWidget;
    statusFrame->setStyleSheet("background:#191d25;border:1px solid #2b313d;border-radius:4px;");
    auto* statusLayout = new QHBoxLayout(statusFrame);
    statusLayout->setContentsMargins(10, 4, 10, 4);
    statusLayout->setSpacing(16);

    syncStateLabel_ = new QLabel("Loading replica into memory (no file downloaded to disk)...");
    syncStateLabel_->setStyleSheet("color:#7ec8ff;font-weight:600;border:none;");
    cursorPosLabel_ = new QLabel("Ln 1, Col 1");
    cursorPosLabel_->setStyleSheet("color:#aab2c0;border:none;");
    docStatsLabel_ = new QLabel("0 lines  |  0 chars  |  0 B");
    docStatsLabel_->setStyleSheet("color:#aab2c0;border:none;");
    zoomLabel_ = new QLabel("11 pt  |  UTF-8  |  In-App Memory Only");
    zoomLabel_->setStyleSheet("color:#8b95a7;border:none;");

    statusLayout->addWidget(syncStateLabel_);
    statusLayout->addStretch();
    statusLayout->addWidget(cursorPosLabel_);
    statusLayout->addWidget(docStatsLabel_);
    statusLayout->addWidget(zoomLabel_);
    rootLayout->addWidget(statusFrame);

    setStyleSheet(
        "QDialog { background:#13161c; color:#e8eaf0; font-family:'Segoe UI', sans-serif; font-size:12px; }"
        "QPlainTextEdit { background:#181c24; color:#e6edf3; border:1px solid #2e3543; border-radius:4px; selection-background-color:#264f78; }"
        "QLineEdit { background:#141820; color:#e8eaf0; border:1px solid #384152; padding:4px 8px; border-radius:4px; }"
        "QLineEdit:focus { border:1px solid #589bf5; }"
        "QPushButton { background:#283447; color:#e8eaf0; border:1px solid #38465e; padding:5px 10px; border-radius:4px; font-weight:600; }"
        "QPushButton:hover { background:#354660; }"
        "QPushButton:disabled { background:#1f232c; color:#586070; border-color:#2a2f3a; }");

    // Connections & Shortcuts
    connect(saveButton_, &QPushButton::clicked, this, &TextEditorDialog::saveChanges);
    connect(undoButton_, &QPushButton::clicked, editor_, &QPlainTextEdit::undo);
    connect(redoButton_, &QPushButton::clicked, editor_, &QPlainTextEdit::redo);
    connect(findToggleButton_, &QPushButton::clicked, this, &TextEditorDialog::toggleFindBar);
    connect(gotoLineButton_, &QPushButton::clicked, this, &TextEditorDialog::goToLineDialog);

    connect(zoomInButton_, &QPushButton::clicked, this, [this]() {
        editor_->adjustFontSize(1);
        updateStatusMetrics();
    });
    connect(zoomOutButton_, &QPushButton::clicked, this, [this]() {
        editor_->adjustFontSize(-1);
        updateStatusMetrics();
    });
    connect(zoomResetButton_, &QPushButton::clicked, this, [this]() {
        editor_->resetFontSize();
        updateStatusMetrics();
    });
    connect(wordWrapCheck_, &QCheckBox::toggled, this, &TextEditorDialog::toggleWordWrap);

    connect(findNextButton_, &QPushButton::clicked, this, [this]() { findNext(false); });
    connect(findPrevButton_, &QPushButton::clicked, this, [this]() { findNext(true); });
    connect(findInput_, &QLineEdit::returnPressed, this, [this]() { findNext(false); });
    connect(replaceButton_, &QPushButton::clicked, this, &TextEditorDialog::replaceOne);
    connect(replaceAllButton_, &QPushButton::clicked, this, &TextEditorDialog::replaceAll);
    connect(closeFindButton_, &QPushButton::clicked, this, [this]() {
        findBar_->setVisible(false);
        editor_->setFocus();
    });

    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, &TextEditorDialog::updateStatusMetrics);
    connect(editor_, &QPlainTextEdit::textChanged, this, &TextEditorDialog::onDocumentModified);

    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &TextEditorDialog::saveChanges);

    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, &TextEditorDialog::toggleFindBar);

    auto* replaceShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_H), this);
    connect(replaceShortcut, &QShortcut::activated, this, &TextEditorDialog::toggleFindBar);

    auto* gotoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);
    connect(gotoShortcut, &QShortcut::activated, this, &TextEditorDialog::goToLineDialog);

    updateWindowTitle();
}

void TextEditorDialog::setInitialContent(const QByteArray& utf8Content, quint64 version) {
    loading_ = true;
    version_ = version;
    editor_->setReadOnly(false);
    editor_->setPlainText(QString::fromUtf8(utf8Content));
    editor_->document()->clearUndoRedoStacks();
    editor_->document()->setModified(false);
    isDirty_ = false;
    loading_ = false;

    saveButton_->setEnabled(false);
    syncStateLabel_->setText(QString("Loaded v%1 in memory — Ready to edit").arg(version_));
    syncStateLabel_->setStyleSheet("color:#8ce99a;font-weight:600;border:none;");
    updateWindowTitle();
    updateStatusMetrics();
    editor_->setFocus();
}

void TextEditorDialog::notifyRemoteFetchFailed(const QString& reason) {
    loading_ = false;
    editor_->setReadOnly(true);
    editor_->setPlainText("Could not load file content from Pandora peers:\n" + reason);
    syncStateLabel_->setText("Load failed: " + reason);
    syncStateLabel_->setStyleSheet("color:#ff8787;font-weight:600;border:none;");
}

void TextEditorDialog::notifyExternalUpdate(quint64 newVersion) {
    if (newVersion <= version_) return;
    version_ = newVersion;
    updateWindowTitle();
    if (!isDirty_) {
        syncStateLabel_->setText(QString("Synced across Pandora mesh (v%1)").arg(version_));
        syncStateLabel_->setStyleSheet("color:#8ce99a;font-weight:600;border:none;");
    } else {
        syncStateLabel_->setText(QString("Remote peer updated file to v%1 — Saving will publish v%2")
                                 .arg(version_).arg(version_ + 1));
        syncStateLabel_->setStyleSheet("color:#ffd43b;font-weight:600;border:none;");
    }
}

void TextEditorDialog::onDocumentModified() {
    if (loading_) return;
    isDirty_ = true;
    saveButton_->setEnabled(true);
    syncStateLabel_->setText("Unsaved edits — Press Ctrl+S or 'Save & Sync' to replicate to all peers");
    syncStateLabel_->setStyleSheet("color:#ffd43b;font-weight:600;border:none;");
    updateWindowTitle();
    updateStatusMetrics();
}

void TextEditorDialog::saveChanges() {
    if (loading_ || editor_->isReadOnly() || !saveCallback_) return;

    const QByteArray utf8Bytes = editor_->toPlainText().toUtf8();
    QString err;
    if (!saveCallback_(utf8Bytes, &err)) {
        QMessageBox::warning(this, "Save & Sync Failed",
                             "Could not save changes to Pandora session:\n" + err);
        return;
    }

    ++version_;
    isDirty_ = false;
    editor_->document()->setModified(false);
    saveButton_->setEnabled(false);
    syncStateLabel_->setText(QString("Saved & broadcast v%1 to all connected Pandora devices").arg(version_));
    syncStateLabel_->setStyleSheet("color:#69db7c;font-weight:bold;border:none;");
    updateWindowTitle();
    updateStatusMetrics();
}

void TextEditorDialog::toggleFindBar() {
    const bool show = !findBar_->isVisible();
    findBar_->setVisible(show);
    if (show) {
        const QString selected = editor_->textCursor().selectedText();
        if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator)) {
            findInput_->setText(selected);
        }
        findInput_->setFocus();
        findInput_->selectAll();
    } else {
        editor_->setFocus();
    }
}

void TextEditorDialog::findNext(bool backward) {
    const QString query = findInput_->text();
    if (query.isEmpty()) return;

    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    if (caseSensitiveCheck_->isChecked()) flags |= QTextDocument::FindCaseSensitively;

    if (!editor_->find(query, flags)) {
        // Wrap around document
        QTextCursor wrapCursor = editor_->textCursor();
        wrapCursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        editor_->setTextCursor(wrapCursor);
        if (!editor_->find(query, flags)) {
            syncStateLabel_->setText(QString("No occurrences of '%1' found").arg(query));
            syncStateLabel_->setStyleSheet("color:#ffa94d;font-weight:600;border:none;");
        }
    }
}

void TextEditorDialog::replaceOne() {
    const QString query = findInput_->text();
    if (query.isEmpty()) return;

    QTextCursor cursor = editor_->textCursor();
    const Qt::CaseSensitivity cs = caseSensitiveCheck_->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (cursor.hasSelection() && cursor.selectedText().compare(query, cs) == 0) {
        cursor.insertText(replaceInput_->text());
    }
    findNext(false);
}

void TextEditorDialog::replaceAll() {
    const QString query = findInput_->text();
    if (query.isEmpty()) return;
    const QString replacement = replaceInput_->text();

    QTextDocument::FindFlags flags;
    if (caseSensitiveCheck_->isChecked()) flags |= QTextDocument::FindCaseSensitively;

    QTextCursor cursor = editor_->textCursor();
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::Start);
    editor_->setTextCursor(cursor);

    int count = 0;
    while (editor_->find(query, flags)) {
        editor_->textCursor().insertText(replacement);
        ++count;
    }
    cursor.endEditBlock();
    syncStateLabel_->setText(QString("Replaced %1 occurrence(s) of '%2'").arg(count).arg(query));
    syncStateLabel_->setStyleSheet("color:#7ec8ff;font-weight:600;border:none;");
}

void TextEditorDialog::toggleWordWrap(bool enabled) {
    editor_->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void TextEditorDialog::goToLineDialog() {
    const int maxLines = qMax(1, editor_->blockCount());
    const int currentLine = editor_->textCursor().blockNumber() + 1;
    bool ok = false;
    const int targetLine = QInputDialog::getInt(
        this, "Go to Line",
        QString("Enter line number (1 - %1):").arg(maxLines),
        currentLine, 1, maxLines, 1, &ok);
    if (!ok) return;

    const QTextBlock block = editor_->document()->findBlockByNumber(targetLine - 1);
    if (block.isValid()) {
        QTextCursor cur(block);
        editor_->setTextCursor(cur);
        editor_->centerCursor();
        editor_->setFocus();
    }
}

void TextEditorDialog::updateStatusMetrics() {
    const QTextCursor cur = editor_->textCursor();
    const int line = cur.blockNumber() + 1;
    const int col = cur.columnNumber() + 1;
    cursorPosLabel_->setText(QString("Ln %1, Col %2").arg(line).arg(col));

    const QString text = editor_->toPlainText();
    const int lines = qMax(1, editor_->blockCount());
    const int chars = text.size();
    const qint64 bytes = text.toUtf8().size();
    docStatsLabel_->setText(QString("%1 lines  |  %2 chars  |  %3 B").arg(lines).arg(chars).arg(bytes));
    zoomLabel_->setText(QString("%1 pt  |  UTF-8  |  In-App Memory Only").arg(editor_->currentFontPointSize()));
}

void TextEditorDialog::updateWindowTitle() {
    const QString dirtyMark = isDirty_ ? " ● [Unsaved]" : "";
    setWindowTitle(QString("Pandora Text Editor — %1 (v%2)%3").arg(fileName_).arg(version_).arg(dirtyMark));
    fileTitleLabel_->setText(QString("%1  (v%2)%3").arg(fileName_).arg(version_).arg(dirtyMark));
}

void TextEditorDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && findBar_->isVisible()) {
        findBar_->setVisible(false);
        editor_->setFocus();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void TextEditorDialog::closeEvent(QCloseEvent* event) {
    if (isDirty_) {
        const auto res = QMessageBox::question(
            this, "Unsaved Changes",
            QString("'%1' has unsaved edits.\n\nDo you want to Save & Sync changes across all Pandora devices before closing?")
                .arg(fileName_),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (res == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (res == QMessageBox::Save) {
            saveChanges();
            if (isDirty_) {
                event->ignore();
                return;
            }
        }
    }
    event->accept();
}

} // namespace pandora
