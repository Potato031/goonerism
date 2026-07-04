cmake_minimum_required(VERSION 3.16)
project(SimpleVideoEditor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
        Widgets
        Multimedia
        MultimediaWidgets
        Concurrent
)

add_executable(PotatoEditor
        src/Main/main.cpp
        resources.qrc
        src/Includes/timelinewidget.h
        src/Main/timelinewidget.cpp
        src/Includes/titlebar.h
        src/Main/titlebar.cpp
        src/Main/audio.cpp
        src/Main/mouseEvents.cpp
        src/Main/notification.cpp
        src/Main/mediaLoader.cpp
        src/Main/keyboardEvents.cpp
        src/Main/export.cpp
        src/Main/undoredo.cpp
        src/Includes/mediaSource.h
        src/Includes/dropFilter.h
        src/Main/mainWindow.cpp
        src/Includes/resizeFilter.h
        src/Includes/mainWindow.h
        src/Main/updater.cpp
        src/Includes/previewLabel.h
        src/Main/previewLabel.cpp
        src/Includes/icons.h
        src/Includes/dragToolButton.h
        src/Includes/overlayShapes.h
)

if(WIN32)
    set_target_properties(PotatoEditor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(PotatoEditor
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::MultimediaWidgets
        Qt6::Concurrent
)