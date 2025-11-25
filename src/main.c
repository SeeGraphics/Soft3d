#include "colors.h"
#include "shapes.h"
#include "types.h"
#include "utils.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
  u32 render_w;
  u32 render_h;
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Event event;
  SDL_Texture *texture;
  u32 *buffer;
  u32 pitch;
  bool quit;
} Game;

typedef struct {
  v2i mouse_pos;
  bool dragging;
  int drag_idx;
  v2i drag_offset;
  int r;
} MouseInteract;

int main(void) {
  Game game = {.render_w = 800, .render_h = 600};
  MouseInteract mouse = {{0}, .drag_idx = -1, .r = 8};

  // test quad
  v2i p1 = {100, 100};
  v2i p2 = {500, 100};
  v2i p3 = {100, 500};
  v2i verts[3] = {p1, p2, p3};

  v2i p4 = {500, 100};
  v2i p5 = {500, 500};
  v2i p6 = {100, 500};
  v2i verts1[3] = {p4, p5, p6};

  const char *title = "A: Hello Window";
  game.pitch = game.render_w * sizeof(u32);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    SDL_Log("Failed to Initialize SDL: %s\n", SDL_GetError());
  }

  game.window = SDL_CreateWindow(
      title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, game.render_w,
      game.render_h, SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_RESIZABLE);
  if (game.window == NULL) {
    SDL_Log("Failed to create Window: %s\n", SDL_GetError());
    SDL_Quit();
  }
  SDL_RaiseWindow(game.window); // immediately focus window

  game.renderer = SDL_CreateRenderer(game.window, -1, SDL_RENDERER_ACCELERATED);
  if (game.renderer == NULL) {
    SDL_Log("Failed to create Renderer: %s\n", SDL_GetError());
    SDL_DestroyWindow(game.window);
    SDL_Quit();
  }
  // what does this do?
  // SDL_RenderSetLogicalSize(game.renderer, game.render_w, game.render_h);

  game.texture = SDL_CreateTexture(game.renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, game.render_w,
                                   game.render_h);

  game.buffer = malloc(game.render_w * game.render_h * sizeof(u32));

  while (!game.quit) {
    while (SDL_PollEvent(&game.event)) {
      switch (game.event.type) {
      case SDL_QUIT:
        game.quit = true;
        break;
        // TODO: add mousevent that prints out coordinates, so testing is easier
        // TODO: also try and figure out how normalizing coordinates work
        // so that all coordinates are in -1.0 - 1.0 space
      case SDL_WINDOWEVENT:
        if (game.event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
          game.render_w = game.event.window.data1;
          game.render_h = game.event.window.data2;
          // reallocate & update
          buffer_reallocate(&game.buffer, game.render_w, game.render_h,
                            sizeof(u32));
          pitch_update(&game.pitch, game.render_w, sizeof(u32));
          texture_recreate(&game.texture, game.renderer, game.render_w,
                           game.render_h);
          for (int i = 0; i < 3; i++) {
            clamp_v2i(&verts[i], 0, game.render_w, 0, game.render_h, mouse.r);
          }
          // SDL_RenderSetLogicalSize(game.renderer, game.render_w,
          // game.render_h);
        }
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (game.event.button.button == SDL_BUTTON_LEFT) {
          mouse.mouse_pos = (v2i){game.event.motion.x, game.event.motion.y};
          // distance to each vertex
          int dx1 = mouse.mouse_pos.x - verts[0].x;
          int dy1 = mouse.mouse_pos.y - verts[0].y;
          int dx2 = mouse.mouse_pos.x - verts[1].x;
          int dy2 = mouse.mouse_pos.y - verts[1].y;
          int dx3 = mouse.mouse_pos.x - verts[2].x;
          int dy3 = mouse.mouse_pos.y - verts[2].y;

          // check each vertex for collision
          if ((dx1 * dx1 + dy1 * dy1) <= mouse.r * mouse.r) {
            mouse.dragging = true;
            mouse.drag_idx = 0;
            mouse.drag_offset = (v2i){verts[0].x - mouse.mouse_pos.x,
                                      verts[0].y - mouse.mouse_pos.y};
          }
          if ((dx2 * dx2 + dy2 * dy2) <= mouse.r * mouse.r) {
            mouse.dragging = true;
            mouse.drag_idx = 1;
            mouse.drag_offset = (v2i){verts[1].x - mouse.mouse_pos.x,
                                      verts[1].y - mouse.mouse_pos.y};
          }
          if ((dx3 * dx3 + dy3 * dy3) <= mouse.r * mouse.r) {
            mouse.dragging = true;
            mouse.drag_idx = 2;
            mouse.drag_offset = (v2i){verts[2].x - mouse.mouse_pos.x,
                                      verts[2].y - mouse.mouse_pos.y};
          }
        }
        break;
      case SDL_MOUSEMOTION:
        mouse.mouse_pos = (v2i){game.event.motion.x, game.event.motion.y};
        if (game.event.motion.state & SDL_BUTTON_LMASK) {
          if (mouse.dragging) {
            // update vertex pos to mouse_pos
            verts[mouse.drag_idx] =
                (v2i){mouse.mouse_pos.x + mouse.drag_offset.x,
                      mouse.mouse_pos.y + mouse.drag_offset.y};
            for (int i = 0; i < 3; i++) {
              clamp_v2i(&verts[i], 0, game.render_w, 0, game.render_h, mouse.r);
            }
          }
        }
        break;
      case SDL_MOUSEBUTTONUP:
        mouse.mouse_pos = (v2i){game.event.button.x, game.event.button.y};
        if (game.event.button.button == SDL_BUTTON_LEFT) {
          mouse.dragging = false;
          mouse.drag_idx = -1;
        }
        break;
      case SDL_KEYDOWN:
        if (game.event.key.keysym.sym == SDLK_ESCAPE) {
          game.quit = true;
          break;
        }
      default:
        break;
      }
    }

    // black background
    memset(game.buffer, 0, game.render_w * game.render_h * sizeof(u32));

    // draw
    draw_triangle_dots(game.buffer, game.render_w, game.render_h, verts[0],
                       verts[1], verts[2], WHITE, WIREFRAME);
    draw_triangle_dots(game.buffer, game.render_w, game.render_h, verts1[0],
                       verts1[1], verts1[2], WHITE, WIREFRAME);

    SDL_UpdateTexture(game.texture, NULL, game.buffer, game.pitch);
    SDL_RenderClear(game.renderer);
    SDL_RenderCopy(game.renderer, game.texture, NULL, NULL);
    SDL_RenderPresent(game.renderer);
  }
  free(game.buffer);
  SDL_DestroyTexture(game.texture);
  SDL_DestroyRenderer(game.renderer);
  SDL_DestroyWindow(game.window);
  SDL_Quit();

  return 0;
}
