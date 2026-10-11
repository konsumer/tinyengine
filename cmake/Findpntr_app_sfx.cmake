# pntr_app_sfx is a single header with no build of its own, so fetching it is enough.
FetchContent_Declare(pntr_app_sfx
  URL https://github.com/konsumer/pntr_app_sfx/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(pntr_app_sfx)
