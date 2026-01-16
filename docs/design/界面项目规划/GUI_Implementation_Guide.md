# MagnetDownload GUI 实现指南

## 🛠️ 技术栈选择

### 推荐技术栈: Qt6 + C++17

#### 选择理由
- **跨平台支持** - Windows/Linux/macOS 一套代码
- **丰富的UI组件** - 表格、树形控件、图表等现成组件
- **优秀的性能** - 原生C++性能，适合大量数据处理
- **成熟的生态** - 大量第三方库和工具支持
- **现代化外观** - 支持自定义样式和主题

#### 项目结构
```
MagnetDownload/
├── src/
│   ├── gui/                    # GUI相关代码
│   │   ├── main_window.cpp     # 主窗口
│   │   ├── dialogs/            # 对话框
│   │   ├── widgets/            # 自定义控件
│   │   ├── models/             # 数据模型
│   │   └── themes/             # 主题样式
│   ├── application/            # 应用逻辑层
│   └── ...                     # 其他模块
├── resources/                  # 资源文件
│   ├── icons/                  # 图标
│   ├── themes/                 # 主题文件
│   └── translations/           # 翻译文件
└── ui/                         # Qt Designer文件
    ├── main_window.ui
    ├── magnet_resolver_dialog.ui
    └── ...
```

## 🎨 核心组件实现

### 1. 主窗口 (MainWindow)

```cpp
// src/gui/main_window.h
#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QLabel>
#include <memory>

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
class QToolBar;
class QStatusBar;
QT_END_NAMESPACE

namespace magnet::gui {

class DownloadTableWidget;
class DetailTabWidget;
class MagnetResolverDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onAddMagnetLink();
    void onPauseDownload();
    void onResumeDownload();
    void onDeleteDownload();
    void onShowSettings();
    void onShowStatistics();
    void onDownloadSelectionChanged();

private:
    void setupUi();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupConnections();
    void loadSettings();
    void saveSettings();

    // UI组件
    QLineEdit* magnetUrlEdit_;
    DownloadTableWidget* downloadTable_;
    DetailTabWidget* detailTabs_;
    
    // 菜单和工具栏
    QMenu* fileMenu_;
    QMenu* editMenu_;
    QMenu* viewMenu_;
    QMenu* toolsMenu_;
    QMenu* helpMenu_;
    
    QToolBar* mainToolBar_;
    
    QAction* addAction_;
    QAction* pauseAction_;
    QAction* resumeAction_;
    QAction* deleteAction_;
    QAction* settingsAction_;
    QAction* exitAction_;
    
    // 状态栏
    QLabel* globalSpeedLabel_;
    QLabel* connectionLabel_;
    QLabel* diskSpaceLabel_;
    
    // 对话框
    std::unique_ptr<MagnetResolverDialog> resolverDialog_;
};

} // namespace magnet::gui
```

```cpp
// src/gui/main_window.cpp
#include "main_window.h"
#include "dialogs/magnet_resolver_dialog.h"
#include "widgets/download_table_widget.h"
#include "widgets/detail_tab_widget.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtCore/QSettings>

namespace magnet::gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , magnetUrlEdit_(nullptr)
    , downloadTable_(nullptr)
    , detailTabs_(nullptr) {
    
    setupUi();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupConnections();
    loadSettings();
    
    setWindowTitle("MagnetDownload v2.0");
    setMinimumSize(800, 600);
    resize(1200, 800);
}

MainWindow::~MainWindow() {
    saveSettings();
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    // 创建主布局
    auto* mainLayout = new QVBoxLayout(centralWidget);
    
    // 磁力链接输入区域
    auto* urlLayout = new QHBoxLayout();
    magnetUrlEdit_ = new QLineEdit(this);
    magnetUrlEdit_->setPlaceholderText("Enter magnet link here...");
    
    auto* parseButton = new QPushButton("Parse", this);
    parseButton->setFixedWidth(80);
    
    urlLayout->addWidget(magnetUrlEdit_);
    urlLayout->addWidget(parseButton);
    mainLayout->addLayout(urlLayout);
    
    // 创建分割器
    auto* splitter = new QSplitter(Qt::Vertical, this);
    
    // 下载列表
    downloadTable_ = new DownloadTableWidget(this);
    splitter->addWidget(downloadTable_);
    
    // 详细信息面板
    detailTabs_ = new DetailTabWidget(this);
    splitter->addWidget(detailTabs_);
    
    // 设置分割器比例
    splitter->setStretchFactor(0, 2);  // 下载列表占2/3
    splitter->setStretchFactor(1, 1);  // 详细信息占1/3
    
    mainLayout->addWidget(splitter);
    
    // 连接解析按钮
    connect(parseButton, &QPushButton::clicked, this, &MainWindow::onAddMagnetLink);
}

void MainWindow::setupMenuBar() {
    // 文件菜单
    fileMenu_ = menuBar()->addMenu("&File");
    
    addAction_ = new QAction("&Add Magnet Link", this);
    addAction_->setShortcut(QKeySequence::New);
    addAction_->setIcon(QIcon(":/icons/add.png"));
    fileMenu_->addAction(addAction_);
    
    fileMenu_->addSeparator();
    
    exitAction_ = new QAction("E&xit", this);
    exitAction_->setShortcut(QKeySequence::Quit);
    fileMenu_->addAction(exitAction_);
    
    // 编辑菜单
    editMenu_ = menuBar()->addMenu("&Edit");
    
    pauseAction_ = new QAction("&Pause", this);
    pauseAction_->setIcon(QIcon(":/icons/pause.png"));
    pauseAction_->setEnabled(false);
    editMenu_->addAction(pauseAction_);
    
    resumeAction_ = new QAction("&Resume", this);
    resumeAction_->setIcon(QIcon(":/icons/resume.png"));
    resumeAction_->setEnabled(false);
    editMenu_->addAction(resumeAction_);
    
    deleteAction_ = new QAction("&Delete", this);
    deleteAction_->setShortcut(QKeySequence::Delete);
    deleteAction_->setIcon(QIcon(":/icons/delete.png"));
    deleteAction_->setEnabled(false);
    editMenu_->addAction(deleteAction_);
    
    editMenu_->addSeparator();
    
    settingsAction_ = new QAction("&Settings", this);
    settingsAction_->setIcon(QIcon(":/icons/settings.png"));
    editMenu_->addAction(settingsAction_);
    
    // 查看菜单
    viewMenu_ = menuBar()->addMenu("&View");
    
    // 工具菜单
    toolsMenu_ = menuBar()->addMenu("&Tools");
    
    auto* statsAction = new QAction("&Statistics", this);
    statsAction_->setIcon(QIcon(":/icons/statistics.png"));
    toolsMenu_->addAction(statsAction);
    
    // 帮助菜单
    helpMenu_ = menuBar()->addMenu("&Help");
    
    auto* aboutAction = new QAction("&About", this);
    helpMenu_->addAction(aboutAction);
}

void MainWindow::setupToolBar() {
    mainToolBar_ = addToolBar("Main");
    mainToolBar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    
    mainToolBar_->addAction(addAction_);
    mainToolBar_->addSeparator();
    mainToolBar_->addAction(pauseAction_);
    mainToolBar_->addAction(resumeAction_);
    mainToolBar_->addAction(deleteAction_);
    mainToolBar_->addSeparator();
    mainToolBar_->addAction(settingsAction_);
}

void MainWindow::setupStatusBar() {
    globalSpeedLabel_ = new QLabel("⬇️ 0 KB/s  ⬆️ 0 KB/s", this);
    connectionLabel_ = new QLabel("👥 0 peers", this);
    diskSpaceLabel_ = new QLabel("💾 Free: 0 GB", this);
    
    statusBar()->addWidget(globalSpeedLabel_);
    statusBar()->addWidget(connectionLabel_);
    statusBar()->addPermanentWidget(diskSpaceLabel_);
}

void MainWindow::setupConnections() {
    connect(addAction_, &QAction::triggered, this, &MainWindow::onAddMagnetLink);
    connect(pauseAction_, &QAction::triggered, this, &MainWindow::onPauseDownload);
    connect(resumeAction_, &QAction::triggered, this, &MainWindow::onResumeDownload);
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::onDeleteDownload);
    connect(settingsAction_, &QAction::triggered, this, &MainWindow::onShowSettings);
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);
    
    connect(downloadTable_, &DownloadTableWidget::selectionChanged,
            this, &MainWindow::onDownloadSelectionChanged);
}

void MainWindow::onAddMagnetLink() {
    QString magnetUrl = magnetUrlEdit_->text().trimmed();
    
    if (magnetUrl.isEmpty()) {
        QMessageBox::information(this, "Add Magnet Link", 
                               "Please enter a magnet link first.");
        return;
    }
    
    if (!resolverDialog_) {
        resolverDialog_ = std::make_unique<MagnetResolverDialog>(this);
    }
    
    resolverDialog_->setMagnetUrl(magnetUrl);
    resolverDialog_->show();
    resolverDialog_->startResolving();
    
    // 清空输入框
    magnetUrlEdit_->clear();
}

void MainWindow::onDownloadSelectionChanged() {
    bool hasSelection = downloadTable_->hasSelection();
    
    pauseAction_->setEnabled(hasSelection);
    resumeAction_->setEnabled(hasSelection);
    deleteAction_->setEnabled(hasSelection);
    
    if (hasSelection) {
        auto selectedDownload = downloadTable_->getSelectedDownload();
        detailTabs_->setCurrentDownload(selectedDownload);
    } else {
        detailTabs_->clearCurrentDownload();
    }
}

void MainWindow::loadSettings() {
    QSettings settings;
    
    // 恢复窗口几何
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
}

void MainWindow::saveSettings() {
    QSettings settings;
    
    // 保存窗口几何
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

} // namespace magnet::gui
```

### 2. 磁力链接解析对话框

```cpp
// src/gui/dialogs/magnet_resolver_dialog.h
#pragma once

#include <QtWidgets/QDialog>
#include <QtCore/QTimer>
#include <memory>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QProgressBar;
class QTextEdit;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

namespace magnet::application {
class MetadataResolver;
}

namespace magnet::gui {

class MagnetResolverDialog : public QDialog {
    Q_OBJECT

public:
    explicit MagnetResolverDialog(QWidget* parent = nullptr);
    ~MagnetResolverDialog() override;

    void setMagnetUrl(const QString& url);
    void startResolving();

private slots:
    void onProgressUpdate(int percentage);
    void onPeersFound(int count);
    void onDhtNodesUpdate(int count);
    void onLogMessage(const QString& message);
    void onResolvingFinished(bool success, const QString& error);
    void onCancelClicked();

private:
    void setupUi();
    void updateNetworkStatus();
    void resetDialog();

    // UI组件
    QLineEdit* magnetUrlEdit_;
    QProgressBar* progressBar_;
    QLabel* peersLabel_;
    QLabel* dhtNodesLabel_;
    QLabel* trackersLabel_;
    QLabel* etaLabel_;
    QTextEdit* logTextEdit_;
    QPushButton* cancelButton_;

    // 业务逻辑
    std::unique_ptr<application::MetadataResolver> resolver_;
    QTimer* updateTimer_;
    
    // 状态数据
    int peersCount_;
    int dhtNodesCount_;
    int successfulTrackers_;
    int totalTrackers_;
};

} // namespace magnet::gui
```

### 3. 自定义下载表格控件

```cpp
// src/gui/widgets/download_table_widget.h
#pragma once

#include <QtWidgets/QTableWidget>
#include <QtCore/QTimer>

namespace magnet::application {
struct DownloadInfo;
}

namespace magnet::gui {

class DownloadTableWidget : public QTableWidget {
    Q_OBJECT

public:
    enum Column {
        Name = 0,
        Size = 1,
        Progress = 2,
        Speed = 3,
        Status = 4,
        ETA = 5,
        ColumnCount = 6
    };

    explicit DownloadTableWidget(QWidget* parent = nullptr);
    ~DownloadTableWidget() override;

    void addDownload(const application::DownloadInfo& info);
    void updateDownload(int downloadId, const application::DownloadInfo& info);
    void removeDownload(int downloadId);
    
    bool hasSelection() const;
    application::DownloadInfo getSelectedDownload() const;

signals:
    void selectionChanged();
    void downloadDoubleClicked(int downloadId);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
    void onSelectionChanged();
    void onUpdateTimer();
    void onPauseDownload();
    void onResumeDownload();
    void onDeleteDownload();
    void onOpenFolder();
    void onCopyMagnetLink();
    void onSetPriority();

private:
    void setupTable();
    void setupContextMenu();
    void updateRowData(int row, const application::DownloadInfo& info);
    QString formatSize(qint64 bytes) const;
    QString formatSpeed(double bytesPerSecond) const;
    QString formatTime(int seconds) const;
    QIcon getFileTypeIcon(const QString& fileName) const;

    QTimer* updateTimer_;
    QMenu* contextMenu_;
    
    // 上下文菜单动作
    QAction* pauseAction_;
    QAction* resumeAction_;
    QAction* deleteAction_;
    QAction* openFolderAction_;
    QAction* copyMagnetAction_;
    QAction* setPriorityAction_;
};

} // namespace magnet::gui
```

## 🎨 主题系统实现

### 主题管理器

```cpp
// src/gui/themes/theme_manager.h
#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QPalette>

namespace magnet::gui {

enum class ThemeType {
    Light,
    Dark,
    Auto  // 跟随系统
};

class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();
    
    void setTheme(ThemeType theme);
    ThemeType currentTheme() const { return currentTheme_; }
    
    // 颜色获取
    QColor primaryColor() const;
    QColor secondaryColor() const;
    QColor backgroundColor() const;
    QColor surfaceColor() const;
    QColor textColor() const;
    QColor successColor() const;
    QColor warningColor() const;
    QColor errorColor() const;
    
    // 样式表获取
    QString getStyleSheet() const;
    QString getWidgetStyleSheet(const QString& widgetType) const;

signals:
    void themeChanged(ThemeType newTheme);

private:
    explicit ThemeManager(QObject* parent = nullptr);
    void loadTheme(ThemeType theme);
    void applySystemTheme();

    ThemeType currentTheme_;
    QString currentStyleSheet_;
    QHash<QString, QColor> colorPalette_;
};

} // namespace magnet::gui
```

### 深色主题样式表

```cpp
// src/gui/themes/dark_theme.cpp
#include "theme_manager.h"

namespace magnet::gui {

const QString DARK_THEME_STYLESHEET = R"(
/* 主窗口样式 */
QMainWindow {
    background-color: #2C3E50;
    color: #ECF0F1;
}

/* 菜单栏样式 */
QMenuBar {
    background-color: #2C3E50;
    color: #ECF0F1;
    border-bottom: 1px solid #34495E;
}

QMenuBar::item {
    background-color: transparent;
    padding: 6px 12px;
}

QMenuBar::item:selected {
    background-color: #3498DB;
}

QMenu {
    background-color: #34495E;
    color: #ECF0F1;
    border: 1px solid #7F8C8D;
}

QMenu::item {
    padding: 6px 24px;
}

QMenu::item:selected {
    background-color: #3498DB;
}

/* 工具栏样式 */
QToolBar {
    background-color: #34495E;
    border: none;
    spacing: 3px;
    padding: 4px;
}

QToolButton {
    background-color: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 6px;
    margin: 2px;
    color: #ECF0F1;
}

QToolButton:hover {
    background-color: #3498DB;
    border-color: #2980B9;
}

QToolButton:pressed {
    background-color: #2980B9;
}

/* 表格样式 */
QTableWidget {
    background-color: #34495E;
    alternate-background-color: #2C3E50;
    selection-background-color: #3498DB;
    selection-color: #FFFFFF;
    gridline-color: #7F8C8D;
    color: #ECF0F1;
}

QTableWidget::item {
    padding: 8px;
    border-bottom: 1px solid #7F8C8D;
}

QTableWidget::item:selected {
    background-color: #3498DB;
}

QHeaderView::section {
    background-color: #2C3E50;
    color: #ECF0F1;
    padding: 8px;
    border: 1px solid #7F8C8D;
    font-weight: bold;
}

/* 进度条样式 */
QProgressBar {
    border: 1px solid #7F8C8D;
    border-radius: 4px;
    text-align: center;
    background-color: #34495E;
    color: #ECF0F1;
    font-weight: bold;
}

QProgressBar::chunk {
    background-color: #27AE60;
    border-radius: 3px;
}

/* 按钮样式 */
QPushButton {
    background-color: #3498DB;
    color: white;
    border: none;
    border-radius: 4px;
    padding: 8px 16px;
    font-weight: bold;
    min-width: 80px;
}

QPushButton:hover {
    background-color: #2980B9;
}

QPushButton:pressed {
    background-color: #21618C;
}

QPushButton:disabled {
    background-color: #7F8C8D;
    color: #BDC3C7;
}

/* 输入框样式 */
QLineEdit {
    background-color: #34495E;
    color: #ECF0F1;
    border: 1px solid #7F8C8D;
    border-radius: 4px;
    padding: 8px;
    font-size: 14px;
}

QLineEdit:focus {
    border-color: #3498DB;
}

/* 文本编辑器样式 */
QTextEdit {
    background-color: #34495E;
    color: #ECF0F1;
    border: 1px solid #7F8C8D;
    border-radius: 4px;
    font-family: 'Consolas', 'Monaco', monospace;
    font-size: 12px;
}

/* 标签样式 */
QLabel {
    color: #ECF0F1;
}

/* 状态栏样式 */
QStatusBar {
    background-color: #2C3E50;
    color: #ECF0F1;
    border-top: 1px solid #34495E;
}

/* 分割器样式 */
QSplitter::handle {
    background-color: #7F8C8D;
}

QSplitter::handle:horizontal {
    width: 2px;
}

QSplitter::handle:vertical {
    height: 2px;
}

/* 标签页样式 */
QTabWidget::pane {
    border: 1px solid #7F8C8D;
    background-color: #34495E;
}

QTabBar::tab {
    background-color: #2C3E50;
    color: #ECF0F1;
    padding: 8px 16px;
    margin-right: 2px;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
}

QTabBar::tab:selected {
    background-color: #34495E;
    border-bottom: 2px solid #3498DB;
}

QTabBar::tab:hover {
    background-color: #3498DB;
}
)";

} // namespace magnet::gui
```

## 📱 响应式设计实现

### 自适应布局管理器

```cpp
// src/gui/widgets/responsive_layout.h
#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QSize>

namespace magnet::gui {

class ResponsiveLayout : public QWidget {
    Q_OBJECT

public:
    enum BreakPoint {
        ExtraSmall = 480,   // 手机
        Small = 768,        // 平板竖屏
        Medium = 1024,      // 平板横屏/小笔记本
        Large = 1440,       // 桌面显示器
        ExtraLarge = 1920   // 大屏显示器
    };

    explicit ResponsiveLayout(QWidget* parent = nullptr);

    void setBreakPointLayout(BreakPoint breakPoint, QLayout* layout);
    BreakPoint currentBreakPoint() const { return currentBreakPoint_; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateLayout();
    BreakPoint calculateBreakPoint(const QSize& size) const;

    BreakPoint currentBreakPoint_;
    QHash<BreakPoint, QLayout*> layouts_;
};

} // namespace magnet::gui
```

这个实现指南提供了：

1. **完整的技术栈选择** - Qt6 + C++17
2. **详细的组件实现** - 主窗口、对话框、自定义控件
3. **主题系统设计** - 深色/浅色主题支持
4. **响应式布局** - 适配不同屏幕尺寸

你觉得这个实现方案如何？我们可以开始实现其中的某个组件！