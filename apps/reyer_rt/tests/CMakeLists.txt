# Enable testing for this directory
enable_testing()

# Create test executable
add_executable(test_variant_json test_variant_json.cpp)

# Link necessary libraries
target_link_libraries(test_variant_json
    PRIVATE
    reyer
    glaze::glaze
)

# Include necessary directories
target_include_directories(test_variant_json
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/
)

# Register with CTest
add_test(NAME VariantJsonSerialization COMMAND test_variant_json)

# Test for variant auto-deduction
add_executable(test_json_auto_deduction test_json_auto_deduction.cpp)
target_link_libraries(test_json_auto_deduction
    PRIVATE
    reyer
    glaze::glaze
)
target_include_directories(test_json_auto_deduction
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/
)
add_test(NAME JsonAutoDeduction COMMAND test_json_auto_deduction)
