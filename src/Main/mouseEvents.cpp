#include "../Includes/timelinewidget.h"

void TimelineWidget::mousePressEvent(QMouseEvent* e) {
    if (durationMs <= 0 || segments.isEmpty()) return;

    const int drawX = e->position().x() - sidebarWidth + scrollOffset;
    const double pxPerMs = static_cast<double>(width() - sidebarWidth) * zoomFactor / durationMs;
    const qint64 clickTime = static_cast<qint64>(drawX / pxPerMs);

    activeEdge = None;
    activeSegmentIdx = -1;
    isScrubbing = false;

    int clickedIdx = -1;
    for (int i = 0; i < segments.size(); ++i) {
        if (clickTime >= segments[i].startMs && clickTime <= segments[i].endMs) {
            clickedIdx = i;
            break;
        }
    }

    if (e->button() == Qt::RightButton) {
        isSelecting = true;
        selectionRect.setTopLeft(e->pos());
        selectionRect.setBottomRight(e->pos());

        preSelectSnapshot = selectedSegmentIndices;
        if (selectedSegmentIdx != -1) preSelectSnapshot.insert(selectedSegmentIdx);

        update();
        return;
    }

    if (e->modifiers() & Qt::ShiftModifier && e->button() == Qt::LeftButton) {
        if (e->pos().x() > sidebarWidth) {
            currentPosMs = qBound(0LL, clickTime, durationMs);
            emit playheadMoved(currentPosMs);
            update();
            return;
        }
    }

    for (int i = 0; i < segments.size(); ++i) {
        if (std::abs(drawX - static_cast<int>(segments[i].startMs * pxPerMs)) < 12) {
            activeEdge = Start; activeSegmentIdx = i; selectedSegmentIdx = i;
            update(); return;
        } else if (std::abs(drawX - static_cast<int>(segments[i].endMs * pxPerMs)) < 12) {
            activeEdge = End; activeSegmentIdx = i; selectedSegmentIdx = i;
            update(); return;
        }
    }

    if (clickedIdx != -1) {
        if (e->modifiers() & Qt::ControlModifier) {
            if (selectedSegmentIndices.contains(clickedIdx)) selectedSegmentIndices.remove(clickedIdx);
            else selectedSegmentIndices.insert(clickedIdx);
            selectedSegmentIdx = -1;
        } else {
            selectedSegmentIndices.clear();
            selectedSegmentIdx = clickedIdx;
        }
    } else {
        selectedSegmentIdx = -1;
        selectedSegmentIndices.clear();
    }

    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e) {
    const int drawX = e->position().x() - sidebarWidth + scrollOffset;
    const double pxPerMs = static_cast<double>(width() - sidebarWidth) * zoomFactor / durationMs;

    if (isSelecting) {
        selectionRect.setBottomRight(e->pos());

        QSet<int> currentBatch = (e->modifiers() & Qt::ControlModifier) ? preSelectSnapshot : QSet<int>();

        const int selStart = qMin(selectionRect.left(), selectionRect.right()) - sidebarWidth + scrollOffset;
        const int selEnd = qMax(selectionRect.left(), selectionRect.right()) - sidebarWidth + scrollOffset;

        for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
            const int clipLeft = segments[i].startMs * pxPerMs;

            if (const int clipRight = segments[i].endMs * pxPerMs; clipRight > selStart && clipLeft < selEnd) {
                if (e->modifiers() & Qt::ControlModifier) {
                    if (preSelectSnapshot.contains(i)) currentBatch.remove(i);
                    else currentBatch.insert(i);
                } else {
                    currentBatch.insert(i);
                }
            }
        }

        selectedSegmentIndices = currentBatch;
        selectedSegmentIdx = -1;
        update();
        return;
    }


    if (e->modifiers() & Qt::ShiftModifier && e->buttons() & Qt::LeftButton) {
        if (e->pos().x() > sidebarWidth) {
            const int viewWidth = width() - sidebarWidth;
            const int contentWidth = viewWidth * zoomFactor;
            const double relativeX = e->pos().x() - sidebarWidth + scrollOffset;

            currentPosMs = qBound(0LL, static_cast<qint64>((relativeX / static_cast<double>(contentWidth)) * durationMs), durationMs);
            emit playheadMoved(currentPosMs);
            update();
            return;
        }
    }

    if (e->buttons() == Qt::NoButton) {
        const bool isModKeyPressed = (e->modifiers() & Qt::ControlModifier) ||
                               (e->modifiers() & Qt::ShiftModifier);

        if (isModKeyPressed) {
            setCursor(Qt::ArrowCursor);
        } else {
            bool onEdge = false;
            for (const auto& seg : segments) {
                if (std::abs(drawX - static_cast<int>(seg.startMs * pxPerMs)) < 12 ||
                    std::abs(drawX - static_cast<int>(seg.endMs * pxPerMs)) < 12) {
                    onEdge = true;
                    break;
                    }
            }

            if (onEdge) {
                setCursor(Qt::SizeHorCursor);
            } else if (std::abs(drawX - static_cast<int>(currentPosMs * pxPerMs)) < 10) {
                setCursor(Qt::SplitHCursor);
            } else {
                setCursor(Qt::ArrowCursor);
            }
        }
        return;
    }

    if (activeEdge != None && activeSegmentIdx != -1) {
        const qint64 newTime = qBound(0LL, static_cast<qint64>(drawX / pxPerMs), durationMs);
        if (activeEdge == Start) {
            const qint64 minStart = (activeSegmentIdx > 0) ? segments[activeSegmentIdx-1].endMs : 0;
            segments[activeSegmentIdx].startMs = qBound(minStart, newTime, segments[activeSegmentIdx].endMs - 100);
        } else {
            const qint64 maxEnd = (activeSegmentIdx < segments.size() - 1) ? segments[activeSegmentIdx+1].startMs : durationMs;
            segments[activeSegmentIdx].endMs = qBound(segments[activeSegmentIdx].startMs + 100, newTime, maxEnd);
        }
        update();
    }
    else if (isScrubbing && (e->buttons() & Qt::LeftButton)) {
        currentPosMs = qBound(0LL, static_cast<qint64>(drawX / pxPerMs), durationMs);
        validatePlayheadPosition();
        emit playheadMoved(currentPosMs);
        update();
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (activeEdge != None) saveState();

    if (e->button() == Qt::RightButton) {
        isSelecting = false;
        update();
    }

    activeEdge = None;
    activeSegmentIdx = -1;
    isScrubbing = false;
    unsetCursor();
}

void TimelineWidget::wheelEvent(QWheelEvent *e) {
    const int viewWidth = width() - sidebarWidth;
    if (e->modifiers() & Qt::ShiftModifier) {
        audioGain = qBound(0.0f, audioGain + (e->angleDelta().y() > 0 ? 0.1f : -0.1f), 5.0f);
        emit audioGainChanged(audioGain);
    } else if (e->modifiers() & Qt::ControlModifier) {
        double oldZoom = zoomFactor;
        zoomFactor = qBound(1.0, zoomFactor * (e->angleDelta().y() > 0 ? 1.15 : 1.0/1.15), 100.0);
        int mouseX = e->position().x() - sidebarWidth;
        scrollOffset = (static_cast<double>(scrollOffset + mouseX) / oldZoom * zoomFactor) - mouseX;
    } else {
        scrollOffset += (e->angleDelta().y() > 0 ? -120 : 120);
    }
    scrollOffset = qBound(0, scrollOffset, qMax(0, static_cast<int>(viewWidth * zoomFactor) - viewWidth));
    update();
}

void TimelineWidget::leaveEvent(QEvent *event) {
    unsetCursor();
    QWidget::leaveEvent(event);
}