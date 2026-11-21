# football::collision — generic continuous collision queries (swept sphere vs
# plane, half-space, sphere or capsule). Depends only on geometry + foundation;
# the ball keeps its own material and response model on top of it.
add_library(football_collision STATIC
  sweep.cpp)
add_library(football::collision ALIAS football_collision)
target_compile_features(football_collision PUBLIC cxx_std_23)
target_link_libraries(football_collision PUBLIC football::geometry)
football_configure_target(football_collision)
