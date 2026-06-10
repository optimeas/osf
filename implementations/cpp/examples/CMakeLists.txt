# Example executables for the OSF C++ library.
#
# Enabled by -D OSF_BUILD_EXAMPLES=ON (default ON in the top-level
# CMakeLists.txt).  Each example:
#   - links osf::osf (the umbrella target that pulls in every public header)
#   - uses the same compiler-warning helper as the rest of the tree
#   - is C++17; the standard is inherited from CMAKE_CXX_STANDARD set in
#     the top-level CMakeLists.txt

foreach(example inspect dump write copy)
    add_executable(osf_example_${example} ${example}.cpp)
    target_link_libraries(osf_example_${example} PRIVATE osf::osf)
    osf_set_warnings(osf_example_${example})
    set_target_properties(osf_example_${example} PROPERTIES
        OUTPUT_NAME ${example}
    )
endforeach()
