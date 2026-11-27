# SL_SANITIZE (e.g. "address,undefined" or "thread") applies to every target
# configured after this point, third-party code included, as sanitizers need.
if(SL_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "SL_SANITIZE is not supported with MSVC")
  endif()
  if(SL_SANITIZE MATCHES "thread" AND SL_SANITIZE MATCHES "address")
    message(FATAL_ERROR "ThreadSanitizer and AddressSanitizer cannot be combined")
  endif()
  add_compile_options(-fsanitize=${SL_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${SL_SANITIZE})
endif()
