#include "../Includes/timelinewidget.h"

void TimelineWidget::saveState() {
    TimelineState currentState;
    currentState.segments = this->segments;

    undoStack.append(currentState);
    if (undoStack.size() > MAX_STACK_SIZE) undoStack.removeFirst();

    redoStack.clear();
}

void TimelineWidget::undo() {
    if (undoStack.isEmpty()) return;

    TimelineState currentState;
    currentState.segments = this->segments;
    redoStack.append(currentState);

    TimelineState previousState = undoStack.takeLast();
    this->segments = previousState.segments;

    selectedSegmentIndices.clear();
    selectedSegmentIdx = -1;
    update();
}

void TimelineWidget::redo() {
    if (redoStack.isEmpty()) return;

    TimelineState currentState;
    currentState.segments = this->segments;
    undoStack.append(currentState);

    TimelineState futureState = redoStack.takeLast();
    this->segments = futureState.segments;

    update();
}