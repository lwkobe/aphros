function(add name)
  set(name ${name})
  add_test_current(NAME ${name} COMMAND test ${name})
endfunction()

add(periodic)
add(slipwall)
add(surfsymm)
add(surfwall)
add(wall)
add(inlet)
add(inletflux)
add(periodic_proj)
add(slipwall_proj)
add(surfsymm_proj)
add(surfwall_proj)
add(wall_proj)
add(inlet_proj)
add(inletflux_proj)
