# Gets libretro.h, which this package does not carry. It is the libretro API header of the RetroArch project (MIT licence,
# "Copyright (C) 2010-2024 The RetroArch team"), downloaded at configure time from a pinned commit and checked against a
# pinned SHA-256. A different file fails the configure.
#
# Sets LGP_LIBRETRO_INCLUDE_DIR to the folder holding libretro.h.
# Option LGP_LIBRETRO_HEADER: path to a local copy of that same file, for a build with no network. It is checked against
# the same hash.
set(LGP_LIBRETRO_COMMIT "39fd949a6d45586501251a94cf795c5bba974606")
set(LGP_LIBRETRO_SHA256 "469b0fbe2919ca9f5372b48a94ff8af51f70ee00c364798ec70670ecf6b7f9da")
set(LGP_LIBRETRO_URL
    "https://raw.githubusercontent.com/libretro/RetroArch/${LGP_LIBRETRO_COMMIT}/libretro-common/include/libretro.h")
set(LGP_LIBRETRO_HEADER "" CACHE FILEPATH "A local copy of libretro.h (RetroArch commit ${LGP_LIBRETRO_COMMIT}); empty downloads it")

set(LGP_LIBRETRO_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/libretro-include")
set(_lgp_header "${LGP_LIBRETRO_INCLUDE_DIR}/libretro.h")
if(LGP_LIBRETRO_HEADER)
  file(SHA256 "${LGP_LIBRETRO_HEADER}" _lgp_actual)
  if(NOT _lgp_actual STREQUAL LGP_LIBRETRO_SHA256)
    message(FATAL_ERROR "${LGP_LIBRETRO_HEADER} is not the pinned libretro.h (sha256 ${_lgp_actual}, wanted ${LGP_LIBRETRO_SHA256}).")
  endif()
  file(COPY "${LGP_LIBRETRO_HEADER}" DESTINATION "${LGP_LIBRETRO_INCLUDE_DIR}")
else()
  file(DOWNLOAD "${LGP_LIBRETRO_URL}" "${_lgp_header}" EXPECTED_HASH SHA256=${LGP_LIBRETRO_SHA256} STATUS _lgp_status)
  list(GET _lgp_status 0 _lgp_code)
  if(NOT _lgp_code EQUAL 0)
    file(REMOVE "${_lgp_header}")
    message(FATAL_ERROR "Could not get libretro.h from ${LGP_LIBRETRO_URL}: ${_lgp_status}\n"
                        "Check the network, or pass -DLGP_LIBRETRO_HEADER=<path to a copy of that file>.")
  endif()
endif()
message(STATUS "libretro.h at ${LGP_LIBRETRO_COMMIT} (sha256 checked)")
