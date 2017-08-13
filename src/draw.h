#ifndef DUNST_DRAW_H
#define DUNST_DRAW_H

#include <stdbool.h>

/*
 * The geometry struct holds all the information needed relevant to the
 * notification window position and dimensions.
 */
struct geometry {
        int width;
        int height;
        int x;
        int y;
        bool dynamic_width;
        bool negative_width;
};

void draw_setup();
#endif
/* vim: set tabstop=8 shiftwidth=8 expandtab textwidth=0: */
