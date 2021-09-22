cmake_minimum_required(VERSION 3.13)
project(zynthian-quick-components VERSION 0.1)
if (POLICY CMP0063)
    cmake_policy(SET CMP0063 NEW)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(QT_MIN_VERSION "5.11.0")
set(KF5_MIN_VERSION "5.54.0")

find_package(ECM ${KF5_MIN_VERSION} REQUIRED CONFIG)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${CMAKE_CURRENT_LIST_DIR}/cmake)

find_package(Qt5 ${QT_MIN_VERSION} REQUIRED CONFIG COMPONENTS Quick)
find_package(KF5Kirigami2 2.7.0)

include(KDEInstallDirs)
include(ECMInstallIcons)

add_subdirectory(src)
