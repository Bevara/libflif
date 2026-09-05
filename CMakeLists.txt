cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBFLIF_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_flif.c
)

SET(LIBFLIF_LIBS
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libflif_dec.a
)

SET(LIBFLIF_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libflif
        "${LIBFLIF_SRC}"
        "${LIBFLIF_LIBS}"
        []
        ""
        "${LIBFLIF_INC}"
        ""
        "1")
