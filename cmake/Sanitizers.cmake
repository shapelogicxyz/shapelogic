# BT_SANITIZE (e.g. "address,undefined" or "thread") applies to every target
# configured after this point, third-party code included, as sanitizers need.
if(BT_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "BT_SANITIZE is not supported with MSVC")
  endif()
  if(BT_SANITIZE MATCHES "thread" AND BT_SANITIZE MATCHES "address")
    message(FATAL_ERROR "ThreadSanitizer and AddressSanitizer cannot be combined")
  endif()
  add_compile_options(-fsanitize=${BT_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${BT_SANITIZE})
endif()
