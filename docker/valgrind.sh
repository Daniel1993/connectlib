#!/bin/bash

valgrind --vgdb=yes --vgdb-error=0 --track-origins=yes --leak-check=full /usr/connectlib/test &
sleep 1s
gdb /usr/connectlib/test
