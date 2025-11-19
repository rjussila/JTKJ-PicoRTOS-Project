# Remember to uncomment in the root CMakeLists.txt the corresponding add_subdirectory if you want to include this application in your project


set(DEFAULT_TARGET hello_hat)
add_executable(${MAIN_TARGET}
  ${CMAKE_CURRENT_LIST_DIR}/src/main.c
)


target_link_libraries(${MAIN_TARGET}} PRIVATE
  pico_stdlib
  FreeRTOS-Kernel
  FreeRTOS-Kernel-Heap4
  TKJHAT_SDK
)

pico_enable_stdio_usb(${MAIN_TARGET}} 1)
pico_enable_stdio_uart(${MAIN_TARGET}} 0)

pico_add_extra_outputs(${MAIN_TARGET}})
