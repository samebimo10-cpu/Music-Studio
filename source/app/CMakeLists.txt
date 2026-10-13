juce_add_gui_app(studio_app
    PRODUCT_NAME "Studio"
    COMPANY_NAME "Studio"
    BUNDLE_ID "app.studio.desktop"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Studio records your voice and instruments."
    DOCUMENT_EXTENSIONS studio)

file(GLOB_RECURSE STUDIO_APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
target_sources(studio_app PRIVATE ${STUDIO_APP_SOURCES})
target_include_directories(studio_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_app PRIVATE studio_core)
