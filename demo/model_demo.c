#include "colors.h"
#include "math.h"
#include "obj_loader.h"
#include "render.h"
#include "shapes.h"
#include "text.h"
#include "types.h"
#include "utils.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

typedef struct {
  u32 window_w;
  u32 window_h;
  u32 render_w;
  u32 render_h;
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Event event;
  SDL_Texture *texture;
  u32 *buffer;
  float *depth;
  u32 pitch;
  bool mouse_grabbed;
} Game;

typedef struct {
  v3f pos;
  float yaw;
  float pitch;
} Camera;

typedef struct {
  Game game;
  Camera camera;
  ObjModel model;
  Texture fallback_tex;
  bool wireframe;
  float fps;
  Uint32 last_ticks;
  bool running;
  int render_scale;
  float near_plane;
  float mouse_sens;
  float model_scale;
  v3f model_center;
  v3f model_pos;
} ModelDemo;

typedef struct {
  v3f view_pos;
  v2f uv;
} ClipVert;

static bool project_vertex(const ClipVert *cv, const mat4 *proj, int render_w,
                           int render_h, VertexPC *out, int *mask_out) {
  v4f clip = mat4_mul_v4(
      *proj, (v4f){cv->view_pos.x, cv->view_pos.y, cv->view_pos.z, 1.0f});
  if (clip.w == 0.0f) {
    return false;
  }

  int mask = 0;
  if (clip.x < -clip.w)
    mask |= 1;
  if (clip.x > clip.w)
    mask |= 2;
  if (clip.y < -clip.w)
    mask |= 4;
  if (clip.y > clip.w)
    mask |= 8;
  if (clip.z < 0.0f)
    mask |= 16;
  if (clip.z > clip.w)
    mask |= 32;

  float inv_w = 1.0f / clip.w;
  v3f ndc = {clip.x * inv_w, clip.y * inv_w, clip.z * inv_w};
  out->pos = norm_to_screen((v2f){ndc.x, ndc.y}, render_w, render_h);
  out->uv = cv->uv;
  out->inv_w = inv_w;
  out->depth = 0.5f * (ndc.z + 1.0f);
  *mask_out = mask;
  return true;
}

static v3f camera_forward(const Camera *cam) {
  float cy = cosf(cam->yaw);
  float sy = sinf(cam->yaw);
  float cp = cosf(cam->pitch);
  float sp = sinf(cam->pitch);
  return v3_normalize((v3f){sy * cp, sp, -cy * cp});
}

static void clear_depth(float *depth, size_t count) {
  for (size_t i = 0; i < count; i++) {
    depth[i] = 1.0f;
  }
}

static void resize_render(Game *game, int window_w, int window_h,
                          int render_scale) {
  game->window_w = (u32)window_w;
  game->window_h = (u32)window_h;
  game->render_w = (u32)(window_w / render_scale);
  game->render_h = (u32)(window_h / render_scale);
  if (game->render_w == 0)
    game->render_w = 1;
  if (game->render_h == 0)
    game->render_h = 1;

  buffer_reallocate(&game->buffer, game->render_w, game->render_h,
                    sizeof(u32));
  if (game->depth) {
    free(game->depth);
  }
  game->depth = malloc(game->render_w * game->render_h * sizeof(float));
  pitch_update(&game->pitch, game->render_w, sizeof(u32));
  if (game->renderer) {
    texture_recreate(&game->texture, game->renderer, game->render_w,
                     game->render_h);
  }
}

static void destroy_texture(Texture *tex) {
  if (tex && tex->pixels) {
    texture_destroy(tex);
  }
}

static void make_fallback(Texture *tex, u32 color) {
  tex->w = 1;
  tex->h = 1;
  tex->pixels = malloc(sizeof(u32));
  if (tex->pixels) {
    tex->pixels[0] = color;
  }
}

static bool model_demo_init(ModelDemo *demo) {
  *demo = (ModelDemo){0};
  demo->game.window_w = 960;
  demo->game.window_h = 540;
  demo->render_scale = 2;
  demo->near_plane = 0.05f;
  demo->mouse_sens = 0.0025f;
  demo->camera = (Camera){.pos = {0.0f, 0.3f, 3.0f}, .yaw = 0.0f, .pitch = 0.0f};
  resize_render(&demo->game, (int)demo->game.window_w,
                (int)demo->game.window_h, demo->render_scale);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    SDL_Log("Failed to Initialize SDL: %s\n", SDL_GetError());
    return false;
  }

  if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP) &
        (IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP))) {
    SDL_Log("Failed to init SDL_image: %s\n", IMG_GetError());
    SDL_Quit();
    return false;
  }

  if (!obj_model_load("assets/backpack/backpack.obj", &demo->model)) {
    SDL_Log("Failed to load backpack model");
    IMG_Quit();
    SDL_Quit();
    return false;
  }

  make_fallback(&demo->fallback_tex, 0xFFFFFFFF);

  v3f size = {0};
  if (demo->model.has_bounds) {
    size = (v3f){demo->model.bounds_max.x - demo->model.bounds_min.x,
                 demo->model.bounds_max.y - demo->model.bounds_min.y,
                 demo->model.bounds_max.z - demo->model.bounds_min.z};
    demo->model_center = (v3f){(demo->model.bounds_min.x + demo->model.bounds_max.x) * 0.5f,
                               (demo->model.bounds_min.y + demo->model.bounds_max.y) * 0.5f,
                               (demo->model.bounds_min.z + demo->model.bounds_max.z) * 0.5f};
    float max_extent = fmaxf(size.x, fmaxf(size.y, size.z));
    demo->model_scale = (max_extent > 0.0f) ? (2.0f / max_extent) : 1.0f;
  } else {
    demo->model_center = (v3f){0};
    demo->model_scale = 1.0f;
  }
  demo->model_pos = (v3f){0.0f, -0.4f, 0.0f};

  const char *title = "Model Demo: Backpack";
  demo->game.window = SDL_CreateWindow(
      title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      (int)demo->game.window_w, (int)demo->game.window_h,
      SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_RESIZABLE);
  if (demo->game.window == NULL) {
    SDL_Log("Failed to create Window: %s\n", SDL_GetError());
    obj_model_free(&demo->model);
    destroy_texture(&demo->fallback_tex);
    IMG_Quit();
    SDL_Quit();
    return false;
  }
  SDL_RaiseWindow(demo->game.window);

  demo->game.renderer =
      SDL_CreateRenderer(demo->game.window, -1, SDL_RENDERER_ACCELERATED);
  if (demo->game.renderer == NULL) {
    SDL_Log("Failed to create Renderer: %s\n", SDL_GetError());
    SDL_DestroyWindow(demo->game.window);
    obj_model_free(&demo->model);
    destroy_texture(&demo->fallback_tex);
    IMG_Quit();
    SDL_Quit();
    return false;
  }

  texture_recreate(&demo->game.texture, demo->game.renderer,
                   demo->game.render_w, demo->game.render_h);
  SDL_SetRelativeMouseMode(SDL_TRUE);
  SDL_ShowCursor(SDL_DISABLE);
  demo->game.mouse_grabbed = true;
  demo->fps = 0.0f;
  demo->last_ticks = SDL_GetTicks();
  demo->running = true;
  return true;
}

static void model_demo_shutdown(ModelDemo *demo) {
  obj_model_free(&demo->model);
  destroy_texture(&demo->fallback_tex);
  if (demo->game.buffer) {
    free(demo->game.buffer);
    demo->game.buffer = NULL;
  }
  if (demo->game.depth) {
    free(demo->game.depth);
    demo->game.depth = NULL;
  }
  if (demo->game.texture) {
    SDL_DestroyTexture(demo->game.texture);
    demo->game.texture = NULL;
  }
  if (demo->game.renderer) {
    SDL_DestroyRenderer(demo->game.renderer);
    demo->game.renderer = NULL;
  }
  if (demo->game.window) {
    SDL_DestroyWindow(demo->game.window);
    demo->game.window = NULL;
  }
  IMG_Quit();
  SDL_Quit();
}

static void model_demo_handle_event(ModelDemo *demo, const SDL_Event *event) {
  Game *game = &demo->game;
  switch (event->type) {
  case SDL_QUIT:
    demo->running = false;
    break;
  case SDL_WINDOWEVENT:
    if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
      resize_render(&demo->game, event->window.data1, event->window.data2,
                    demo->render_scale);
    }
    break;
  case SDL_MOUSEMOTION:
    if (game->mouse_grabbed) {
      demo->camera.yaw += (float)event->motion.xrel * demo->mouse_sens;
      demo->camera.pitch -= (float)event->motion.yrel * demo->mouse_sens;
    }
    break;
  case SDL_KEYDOWN:
    if (event->key.keysym.sym == SDLK_ESCAPE) {
      demo->running = false;
    }
    if (event->key.keysym.sym == SDLK_r) {
      demo->wireframe = !demo->wireframe;
    }
    if (event->key.keysym.sym == SDLK_q) {
      game->mouse_grabbed = !game->mouse_grabbed;
      SDL_SetRelativeMouseMode(game->mouse_grabbed ? SDL_TRUE : SDL_FALSE);
      SDL_ShowCursor(game->mouse_grabbed ? SDL_DISABLE : SDL_ENABLE);
    }
    if (event->key.keysym.sym == SDLK_7) {
      Uint32 flags = SDL_GetWindowFlags(game->window);
      bool is_full = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
      if (SDL_SetWindowFullscreen(
              game->window, is_full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) == 0) {
        int w, h;
        SDL_GetWindowSize(game->window, &w, &h);
        resize_render(&demo->game, w, h, demo->render_scale);
      }
    }
    break;
  default:
    break;
  }
}

static void model_demo_frame(ModelDemo *demo, Uint32 now, float dt) {
  Game *game = &demo->game;
  const float world_up_y = 1.0f;

  const Uint8 *state = SDL_GetKeyboardState(NULL);
  v3f forward = camera_forward(&demo->camera);
  v3f world_up = {0.0f, world_up_y, 0.0f};
  v3f right = v3_normalize(v3_cross(forward, world_up));

  float move_speed = 2.5f * dt;
  if (state[SDL_SCANCODE_W]) {
    demo->camera.pos = v3_add(demo->camera.pos, v3_scale(forward, move_speed));
  }
  if (state[SDL_SCANCODE_S]) {
    demo->camera.pos = v3_sub(demo->camera.pos, v3_scale(forward, move_speed));
  }
  if (state[SDL_SCANCODE_A]) {
    demo->camera.pos = v3_sub(demo->camera.pos, v3_scale(right, move_speed));
  }
  if (state[SDL_SCANCODE_D]) {
    demo->camera.pos = v3_add(demo->camera.pos, v3_scale(right, move_speed));
  }
  if (state[SDL_SCANCODE_SPACE]) {
    demo->camera.pos.y += move_speed;
  }
  if (state[SDL_SCANCODE_LCTRL]) {
    demo->camera.pos.y -= move_speed;
  }

  float look_speed = 1.5f * dt;
  if (state[SDL_SCANCODE_LEFT]) {
    demo->camera.yaw -= look_speed;
  }
  if (state[SDL_SCANCODE_RIGHT]) {
    demo->camera.yaw += look_speed;
  }
  if (state[SDL_SCANCODE_UP]) {
    demo->camera.pitch += look_speed;
  }
  if (state[SDL_SCANCODE_DOWN]) {
    demo->camera.pitch -= look_speed;
  }
  float max_pitch = (float)M_PI_2 - 0.1f;
  if (demo->camera.pitch > max_pitch)
    demo->camera.pitch = max_pitch;
  if (demo->camera.pitch < -max_pitch)
    demo->camera.pitch = -max_pitch;

  memset(game->buffer, 0, game->render_w * game->render_h * sizeof(u32));
  clear_depth(game->depth, (size_t)game->render_w * (size_t)game->render_h);

  float aspect = (float)game->render_w / (float)game->render_h;
  mat4 view = mat4_look_at(
      demo->camera.pos,
      v3_add(demo->camera.pos, camera_forward(&demo->camera)), world_up);
  mat4 proj = mat4_perspective((float)M_PI / 3.0f, aspect, demo->near_plane,
                               100.0f);

  typedef struct {
    v2i screen;
    v2f uv;
    v3f view_pos;
    float inv_w;
    float depth;
    int clip_mask;
    bool depth_ok;
  } CachedVertex;
  CachedVertex tri[3];

  for (int i = 0; i < demo->model.face_count; i++) {
    ObjFace *face = &demo->model.faces[i];
    bool skip = false;
    for (int j = 0; j < 3; j++) {
      v3f local = v3_sub(face->v[j].pos, demo->model_center);
      local = v3_scale(local, demo->model_scale);
      v4f world = {local.x, local.y, local.z, 1.0f};

      world.x += demo->model_pos.x;
      world.y += demo->model_pos.y;
      world.z += demo->model_pos.z;

      v4f view_pos4 = mat4_mul_v4(view, world);
      v4f clip = mat4_mul_v4(proj, view_pos4);

      tri[j].uv = face->v[j].uv;
      tri[j].view_pos = (v3f){view_pos4.x, view_pos4.y, view_pos4.z};

      int mask = 0;
      if (clip.w == 0.0f) {
        tri[j].clip_mask = 0x3F;
        tri[j].depth_ok = false;
        skip = true;
        break;
      }
      if (clip.x < -clip.w)
        mask |= 1;
      if (clip.x > clip.w)
        mask |= 2;
      if (clip.y < -clip.w)
        mask |= 4;
      if (clip.y > clip.w)
        mask |= 8;
      if (clip.z < 0.0f)
        mask |= 16;
      if (clip.z > clip.w)
        mask |= 32;
      tri[j].clip_mask = mask;

      float inv_w = 1.0f / clip.w;
      tri[j].inv_w = inv_w;
      v3f ndc = {clip.x * inv_w, clip.y * inv_w, clip.z * inv_w};
      tri[j].depth_ok = ndc.z >= 0.0f && ndc.z <= 1.0f;
      tri[j].screen =
          norm_to_screen((v2f){ndc.x, ndc.y}, game->render_w, game->render_h);
      tri[j].depth = 0.5f * (ndc.z + 1.0f);
    }
    if (skip) {
      continue;
    }

    if ((tri[0].clip_mask & tri[1].clip_mask & tri[2].clip_mask) != 0) {
      continue;
    }

    bool near_in[3] = {tri[0].view_pos.z <= -demo->near_plane,
                       tri[1].view_pos.z <= -demo->near_plane,
                       tri[2].view_pos.z <= -demo->near_plane};
    bool needs_clip = !(near_in[0] && near_in[1] && near_in[2]);

    Texture *tex =
        (face->mat && face->mat->has_diffuse) ? &face->mat->diffuse
                                              : &demo->fallback_tex;

    if (!needs_clip) {
      if (!tri[0].depth_ok || !tri[1].depth_ok || !tri[2].depth_ok) {
        continue;
      }
      v3f edge1 = v3_sub(tri[1].view_pos, tri[0].view_pos);
      v3f edge2 = v3_sub(tri[2].view_pos, tri[0].view_pos);
      v3f normal = v3_cross(edge1, edge2);
      if (v3_dot(normal, tri[0].view_pos) >= 0.0f) {
        continue;
      }
      VertexPC pv[3] = {
          {.pos = tri[0].screen,
           .uv = tri[0].uv,
           .inv_w = tri[0].inv_w,
           .depth = tri[0].depth},
          {.pos = tri[1].screen,
           .uv = tri[1].uv,
           .inv_w = tri[1].inv_w,
           .depth = tri[1].depth},
          {.pos = tri[2].screen,
           .uv = tri[2].uv,
           .inv_w = tri[2].inv_w,
           .depth = tri[2].depth},
      };

      if (demo->wireframe) {
        draw_triangle(game->buffer, game->render_w, game->render_h, pv[0].pos,
                      pv[1].pos, pv[2].pos, WHITE, WIREFRAME);
      } else {
        draw_textured_triangle(game->buffer, game->depth, game->render_w,
                               game->render_h, tex, pv[0], pv[1], pv[2]);
      }
    } else {
      ClipVert in_poly[4] = {
          {.view_pos = tri[0].view_pos, .uv = tri[0].uv},
          {.view_pos = tri[1].view_pos, .uv = tri[1].uv},
          {.view_pos = tri[2].view_pos, .uv = tri[2].uv},
      };
      int in_count = 3;
      ClipVert out_poly[4];
      int out_count = 0;

      for (int v = 0; v < in_count; v++) {
        ClipVert a = in_poly[v];
        ClipVert b = in_poly[(v + 1) % in_count];
        bool a_in = a.view_pos.z <= -demo->near_plane;
        bool b_in = b.view_pos.z <= -demo->near_plane;

        if (a_in && b_in) {
          out_poly[out_count++] = b;
        } else if (a_in && !b_in) {
          float t = (-demo->near_plane - a.view_pos.z) /
                    (b.view_pos.z - a.view_pos.z);
          ClipVert inter = {
              .view_pos = {a.view_pos.x + (b.view_pos.x - a.view_pos.x) * t,
                           a.view_pos.y + (b.view_pos.y - a.view_pos.y) * t,
                           -demo->near_plane},
              .uv = {a.uv.x + (b.uv.x - a.uv.x) * t,
                     a.uv.y + (b.uv.y - a.uv.y) * t}};
          out_poly[out_count++] = inter;
        } else if (!a_in && b_in) {
          float t = (-demo->near_plane - a.view_pos.z) /
                    (b.view_pos.z - a.view_pos.z);
          ClipVert inter = {
              .view_pos = {a.view_pos.x + (b.view_pos.x - a.view_pos.x) * t,
                           a.view_pos.y + (b.view_pos.y - a.view_pos.y) * t,
                           -demo->near_plane},
              .uv = {a.uv.x + (b.uv.x - a.uv.x) * t,
                     a.uv.y + (b.uv.y - a.uv.y) * t}};
          out_poly[out_count++] = inter;
          out_poly[out_count++] = b;
        }
      }

      if (out_count < 3) {
        continue;
      }

      int tri_sets[2][3] = {{0, 1, 2}, {0, 2, 3}};
      int tri_total = (out_count == 4) ? 2 : 1;

      for (int t = 0; t < tri_total; t++) {
        ClipVert *a = &out_poly[tri_sets[t][0]];
        ClipVert *b = &out_poly[tri_sets[t][1]];
        ClipVert *c = &out_poly[tri_sets[t][2]];

        v3f edge1 = v3_sub(b->view_pos, a->view_pos);
        v3f edge2 = v3_sub(c->view_pos, a->view_pos);
        v3f normal = v3_cross(edge1, edge2);
        if (v3_dot(normal, a->view_pos) >= 0.0f) {
          continue;
        }

        VertexPC pv[3];
        int masks[3];
        if (!project_vertex(a, &proj, game->render_w, game->render_h, &pv[0],
                            &masks[0]) ||
            !project_vertex(b, &proj, game->render_w, game->render_h, &pv[1],
                            &masks[1]) ||
            !project_vertex(c, &proj, game->render_w, game->render_h, &pv[2],
                            &masks[2])) {
          continue;
        }
        if ((masks[0] & masks[1] & masks[2]) != 0) {
          continue;
        }

        if (demo->wireframe) {
          draw_triangle(game->buffer, game->render_w, game->render_h, pv[0].pos,
                        pv[1].pos, pv[2].pos, WHITE, WIREFRAME);
        } else {
          draw_textured_triangle(game->buffer, game->depth, game->render_w,
                                 game->render_h, tex, pv[0], pv[1], pv[2]);
        }
      }
    }
  }

  char fps_text[32];
  snprintf(fps_text, sizeof(fps_text), "FPS %d", (int)(demo->fps + 0.5f));
  draw_text(game->buffer, game->render_w, (v2i){5, 5}, fps_text, WHITE);

  SDL_UpdateTexture(game->texture, NULL, game->buffer, game->pitch);
  SDL_RenderClear(game->renderer);
  SDL_Rect dest = {0, 0, (int)game->window_w, (int)game->window_h};
  SDL_RenderCopy(game->renderer, game->texture, NULL, &dest);
  SDL_RenderPresent(game->renderer);
}

int main(void) {
  ModelDemo demo = {0};
  if (!model_demo_init(&demo)) {
    return 1;
  }

  while (demo.running) {
    Uint32 now = SDL_GetTicks();
    float dt = (now - demo.last_ticks) / 1000.0f;
    demo.last_ticks = now;
    if (dt > 0.0f) {
      float inst = 1.0f / dt;
      demo.fps = demo.fps * 0.9f + inst * 0.1f;
    }

    while (SDL_PollEvent(&demo.game.event)) {
      model_demo_handle_event(&demo, &demo.game.event);
    }

    model_demo_frame(&demo, now, dt);
  }

  model_demo_shutdown(&demo);
  return 0;
}
