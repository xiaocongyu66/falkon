set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

# Headless build: only the GreaseMonkey user-script plugin is compiled.
add_subdirectory(GreaseMonkey)
