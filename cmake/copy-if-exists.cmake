# Copy a directory only when the source exists (POST_BUILD helper).
# Usage: cmake -DCOPY_FROM=<src> -DCOPY_TO=<dst> -P copy-if-exists.cmake
if(EXISTS "${COPY_FROM}")
  file(COPY "${COPY_FROM}/" DESTINATION "${COPY_TO}")
endif()
