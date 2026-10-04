#include "../Includes/timelinewidget.h"

void TimelineWidget::saveState(const QString &label) {
    ++editRevision;
    TimelineState currentState;
    currentState.segments = this->segments;
    currentState.overlays = this->overlays;
    currentState.markers = this->markers;
    currentState.sources = this->sources;
    currentState.durationMs = this->durationMs;
    currentState.label = label;

    undoStack.append(currentState);
    if (undoStack.size() > MAX_STACK_SIZE) undoStack.removeFirst();

    redoStack.clear();
    emit historyChanged();
}

void TimelineWidget::undo() {
    if (undoStack.isEmpty()) return;

    ++editRevision;
    TimelineState previousState = undoStack.takeLast();

    // previousState.label describes the action that moves forward from it to
    // the current state — that's exactly what redoing this entry reapplies.
    TimelineState currentState;
    currentState.segments = this->segments;
    currentState.overlays = this->overlays;
    currentState.markers = this->markers;
    currentState.sources = this->sources;
    currentState.durationMs = this->durationMs;
    currentState.label = previousState.label;
    redoStack.append(currentState);

    this->segments = previousState.segments;
    this->overlays = previousState.overlays;
    this->markers = previousState.markers;
    this->sources = previousState.sources;
    this->durationMs = previousState.durationMs;

    selectedSegmentIndices.clear();
    selectedSegmentIdx = -1;
    if (selectedOverlayIdx >= overlays.size()) selectedOverlayIdx = -1;
    relayout();
    update();
    emit overlaysChanged();
    emit clipTrimmed();
    emit historyChanged();
}

void TimelineWidget::redo() {
    if (redoStack.isEmpty()) return;

    ++editRevision;
    TimelineState futureState = redoStack.takeLast();

    // futureState.label describes the action that led into it from the
    // current state — that's exactly what undoing back past it would reverse.
    TimelineState currentState;
    currentState.segments = this->segments;
    currentState.overlays = this->overlays;
    currentState.markers = this->markers;
    currentState.sources = this->sources;
    currentState.durationMs = this->durationMs;
    currentState.label = futureState.label;
    undoStack.append(currentState);

    this->segments = futureState.segments;
    this->overlays = futureState.overlays;
    this->markers = futureState.markers;
    this->sources = futureState.sources;
    this->durationMs = futureState.durationMs;

    selectedSegmentIndices.clear();
    selectedSegmentIdx = -1;
    if (selectedOverlayIdx >= overlays.size()) selectedOverlayIdx = -1;
    relayout();
    update();
    emit overlaysChanged();
    emit clipTrimmed();
    emit historyChanged();
}

QStringList TimelineWidget::undoHistoryLabels() const {
    QStringList labels;
    for (const auto &s : undoStack) labels << (s.label.isEmpty() ? QStringLiteral("Edit") : s.label);
    return labels;
}

QStringList TimelineWidget::redoHistoryLabels() const {
    QStringList labels;
    for (const auto &s : redoStack) labels << (s.label.isEmpty() ? QStringLiteral("Edit") : s.label);
    return labels;
}
