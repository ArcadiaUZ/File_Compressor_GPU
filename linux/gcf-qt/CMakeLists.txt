cmake_minimum_required(VERSION 3.18)
project(gcf-qt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt5 yoki Qt6 — qaysi topilsa shu (Ubuntu 22.04: Qt5, 24.04: Qt5/Qt6)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(SOURCES
  main.cpp
  backend.cpp
  mainwindow.cpp
  mainwindow2.cpp
  mainwindow3.cpp
  archivewindow.cpp
  archivewindow2.cpp
)

add_executable(gcf-qt ${SOURCES})
target_link_libraries(gcf-qt PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_compile_options(gcf-qt PRIVATE -O2 -Wall)

install(TARGETS gcf-qt DESTINATION bin)
