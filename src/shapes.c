#include "shapes.h"
#include "colors.h"
#include "render.h"
#include "types.h"
#include "utils.h"
#include <math.h>

void draw_triangle(u32 *buffer, int w, int h, v2i p1, v2i p2, v2i p3, u32 color,
                   u32 mode) {
  if (mode == WIREFRAME) {
    draw_linei(buffer, w, p1, p2, color);
    draw_linei(buffer, w, p2, p3, color);
    draw_linei(buffer, w, p3, p1, color);
  }
  if (mode == FILLED) {
    sort_by_y(&p1, &p2, &p3);
    if (p1.y == p3.y) {
      return;
    }
    int y_start = p1.y;
    int y_end = p3.y;
    for (int y = y_start; y < y_end; y++) {
      // which edges are active
      float xa, xb;
      if (y < p2.y) {
        // upper part (p1 -> p3 && p1 -> p2)
        xa = p1.x + (float)(p3.x - p1.x) * (y - p1.y) / (float)(p3.y - p1.y);
        xb = p1.x + (float)(p2.x - p1.x) * (y - p1.y) / (float)(p2.y - p1.y);
      } else {
        // lower part (p1 -> p3 && p2 -> p3)
        xa = p1.x + (float)(p3.x - p1.x) * (y - p1.y) / (float)(p3.y - p1.y);
        xb = p2.x + (float)(p3.x - p2.x) * (y - p2.y) / (float)(p3.y - p2.y);
      }
      if (xa > xb) {
        float t = xa;
        xa = xb;
        xb = t;
      }
      int xl = (int)ceilf(xa);
      int xr = (int)floorf(xb);
      // clamp to buffer bounds
      if (xl < 0)
        xl = 0;
      if (xr >= w)
        xr = w - 1;
      if (y < 0 || y >= h)
        continue;

      // fill out
      for (int x = xl; x <= xr; x++)
        set_pixel(buffer, w, (v2i){x, y}, color);
    }
  }
}

void draw_triangle_dots(u32 *buffer, int w, int h, v2i p1, v2i p2, v2i p3,
                        u32 color, u32 mode) {
  if (mode == WIREFRAME) {
    draw_linei(buffer, w, p1, p2, color);
    draw_linei(buffer, w, p2, p3, color);
    draw_linei(buffer, w, p3, p1, color);
    draw_cirlcei(buffer, w, p1, 5, RED);
    draw_cirlcei(buffer, w, p2, 5, RED);
    draw_cirlcei(buffer, w, p3, 5, RED);
  }
  if (mode == FILLED) {
    sort_by_y(&p1, &p2, &p3);
    if (p1.y == p3.y) {
      return;
    }
    int y_start = p1.y;
    int y_end = p3.y;
    for (int y = y_start; y < y_end; y++) {
      // which edges are active
      float xa, xb;
      if (y < p2.y) {
        // upper part (p1 -> p3 && p1 -> p2)
        xa = p1.x + (float)(p3.x - p1.x) * (y - p1.y) / (float)(p3.y - p1.y);
        xb = p1.x + (float)(p2.x - p1.x) * (y - p1.y) / (float)(p2.y - p1.y);
      } else {
        // lower part (p1 -> p3 && p2 -> p3)
        xa = p1.x + (float)(p3.x - p1.x) * (y - p1.y) / (float)(p3.y - p1.y);
        xb = p2.x + (float)(p3.x - p2.x) * (y - p2.y) / (float)(p3.y - p2.y);
      }
      if (xa > xb) {
        float t = xa;
        xa = xb;
        xb = t;
      }
      int xl = (int)ceilf(xa);
      int xr = (int)floorf(xb);
      // clamp to buffer bounds
      if (xl < 0)
        xl = 0;
      if (xr >= w)
        xr = w - 1;
      if (y < 0 || y >= h)
        continue;

      // fill out
      for (int x = xl; x <= xr; x++)
        set_pixel(buffer, w, (v2i){x, y}, color);
    }
    draw_cirlcei(buffer, w, p1, 5, RED);
    draw_cirlcei(buffer, w, p2, 5, RED);
    draw_cirlcei(buffer, w, p3, 5, RED);
  }
}

void draw_cirlcei(u32 *buffer, int w, v2i pos, int r, u32 color) {
  int x = 0;
  int y = -r;
  int d = -r;

  while (x < -y) {
    if (d > 0) {
      y += 1;
      d += 2 * (x + y) + 1;
    } else {
      d += 2 * x + 1;
    }
    set_pixel(buffer, w, (v2i){pos.x + x, pos.y + y}, color);
    set_pixel(buffer, w, (v2i){pos.x - x, pos.y + y}, color);
    set_pixel(buffer, w, (v2i){pos.x + x, pos.y - y}, color);
    set_pixel(buffer, w, (v2i){pos.x - x, pos.y - y}, color);
    set_pixel(buffer, w, (v2i){pos.x + y, pos.y + x}, color);
    set_pixel(buffer, w, (v2i){pos.x + y, pos.y - x}, color);
    set_pixel(buffer, w, (v2i){pos.x - y, pos.y + x}, color);
    set_pixel(buffer, w, (v2i){pos.x - y, pos.y - x}, color);

    x += 1;
  }
}
