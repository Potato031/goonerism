#include "../Includes/timelinewidget.h"
#include <QMenu>

void TimelineWidget::mousePressEvent(QMouseEvent* e) {
    if (durationMs <= 0 || segments.isEmpty()) return;
    setFocus();

    const int drawX = e->position().x() - sidebarWidth + scrollOffset;
    const double pxPerMs = static_cast<double>(width() - sidebarWidth) * zoomFactor / durationMs;
    const qint64 clickTime = static_cast<qint64>(drawX / pxPerMs);

    activeEdge = None;
    activeSegmentIdx = -1;
    isScrubbing = false;
    overlayDrag = OvNone;
    overlayDragIdx = -1;

    // --- Overlay lanes take priority: they sit above the video track ---
    {
        OverlayDragMode ovEdge;
        const int ovIdx = overlayIndexAt(e->pos(), &ovEdge);
        if (ovIdx != -1) {
            selectedOverlayIdx = ovIdx;

            if (e->button() == Qt::RightButton) {
                QMenu menu(this);
                menu.setObjectName("TimelineContextMenu");
                QAction *editAction = overlays[ovIdx].type == 3 ? menu.addAction("Edit text…") : nullptr;
                QAction *deleteAction = menu.addAction("Delete overlay");
                QAction *chosen = menu.exec(e->globalPosition().toPoint());
                if (chosen && chosen == deleteAction) deleteSelectedOverlay();
                else if (chosen && editAction && chosen == editAction) emit requestEditTextOverlay(ovIdx);
                update();
                return;
            }

            saveState();
            overlayDrag = ovEdge;
            overlayDragIdx = ovIdx;
            overlayDragGrabOffsetMs = clickTime - overlays[ovIdx].startMs;
            update();
            emit overlaysChanged();
            return;
        }
        if (selectedOverlayIdx != -1 && e->pos().y() < videoTrackTop()) {
            selectedOverlayIdx = -1;
            emit overlaysChanged();
        }
    }

    int clickedIdx = -1;
    for (int i = 0; i < segments.size(); ++i) {
        if (clickTime >= segments[i].startMs && clickTime <= segments[i].endMs) {
            clickedIdx = i;
            break;
        }
    }

    if (e->button() == Qt::RightButton) {
        if (clickedIdx != -1) {
            selectedSegmentIndices.clear();
            selectedSegmentIdx = clickedIdx;
            currentPosMs = qBound(0LL, clickTime, durationMs);
            emitVisualStateForCurrentContext();
            emit playheadMoved(currentPosMs);
            showClipContextMenu(e->globalPosition().toPoint(), clickTime, clickedIdx);
        }
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
        emitVisualStateForCurrentContext();
    } else {
        selectedSegmentIdx = -1;
        selectedSegmentIndices.clear();
    }

    update();
}

void TimelineWidget::showClipContextMenu(const QPoint &globalPos, qint64 clickTime, int clickedIdx) {
    QMenu menu(this);
    menu.setObjectName("TimelineContextMenu");

    QAction *splitAction = menu.addAction("Split clip here");
    menu.addSeparator();
    QAction *blurAction = menu.addAction("Add blur overlay here");
    QAction *pixelAction = menu.addAction("Add pixelate overlay here");
    QAction *blackoutAction = menu.addAction("Add blackout overlay here");
    QAction *textAction = menu.addAction("Add text overlay here");
    menu.addSeparator();
    QAction *applyClipAction = menu.addAction("Apply current crop to clip");
    QAction *applyAllAction = menu.addAction("Apply current crop to all clips");
    QAction *clearClipAction = menu.addAction("Clear clip crop");
    QAction *clearAllAction = menu.addAction("Clear all clip crops");

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == splitAction) {
        currentPosMs = qBound(0LL, clickTime, durationMs);
        saveState();
        splitAtPlayhead();
        return;
    }

    if (chosen == blurAction || chosen == pixelAction || chosen == blackoutAction || chosen == textAction) {
        const int type = chosen == blurAction ? 0 : chosen == pixelAction ? 1 : chosen == blackoutAction ? 2 : 3;
        addOverlayAt(type, qBound(0LL, clickTime, durationMs));
        return;
    }

    if (chosen == applyClipAction) {
        applyCurrentVisualsToSelection(false);
    } else if (chosen == applyAllAction) {
        applyCurrentVisualsToSelection(true);
    } else if (chosen == clearClipAction) {
        clearVisualsForSelection(false);
    } else if (chosen == clearAllAction) {
        clearVisualsForSelection(true);
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e) {
    const int drawX = e->position().x() - sidebarWidth + scrollOffset;
    const double pxPerMs = static_cast<double>(width() - sidebarWidth) * zoomFactor / durationMs;

    // --- Overlay clip drag: move / trim on its lane ---
    if (overlayDrag != OvNone && overlayDragIdx >= 0 && overlayDragIdx < overlays.size()) {
        auto &ov = overlays[overlayDragIdx];
        const qint64 mouseTime = qBound<qint64>(0, static_cast<qint64>(drawX / pxPerMs), durationMs);
        const qint64 minLen = 150;

        if (overlayDrag == OvMove) {
            const qint64 len = ov.endMs - ov.startMs;
            ov.startMs = qBound<qint64>(0, mouseTime - overlayDragGrabOffsetMs, durationMs - len);
            ov.endMs = ov.startMs + len;
        } else if (overlayDrag == OvStart) {
            ov.startMs = qBound<qint64>(0, mouseTime, ov.endMs - minLen);
        } else if (overlayDrag == OvEnd) {
            ov.endMs = qBound<qint64>(ov.startMs + minLen, mouseTime, durationMs);
        }
        update();
        emit overlaysChanged();
        return;
    }

    if (e->buttons() == Qt::NoButton && e->pos().y() < videoTrackTop()) {
        OverlayDragMode hoverEdge;
        if (overlayIndexAt(e->pos(), &hoverEdge) != -1) {
            setCursor(hoverEdge == OvMove ? Qt::OpenHandCursor : Qt::SizeHorCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        return;
    }

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
            emitVisualStateForCurrentContext();
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
        const qint64 minSegmentDuration = playbackSettings.minSegmentDurationMs;
        if (activeEdge == Start) {
            const qint64 minStart = (activeSegmentIdx > 0) ? segments[activeSegmentIdx-1].endMs : 0;
            segments[activeSegmentIdx].startMs = qBound(minStart, newTime, segments[activeSegmentIdx].endMs - minSegmentDuration);
        } else {
            const qint64 maxEnd = (activeSegmentIdx < segments.size() - 1) ? segments[activeSegmentIdx+1].startMs : durationMs;
            segments[activeSegmentIdx].endMs = qBound(segments[activeSegmentIdx].startMs + minSegmentDuration, newTime, maxEnd);
        }
        update();
    }
    else if (isScrubbing && (e->buttons() & Qt::LeftButton)) {
        currentPosMs = qBound(0LL, static_cast<qint64>(drawX / pxPerMs), durationMs);
        validatePlayheadPosition();
        emitVisualStateForCurrentContext();
        updateEditorVolume();
        emit playheadMoved(currentPosMs);
        update();
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (overlayDrag != OvNone) {
        overlayDrag = OvNone;
        overlayDragIdx = -1;
        emit overlaysChanged();
    }

    if (activeEdge != None) {
        saveState();
        emit clipTrimmed();
    }

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
        // Find what we are hovering over
        const int drawX = e->position().x() - sidebarWidth + scrollOffset;
        const double pxPerMs = static_cast<double>(viewWidth) * zoomFactor / durationMs;
        const qint64 hoverTime = static_cast<qint64>(drawX / pxPerMs);

        QSet<int> targets;
        int hoveredIdx = -1;
        for (int i = 0; i < segments.size(); ++i) {
            if (hoverTime >= segments[i].startMs && hoverTime <= segments[i].endMs) {
                hoveredIdx = i;
                break;
            }
        }

        if (hoveredIdx != -1) {
            if (selectedSegmentIndices.contains(hoveredIdx)) {
                targets = selectedSegmentIndices;
            } else {
                targets.insert(hoveredIdx);
            }
        }

        float delta = (e->angleDelta().y() > 0 ? 0.1f : -0.1f);
        for (int idx : targets) {
            segments[idx].gain = qBound(0.0f, segments[idx].gain + delta, 5.0f);
        }

        if(!targets.isEmpty()) emit audioGainChanged(audioGain); updateEditorVolume(); update();

    } else if (e->modifiers() & Qt::ControlModifier) {
        double oldZoom = zoomFactor;
        zoomFactor = qBound(1.0, zoomFactor * (e->angleDelta().y() > 0 ? 1.15 : 1.0/1.15), 100.0);
        int mouseX = e->position().x() - sidebarWidth;
        scrollOffset = (static_cast<double>(scrollOffset + mouseX) / oldZoom * zoomFactor) - mouseX;
        emit zoomChanged(zoomFactor);
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
