#include "draw.h"

#include <cairo.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <locale.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <pango/pango-attributes.h>
#include <pango/pango-font.h>
#include <pango/pango-layout.h>
#include <pango/pango-types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/dunst.h"
#include "src/markup.h"
#include "src/notification.h"
#include "src/settings.h"
#include "src/utils.h"

const char *color_strings[3][3];

struct geometry geometry;

void draw_setup()
{
        x_parse_geometry(&geometry);

        color_strings[ColFG][LOW] = settings.lowfgcolor;
        color_strings[ColFG][NORM] = settings.normfgcolor;
        color_strings[ColFG][CRIT] = settings.critfgcolor;

        color_strings[ColBG][LOW] = settings.lowbgcolor;
        color_strings[ColBG][NORM] = settings.normbgcolor;
        color_strings[ColBG][CRIT] = settings.critbgcolor;

        if (settings.lowframecolor)
                color_strings[ColFrame][LOW] = settings.lowframecolor;
        else
                color_strings[ColFrame][LOW] = settings.frame_color;
        if (settings.normframecolor)
                color_strings[ColFrame][NORM] = settings.normframecolor;
        else
                color_strings[ColFrame][NORM] = settings.frame_color;
        if (settings.critframecolor)
                color_strings[ColFrame][CRIT] = settings.critframecolor;
        else
                color_strings[ColFrame][CRIT] = settings.frame_color;

        x_setup();
}

/* vim: set tabstop=8 shiftwidth=8 expandtab textwidth=0: */
