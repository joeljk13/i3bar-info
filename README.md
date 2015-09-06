i3bar-info
==========

A replacement for i3status that's fast and easily extendable, written in C.

Modifying
---------

The code knows what sections to use via the `sections` variable in
`print_add_data()`. Each section consists of 2 functions:
 * The first should take a `struct section_data *` and fill in whatever data
   it wants (see `i3bar` documentation for what the sections mean). It should
   return 0 on success, and -1 on failure.
 * The second should take the same `struct section_data *` and free any data
   that could have been allocated in the first function. Remember that `free()`
   does nothing when given a NULL pointer, so for the most part this function
   will probably just be some calls to `free()`.
