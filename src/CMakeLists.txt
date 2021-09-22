add_library(zynthian-quick-plugin SHARED)
target_sources(zynthian-quick-plugin
    PRIVATE
    qmlplugin.cpp
    #resources.qrc
)
target_link_libraries(zynthian-quick-plugin Qt5::Core Qt5::Qml ${X11_LIBRARIES} ${LIBXDO_LIBRARIES})
target_include_directories(zynthian-quick-plugin PRIVATE ${X11_INCLUDE_DIR} ${LIBXDO_INCLUDE_DIRS})

install(TARGETS zynthian-quick-plugin DESTINATION ${QML_INSTALL_DIR}/org/zynthian/quick)
install(FILES qmldir DESTINATION ${QML_INSTALL_DIR}/org/zynthian/quick)
