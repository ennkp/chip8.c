set(EXE_NAME "chip8")
message("Using ${CMAKE_C_COMPILER_ID}")
add_executable(${EXE_NAME})

target_sources(${EXE_NAME} PUBLIC
    main.c
)

if (WIN32)

    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
	target_compile_options(${EXE_NAME} PUBLIC
		-Wall -Wextra -Wpedantic -Werror
        )
    endif()

else ()

    message("Enabled ASAN - Linux")
	target_compile_options(${EXE_NAME} PUBLIC
		-Wall -Wextra -Wpedantic -Werror
        -fsanitize=address
        )
    target_link_options(${EXE_NAME} PRIVATE -fsanitize=address)

endif()
