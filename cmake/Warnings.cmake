# bt_set_warnings(<target>): the warning set every ShapeLogic target builds with.
# Third-party code never goes through this function.
function(bt_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(BT_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic
      -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
      -Woverloaded-virtual -Wconversion -Wsign-conversion
      -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)
    if(BT_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
