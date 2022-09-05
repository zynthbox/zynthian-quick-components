add_library(zynthian-quick-plugin SHARED)
target_sources(zynthian-quick-plugin
    PRIVATE
    qmlplugin.cpp
    FilterProxy.cpp
    Note.cpp
    NotesModel.cpp
    MidiRecorder.cpp
    PatternImageProvider.cpp
    PatternModel.cpp
    PlayGrid.cpp
    PlayGridManager.cpp
    SegmentHandler.cpp
    SequenceModel.cpp
    SettingsContainer.cpp

    #resources.qrc
)
target_link_libraries(zynthian-quick-plugin Qt5::Core Qt5::Qml Qt5::Quick ${LIBZL_LIBRARIES} ${RTMIDI_LIBRARIES})
target_include_directories(zynthian-quick-plugin PRIVATE ${LIBZL_INCLUDE_DIRS} ${RTMIDI_INCLUDE_DIRS})

install(TARGETS zynthian-quick-plugin DESTINATION ${QML_INSTALL_DIR}/org/zynthian/quick)
install(FILES qmldir DESTINATION ${QML_INSTALL_DIR}/org/zynthian/quick)
