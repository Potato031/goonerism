#include "../Includes/timelinewidget.h"

void TimelineWidget::saveState(const QString &label) {
    TimelineState currentState;
    currentState.segments = this->segments;
    currentState.overlays = this->overlays;
    currentState.markers = this->markers;
    currentState.label = label;

    undoStack.append(currentState);
    if (undoStack.size() > MAX_STACK_SIZE) undoStack.removeFirst();

    redoStack.clear();
}

void TimelineWidget::undo() {
    if (undoStack.isEmpty()) return;

    TimelineState previousState = undoStack.takeLast();

    // previousState.label describes the action that moves forward from it to
    // the current state — that's exactly what redoing this entry reapplies.
    TimelineState currentState;
    currentState.segments = this->segments;
    currentState.overlays = this->overlays;
    currentState.markers = this->markers;
    currentState.label = previousState.label;
    redoStack.append(currentState);

    this->segments = previousState.segments;
    this->overlays = previousState.overlays;
    this->markers = previousState.markers;

    selectedSegmentIndices.clear();
    selectedSegmentIdx = -1;
    if (selectedOverlayIdx >= overlays.size()) selectedOverlayIdx = -1;
    relayout();
    update();
    emit overlaysChanged();
    emit clipTrimmed();
}

void TimelineWidget::redo() {
    if (redoStack.isEmpty()) return;

    TimelineState futureState = redoStack.takeLast();

    // futureState.label describes the action that led into it from the
    // current state — that's exactly what undoing back past it would reverse.
    TimelineState currentState;
    currentState.segments = this->segments;
    currentState.overlays = this->overlays;
    currentState.markers = this->markers;
    currentState.label = futureState.label;
    undoStack.append(currentState);

    this->segments = futureState.segments;
    this->overlays = futureState.overlays;
    this->markers = futureState.markers;

    if (selectedOverlayIdx >= overlays.size()) selectedOverlayIdx = -1;
    relayout();
    update();
    emit overlaysChanged();
    emit clipTrimmed();
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
