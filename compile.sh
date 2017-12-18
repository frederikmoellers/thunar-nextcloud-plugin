#!/bin/bash

#gcc -shared -fPIC `pkg-config --cflags --libs exo-1 thunarx-2` -DG_ENABLE_DEBUG -o thunar-nextcloud-plugin.so thunar-nextcloud-plugin.c tnp-provider.c
gcc -shared -fPIC `pkg-config --cflags --libs exo-1 thunarx-2` -o thunar-nextcloud-plugin.so thunar-nextcloud-plugin.c tnp-provider.c
