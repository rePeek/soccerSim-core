# football::collision — generic continuous collision queries (swept sphere vs
# plane/sphere/capsule). Depends only on geometry + foundation; ball/player
# keep their own material and response models on top of it.
add_library(football_collision STATIC
  sweep.cpp)
add_library(football::collision ALIAS football_collision)
target_compile_features(football_collision PUBLIC cxx_std_23)
target_link_libraries(football_collision PUBLIC football::geometry)
football_configure_target(football_collision)
