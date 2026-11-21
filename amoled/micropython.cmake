# Create an INTERFACE library for our C module.
add_library(usermod_amoled INTERFACE)

# Add our source files to the lib
target_sources(usermod_amoled INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/amoled.c
    ${CMAKE_CURRENT_LIST_DIR}/amoled_qspi_bus.c
    ${CMAKE_CURRENT_LIST_DIR}/mpfile/mpfile.c
    ${CMAKE_CURRENT_LIST_DIR}/jpg/tjpgd565.c
    ${CMAKE_CURRENT_LIST_DIR}/schrift/schrift.c
    )

# Add the current directory as an include directory.
target_include_directories(usermod_amoled INTERFACE
    ${IDF_PATH}/components/esp_lcd/include/
    ${CMAKE_CURRENT_LIST_DIR}
    )

# Link our INTERFACE library to the usermod target.
target_link_libraries(usermod INTERFACE usermod_amoled)
