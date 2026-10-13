juce_add_console_app(studio_cli PRODUCT_NAME "studio-cli")
target_sources(studio_cli PRIVATE Main.cpp)
target_link_libraries(studio_cli PRIVATE studio_core)
