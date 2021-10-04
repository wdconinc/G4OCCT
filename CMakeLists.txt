cmake_minimum_required(VERSION 3.13)
project(G4OCCT LANGUAGES C CXX)

#----------------------------------------------------------------------------
# Find Geant4 package, activating all available Vis drivers by default
#
find_package(Geant4 REQUIRED vis_all)

#----------------------------------------------------------------------------
# Find OpenCascade package
#
find_package(OpenCASCADE COMPONENTS FoundationClasses REQUIRED)

#----------------------------------------------------------------------------
# Locate sources and headers for this project
#
file(GLOB sources ${PROJECT_SOURCE_DIR}/src/*.cc)
file(GLOB headers ${PROJECT_SOURCE_DIR}/include/*.hh)

#----------------------------------------------------------------------------
# Add the library
#
set(libname lib${PROJECT_NAME})
add_library(${libname}
  ${sources}
)
target_compile_features(${libname}
  PUBLIC cxx_auto_type
  PUBLIC cxx_trailing_return_types
  PUBLIC cxx_std_17
  PRIVATE cxx_variadic_templates
)
target_compile_options(${libname}
  PRIVATE 
  -Wno-extra
  -Wno-ignored-qualifiers
  -Wno-overloaded-virtual
  -Wno-shadow
)
install(
  TARGETS ${libname}
  DESTINATION lib
)

#----------------------------------------------------------------------------
# Add the examples
#
file(GLOB examples ${PROJECT_SOURCE_DIR}/examples/*)
foreach(example ${examples})
  file(GLOB sources ${example}/src/*.cc)
  file(GLOB headers ${example}/include/*.hh)
  add_executable(${example}
    ${example}.cc ${sources}
  )
  target_include_directories(${example}
    PUBLIC $<BUILD_INTERFACE:${CURRENT_SOURCE_DIR}/examples/${example}include>
    PUBLIC ${OpenCASCADE_INCLUDE_DIR}
    PUBLIC ${Geant4_INCLUDE_DIR}
  )
  target_link_directories(${example}
    PUBLIC ${OpenCASCADE_LIBRARY_DIR}
  )
  target_link_libraries(${example}
    PUBLIC ${OpenCASCADE_LIBRARIES}
    PUBLIC ${Geant4_LIBRARIES}
  )
  install(
    TARGETS ${example}
    DESTINATION bin
  )
endforeach()
