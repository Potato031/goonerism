#include "../Includes/timelinewidget.h"

void TimelineWidget::saveState() {
    TimelineState currentState;
    currentState.segments = this->segments;
    currentState.overlays = this->overlays;

    undoStack.append(currentState);
    if (undoStack.size() > MAX_STACK_SIZE) undoStack.removeFirst();

    redoStack.clear();
}

void TimelineWidget::undo() {
    if (undoStack.isEmpty()) return;

    TimelineState currentState;
    currentState.segments = this->segments;
    currentState.overlays = this->overlays;
    redoStack.append(currentState);

    TimelineState previousState = undoStack.takeLast();
    this->segments = previousState.segments;
    this->overlays = previousState.overlays;

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

    TimelineState currentState;
    currentState.segments = this->segments;
    currentState.overlays = this->overlays;
    undoStack.append(currentState);

    TimelineState futureState = redoStack.takeLast();
    this->segments = futureState.segments;
    this->overlays = futureState.overlays;

    if (selectedOverlayIdx >= overlays.size()) selectedOverlayIdx = -1;
    relayout();
    update();
    emit overlaysChanged();
    emit clipTrimmed();
}
