# nlohmann/json is the coach tool-call JSON boundary. It is header-only and
# pinned for reproducible tool schema/argument parsing. It never leaks into a
# public simcore header: only football_ai's private sources and the AI unit
# tests include it.
CPMAddPackage(NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  GIT_TAG v3.11.3
  OPTIONS "JSON_BuildTests OFF")
