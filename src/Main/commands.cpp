#include "../Includes/mainWindow.h"
#include <QDialogButtonBox>
#include <QListWidget>
#include <QShortcut>
#include <QAction>
#include <QMenu>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QTabWidget>
#include <functional>

namespace {
class CommandDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem row(option);
        initStyleOption(&row, index);
        const QString shortcut = index.data(Qt::UserRole + 1).toString();
        const int shortcutWidth = row.fontMetrics.horizontalAdvance(shortcut);
        row.text = row.fontMetrics.elidedText(row.text, Qt::ElideRight,
                                             row.rect.width() - shortcutWidth - 40);
        const auto *style = row.widget ? row.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &row, painter, row.widget);
        painter->save();
        painter->setFont(row.font);
        painter->setPen(row.palette.color(QPalette::Text));
        painter->setOpacity(0.65);
        painter->drawText(row.rect.adjusted(12, 0, -12, 0), Qt::AlignRight | Qt::AlignVCenter, shortcut);
        painter->restore();
    }
};
}

void MainWindow::updateEditActions() {
    undoBtn->setEnabled(timeline->canUndo());
    redoBtn->setEnabled(timeline->canRedo());
    historyBtn->setEnabled(timeline->canUndo() || timeline->canRedo());
    const auto undo = timeline->undoHistoryLabels();
    const auto redo = timeline->redoHistoryLabels();
    undoBtn->setToolTip(undo.isEmpty() ? "Nothing to undo" : "Undo " + undo.last() + " (" + editorSettings.keyUndo + ")");
    redoBtn->setToolTip(redo.isEmpty() ? "Nothing to redo" : "Redo " + redo.first() + " (" + editorSettings.keyRedo + ")");
}

void MainWindow::resetPanelLayout() {
    viewMenu->actions()[0]->setChecked(true);
    viewMenu->actions()[1]->setChecked(true);
    setLibraryPageVisible(0, true);
    setLibraryPageVisible(1, true);
    libraryTabs->setCurrentIndex(0);
    if (editorSettings.sidebarPosition == "right") topPaneSplitter->setSizes({1000, 260});
    else topPaneSplitter->setSizes({260, 1000});
    mainSplitter->setSizes({540, 280});
}

void MainWindow::setLibraryPageVisible(int index, bool visible) {
    libraryTabs->setTabVisible(index, visible);
    libraryTabs->setVisible(libraryTabs->isTabVisible(0) || libraryTabs->isTabVisible(1));
}

void MainWindow::showCommandPalette() {
    struct Command { QString label; QString key; std::function<void()> run; bool enabled; };
    QList<Command> commands;
    auto buttonCommand = [&commands](const QString &name, QPushButton *button, const QString &key = QString()) {
        commands.append({name, key, [button]() { button->click(); }, button->isEnabled()});
    };
    buttonCommand("Import media", importBtn, "Ctrl+O");
    buttonCommand("Play / pause", playPauseBtn, editorSettings.keyPlayPause);
    buttonCommand("Split clip at playhead", splitBtn, editorSettings.keySplit);
    buttonCommand("Delete selected clips or effect", deleteClipBtn, editorSettings.keyDeleteClip);
    buttonCommand("Undo", undoBtn, editorSettings.keyUndo);
    buttonCommand("Redo", redoBtn, editorSettings.keyRedo);
    buttonCommand("Add text", textBtn);
    buttonCommand("Add blur region", blurBtn);
    buttonCommand("Add pixelated region", pixelBtn);
    buttonCommand("Add blackout region", solidBtn);
    buttonCommand("Add shape or arrow", shapeBtn);
    buttonCommand("Color correction", colorCorrectBtn);
    buttonCommand("Auto-cut silence", autoCutBtn);
    buttonCommand("Speed ramp", speedRampBtn);
    buttonCommand("Reset crop", resetCropBtn);
    buttonCommand("Save current frame as PNG", snapshotBtn);
    buttonCommand("Fullscreen preview", fullscreenBtn);
    buttonCommand("Fit timeline", timelineFitBtn);
    for (QAction *action : exportMenu->actions()) {
        commands.append({action->text(), "", [action]() { action->trigger(); }, action->isEnabled() && !exportBusy});
    }
    commands.append({"Add / remove marker", editorSettings.keyAddMarker,
                     [this]() { timeline->toggleMarkerAtPlayhead(); }, !currentMediaPath.isEmpty()});
    commands.append({"Show / hide media panel", "", [this]() { viewMenu->actions()[0]->trigger(); }, true});
    commands.append({"Show / hide effects panel", "", [this]() { viewMenu->actions()[1]->trigger(); }, true});
    commands.append({"Reset panel layout", "", [this]() { resetPanelLayout(); }, true});
    buttonCommand("Keyboard shortcuts", helpBtn);
    buttonCommand("Settings", settingsBtn);

    QDialog dialog(this);
    dialog.setObjectName("CommandPalette");
    dialog.setWindowTitle("Find an action");
    dialog.resize(540, 460);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *search = new QLineEdit(&dialog);
    search->setObjectName("CommandSearch");
    search->setPlaceholderText("Search actions…");
    search->setAccessibleName("Search actions");
    auto *list = new QListWidget(&dialog);
    list->setItemDelegate(new CommandDelegate(list));
    list->setObjectName("CommandList");
    list->setAccessibleName("Available actions");
    layout->addWidget(search);
    layout->addWidget(list, 1);
    auto *hint = new QLabel("↑ ↓ Navigate    Enter Run    Esc Close", &dialog);
    hint->setObjectName("SubtleHint");
    layout->addWidget(hint);
    auto populate = [&commands, list](const QString &query) {
        list->clear();
        const auto words = query.simplified().split(' ', Qt::SkipEmptyParts);
        for (int index = 0; index < commands.size(); ++index) {
            const auto &command = commands[index];
            bool match = true;
            for (const auto &word : words) match &= command.label.contains(word, Qt::CaseInsensitive);
            if (!match) continue;
            auto *item = new QListWidgetItem(command.label, list);
            item->setData(Qt::UserRole, index);
            item->setData(Qt::UserRole + 1, command.key);
            item->setData(Qt::AccessibleDescriptionRole, command.key);
            if (!command.enabled) { item->setFlags(Qt::NoItemFlags); item->setToolTip("Open suitable media to use this action."); }
        }
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->flags().testFlag(Qt::ItemIsEnabled)) { list->setCurrentRow(row); break; }
        }
    };
    connect(search, &QLineEdit::textChanged, &dialog, populate);
    int chosen = -1;
    auto choose = [&]() {
        auto *item = list->currentItem();
        if (!item || !item->flags().testFlag(Qt::ItemIsEnabled)) return;
        chosen = item->data(Qt::UserRole).toInt();
        dialog.accept();
    };
    connect(search, &QLineEdit::returnPressed, &dialog, choose);
    connect(list, &QListWidget::itemActivated, &dialog, [choose](QListWidgetItem*) { choose(); });
    for (const auto key : {Qt::Key_Up, Qt::Key_Down}) {
        auto *shortcut = new QShortcut(QKeySequence(key), &dialog);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, &dialog, [list, key]() {
            const int delta = key == Qt::Key_Up ? -1 : 1;
            for (int row = list->currentRow() + delta; row >= 0 && row < list->count(); row += delta) {
                if (list->item(row)->flags().testFlag(Qt::ItemIsEnabled)) { list->setCurrentRow(row); return; }
            }
        });
    }
    populate("");
    search->setFocus();
    if (dialog.exec() == QDialog::Accepted && chosen >= 0) commands[chosen].run();
}
