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

typedef struct _color_t {
        double r;
        double g;
        double b;
} color_t;

void draw_setup();
void draw_free();
void draw();

const struct geometry *draw_get_geometry();

extern const char *color_strings[3][3];
#endif
/* vim: set tabstop=8 shiftwidth=8 expandtab textwidth=0: */
