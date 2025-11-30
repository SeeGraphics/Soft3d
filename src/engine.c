#include "engine.h"
#include "colors.h"
#include "math.h"
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

typedef struct
{
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

typedef struct
{
  v3f pos;
  float yaw;
  float pitch;
} Camera;

typedef struct
{
  Game game;
  Camera camera;
  Texture texture;
  bool wireframe;
  float fps;
  Uint32 last_ticks;
  bool running;
  int render_scale;
  float near_plane;
  float mouse_sens;
} Engine;

typedef struct
{
  v3f view_pos;
  v2f uv;
} ClipVert;

static v3f camera_forward(const Camera *cam)
{
  float cy = cosf(cam->yaw);
  float sy = sinf(cam->yaw);
  float cp = cosf(cam->pitch);
  float sp = sinf(cam->pitch);
  return v3_normalize((v3f){sy * cp, sp, -cy * cp});
}

static void clear_depth(float *depth, size_t count)
{
  for (size_t i = 0; i < count; i++)
  {
    depth[i] = 1.0f;
  }
}

static void resize_render(Game *game, int window_w, int window_h,
                          int render_scale)
{
  game->window_w = (u32)window_w;
  game->window_h = (u32)window_h;
  game->render_w = (u32)(window_w / render_scale);
  game->render_h = (u32)(window_h / render_scale);
  if (game->render_w == 0)
    game->render_w = 1;
  if (game->render_h == 0)
    game->render_h = 1;

  buffer_reallocate(&game->buffer, game->render_w, game->render_h, sizeof(u32));
  if (game->depth)
  {
    free(game->depth);
  }
  game->depth = malloc(game->render_w * game->render_h * sizeof(float));
  pitch_update(&game->pitch, game->render_w, sizeof(u32));
  if (game->renderer)
  {
    texture_recreate(&game->texture, game->renderer, game->render_w,
                     game->render_h);
  }
}

static const Vertex3D cube_vertices[] = {
    // Front (-Z)
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
    {{0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f}},
    {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f}},
    // Back (+Z)
    {{0.5f, -0.5f, 0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f, 0.5f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f}},
    // Left (-X)
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {1.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f}},
    // Right (+X)
    {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
    {{0.5f, -0.5f, 0.5f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f}},
    // Top (+Y)
    {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f}},
    // Bottom (-Y)
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 1.0f}},
    {{0.5f, -0.5f, 0.5f}, {1.0f, 1.0f}},
    {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
};

static const int cube_indices[][3] = {
    {0, 1, 2}, {0, 2, 3}, // front
    {4, 5, 6},
    {4, 6, 7}, // back
    {8, 9, 10},
    {8, 10, 11}, // left
    {12, 13, 14},
    {12, 14, 15}, // right
    {16, 17, 18},
    {16, 18, 19}, // top
    {20, 21, 22},
    {20, 22, 23} // bottom
};

static const int cube_vertex_count =
    (int)(sizeof(cube_vertices) / sizeof(cube_vertices[0]));
static const int cube_triangle_count =
    (int)(sizeof(cube_indices) / sizeof(cube_indices[0]));

static bool project_vertex(const ClipVert *cv, const mat4 *proj, int render_w,
                           int render_h, VertexPC *out, int *mask_out)
{
  v4f clip = mat4_mul_v4(
      *proj, (v4f){cv->view_pos.x, cv->view_pos.y, cv->view_pos.z, 1.0f});
  if (clip.w == 0.0f)
  {
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

static bool engine_init(Engine *eng)
{
  *eng = (Engine){0};
  eng->game.window_w = 800;
  eng->game.window_h = 600;
  eng->render_scale = 2;
  eng->near_plane = 0.1f;
  eng->mouse_sens = 0.0025f;
  eng->camera = (Camera){.pos = {0.0f, 0.0f, 2.0f}, .yaw = 0.0f, .pitch = 0.0f};
  resize_render(&eng->game, (int)eng->game.window_w, (int)eng->game.window_h,
                eng->render_scale);

  if (SDL_Init(SDL_INIT_VIDEO) != 0)
  {
    SDL_Log("Failed to Initialize SDL: %s\n", SDL_GetError());
    return false;
  }

  if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
  {
    SDL_Log("Failed to init SDL_image: %s\n", IMG_GetError());
    SDL_Quit();
    return false;
  }

  const char *texture_path = "assets/brick.png";
  if (!texture_load(&eng->texture, texture_path))
  {
    IMG_Quit();
    SDL_Quit();
    return false;
  }

  const char *title = "A: Hello Window";
  eng->game.window =
      SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       (int)eng->game.window_w, (int)eng->game.window_h,
                       SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_RESIZABLE);
  if (eng->game.window == NULL)
  {
    SDL_Log("Failed to create Window: %s\n", SDL_GetError());
    texture_destroy(&eng->texture);
    IMG_Quit();
    SDL_Quit();
    return false;
  }
  SDL_RaiseWindow(eng->game.window);

  eng->game.renderer =
      SDL_CreateRenderer(eng->game.window, -1, SDL_RENDERER_ACCELERATED);
  if (eng->game.renderer == NULL)
  {
    SDL_Log("Failed to create Renderer: %s\n", SDL_GetError());
    SDL_DestroyWindow(eng->game.window);
    texture_destroy(&eng->texture);
    IMG_Quit();
    SDL_Quit();
    return false;
  }

  texture_recreate(&eng->game.texture, eng->game.renderer, eng->game.render_w,
                   eng->game.render_h);
  SDL_SetRelativeMouseMode(SDL_TRUE);
  SDL_ShowCursor(SDL_DISABLE);
  eng->game.mouse_grabbed = true;
  eng->fps = 0.0f;
  eng->last_ticks = SDL_GetTicks();
  eng->running = true;
  return true;
}

static void engine_shutdown(Engine *eng)
{
  if (eng->game.buffer)
  {
    free(eng->game.buffer);
    eng->game.buffer = NULL;
  }
  if (eng->game.depth)
  {
    free(eng->game.depth);
    eng->game.depth = NULL;
  }
  texture_destroy(&eng->texture);
  if (eng->game.texture)
  {
    SDL_DestroyTexture(eng->game.texture);
    eng->game.texture = NULL;
  }
  if (eng->game.renderer)
  {
    SDL_DestroyRenderer(eng->game.renderer);
    eng->game.renderer = NULL;
  }
  if (eng->game.window)
  {
    SDL_DestroyWindow(eng->game.window);
    eng->game.window = NULL;
  }
  IMG_Quit();
  SDL_Quit();
}

static void engine_handle_event(Engine *eng, const SDL_Event *event)
{
  Game *game = &eng->game;
  switch (event->type)
  {
  case SDL_QUIT:
    eng->running = false;
    break;
  case SDL_WINDOWEVENT:
    if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
    {
      resize_render(&eng->game, event->window.data1, event->window.data2,
                    eng->render_scale);
    }
    break;
  case SDL_MOUSEMOTION:
    if (game->mouse_grabbed)
    {
      eng->camera.yaw += (float)event->motion.xrel * eng->mouse_sens;
      eng->camera.pitch -= (float)event->motion.yrel * eng->mouse_sens;
    }
    break;
  case SDL_KEYDOWN:
    if (event->key.keysym.sym == SDLK_ESCAPE)
    {
      eng->running = false;
    }
    if (event->key.keysym.sym == SDLK_r)
    {
      eng->wireframe = !eng->wireframe;
    }
    if (event->key.keysym.sym == SDLK_q)
    {
      game->mouse_grabbed = !game->mouse_grabbed;
      SDL_SetRelativeMouseMode(game->mouse_grabbed ? SDL_TRUE : SDL_FALSE);
      SDL_ShowCursor(game->mouse_grabbed ? SDL_DISABLE : SDL_ENABLE);
    }
    if (event->key.keysym.sym == SDLK_7)
    {
      Uint32 flags = SDL_GetWindowFlags(game->window);
      bool is_full = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
      if (SDL_SetWindowFullscreen(
              game->window, is_full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) == 0)
      {
        int w, h;
        SDL_GetWindowSize(game->window, &w, &h);
        resize_render(&eng->game, w, h, eng->render_scale);
      }
    }
    break;
  default:
    break;
  }
}

static void engine_frame(Engine *eng, Uint32 now, float dt)
{
  Game *game = &eng->game;
  const float world_up_y = 1.0f;

  const Uint8 *state = SDL_GetKeyboardState(NULL);
  v3f forward = camera_forward(&eng->camera);
  v3f world_up = {0.0f, world_up_y, 0.0f};
  v3f right = v3_normalize(v3_cross(forward, world_up));

  float move_speed = 2.5f * dt;
  if (state[SDL_SCANCODE_W])
  {
    eng->camera.pos = v3_add(eng->camera.pos, v3_scale(forward, move_speed));
  }
  if (state[SDL_SCANCODE_S])
  {
    eng->camera.pos = v3_sub(eng->camera.pos, v3_scale(forward, move_speed));
  }
  if (state[SDL_SCANCODE_A])
  {
    eng->camera.pos = v3_sub(eng->camera.pos, v3_scale(right, move_speed));
  }
  if (state[SDL_SCANCODE_D])
  {
    eng->camera.pos = v3_add(eng->camera.pos, v3_scale(right, move_speed));
  }
  if (state[SDL_SCANCODE_SPACE])
  {
    eng->camera.pos.y += move_speed;
  }
  if (state[SDL_SCANCODE_LCTRL])
  {
    eng->camera.pos.y -= move_speed;
  }

  float look_speed = 1.5f * dt;
  if (state[SDL_SCANCODE_LEFT])
  {
    eng->camera.yaw -= look_speed;
  }
  if (state[SDL_SCANCODE_RIGHT])
  {
    eng->camera.yaw += look_speed;
  }
  if (state[SDL_SCANCODE_UP])
  {
    eng->camera.pitch += look_speed;
  }
  if (state[SDL_SCANCODE_DOWN])
  {
    eng->camera.pitch -= look_speed;
  }
  float max_pitch = (float)M_PI_2 - 0.1f;
  if (eng->camera.pitch > max_pitch)
    eng->camera.pitch = max_pitch;
  if (eng->camera.pitch < -max_pitch)
    eng->camera.pitch = -max_pitch;

  memset(game->buffer, 0, game->render_w * game->render_h * sizeof(u32));
  clear_depth(game->depth, (size_t)game->render_w * (size_t)game->render_h);

  float aspect = (float)game->render_w / (float)game->render_h;
  float angle = (float)now * 0.001f;
  mat4 model = mat4_mul(mat4_rotate_y(angle), mat4_rotate_x(angle * 0.5f));
  mat4 view = mat4_look_at(
      eng->camera.pos, v3_add(eng->camera.pos, camera_forward(&eng->camera)),
      world_up);
  mat4 proj =
      mat4_perspective((float)M_PI / 3.0f, aspect, eng->near_plane, 100.0f);
  mat4 mv = mat4_mul(view, model);

  typedef struct
  {
    v2i screen;
    v2f uv;
    v3f view_pos;
    float inv_w;
    float depth;
    int clip_mask;
    bool depth_ok;
  } CachedVertex;
  CachedVertex cached[cube_vertex_count];

  for (int i = 0; i < cube_vertex_count; i++)
  {
    v4f world = {cube_vertices[i].pos.x, cube_vertices[i].pos.y,
                 cube_vertices[i].pos.z, 1.0f};
    v4f view_pos4 = mat4_mul_v4(mv, world);
    v4f clip = mat4_mul_v4(proj, view_pos4);

    cached[i].uv = cube_vertices[i].uv;
    cached[i].view_pos = (v3f){view_pos4.x, view_pos4.y, view_pos4.z};

    int mask = 0;
    if (clip.w == 0.0f)
    {
      cached[i].clip_mask = 0x3F; // force cull
      cached[i].depth_ok = false;
      continue;
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
    cached[i].clip_mask = mask;

    float inv_w = 1.0f / clip.w;
    cached[i].inv_w = inv_w;
    v3f ndc = {clip.x * inv_w, clip.y * inv_w, clip.z * inv_w};
    cached[i].depth_ok = ndc.z >= 0.0f && ndc.z <= 1.0f;
    cached[i].screen =
        norm_to_screen((v2f){ndc.x, ndc.y}, game->render_w, game->render_h);
    cached[i].depth = 0.5f * (ndc.z + 1.0f);
  }

  for (int tri_idx = 0; tri_idx < cube_triangle_count; tri_idx++)
  {
    const int i0 = cube_indices[tri_idx][0];
    const int i1 = cube_indices[tri_idx][1];
    const int i2 = cube_indices[tri_idx][2];
    const CachedVertex *v0 = &cached[i0];
    const CachedVertex *v1 = &cached[i1];
    const CachedVertex *v2 = &cached[i2];

    if ((v0->clip_mask & v1->clip_mask & v2->clip_mask) != 0)
    {
      continue; // frustum culled
    }

    bool near_in[3] = {v0->view_pos.z <= -eng->near_plane,
                       v1->view_pos.z <= -eng->near_plane,
                       v2->view_pos.z <= -eng->near_plane};
    bool needs_clip = !(near_in[0] && near_in[1] && near_in[2]);

    if (!needs_clip)
    {
      if (!v0->depth_ok || !v1->depth_ok || !v2->depth_ok)
      {
        continue;
      }
      v3f edge1 = v3_sub(v1->view_pos, v0->view_pos);
      v3f edge2 = v3_sub(v2->view_pos, v0->view_pos);
      v3f normal = v3_cross(edge1, edge2);
      if (normal.z >= 0.0f)
      {
        continue; // backface
      }

      VertexPC pv[3] = {
          {.pos = v0->screen,
           .uv = v0->uv,
           .inv_w = v0->inv_w,
           .depth = v0->depth},
          {.pos = v1->screen,
           .uv = v1->uv,
           .inv_w = v1->inv_w,
           .depth = v1->depth},
          {.pos = v2->screen,
           .uv = v2->uv,
           .inv_w = v2->inv_w,
           .depth = v2->depth},
      };

      if (eng->wireframe)
      {
        draw_triangle(game->buffer, game->render_w, game->render_h, pv[0].pos,
                      pv[1].pos, pv[2].pos, WHITE, WIREFRAME);
      }
      else
      {
        draw_textured_triangle(game->buffer, game->depth, game->render_w,
                               game->render_h, &eng->texture, pv[0], pv[1],
                               pv[2]);
      }
    }
    else
    {
      ClipVert in_poly[4] = {
          {.view_pos = v0->view_pos, .uv = v0->uv},
          {.view_pos = v1->view_pos, .uv = v1->uv},
          {.view_pos = v2->view_pos, .uv = v2->uv},
      };
      int in_count = 3;
      ClipVert out_poly[4];
      int out_count = 0;

      for (int i = 0; i < in_count; i++)
      {
        ClipVert a = in_poly[i];
        ClipVert b = in_poly[(i + 1) % in_count];
        bool a_in = a.view_pos.z <= -eng->near_plane;
        bool b_in = b.view_pos.z <= -eng->near_plane;

        if (a_in && b_in)
        {
          out_poly[out_count++] = b;
        }
        else if (a_in && !b_in)
        {
          float t =
              (-eng->near_plane - a.view_pos.z) / (b.view_pos.z - a.view_pos.z);
          ClipVert inter = {
              .view_pos = {a.view_pos.x + (b.view_pos.x - a.view_pos.x) * t,
                           a.view_pos.y + (b.view_pos.y - a.view_pos.y) * t,
                           -eng->near_plane},
              .uv = {a.uv.x + (b.uv.x - a.uv.x) * t,
                     a.uv.y + (b.uv.y - a.uv.y) * t}};
          out_poly[out_count++] = inter;
        }
        else if (!a_in && b_in)
        {
          float t =
              (-eng->near_plane - a.view_pos.z) / (b.view_pos.z - a.view_pos.z);
          ClipVert inter = {
              .view_pos = {a.view_pos.x + (b.view_pos.x - a.view_pos.x) * t,
                           a.view_pos.y + (b.view_pos.y - a.view_pos.y) * t,
                           -eng->near_plane},
              .uv = {a.uv.x + (b.uv.x - a.uv.x) * t,
                     a.uv.y + (b.uv.y - a.uv.y) * t}};
          out_poly[out_count++] = inter;
          out_poly[out_count++] = b;
        }
      }

      if (out_count < 3)
      {
        continue;
      }

      int tri_sets[2][3] = {{0, 1, 2}, {0, 2, 3}};
      int tri_total = (out_count == 4) ? 2 : 1;

      for (int t = 0; t < tri_total; t++)
      {
        ClipVert *a = &out_poly[tri_sets[t][0]];
        ClipVert *b = &out_poly[tri_sets[t][1]];
        ClipVert *c = &out_poly[tri_sets[t][2]];

        v3f edge1 = v3_sub(b->view_pos, a->view_pos);
        v3f edge2 = v3_sub(c->view_pos, a->view_pos);
        v3f normal = v3_cross(edge1, edge2);
        if (normal.z >= 0.0f)
        {
          continue; // backface
        }

        VertexPC pv[3];
        int masks[3];
        if (!project_vertex(a, &proj, game->render_w, game->render_h, &pv[0],
                            &masks[0]) ||
            !project_vertex(b, &proj, game->render_w, game->render_h, &pv[1],
                            &masks[1]) ||
            !project_vertex(c, &proj, game->render_w, game->render_h, &pv[2],
                            &masks[2]))
        {
          continue;
        }
        if ((masks[0] & masks[1] & masks[2]) != 0)
        {
          continue;
        }

        if (eng->wireframe)
        {
          draw_triangle(game->buffer, game->render_w, game->render_h, pv[0].pos,
                        pv[1].pos, pv[2].pos, WHITE, WIREFRAME);
        }
        else
        {
          draw_textured_triangle(game->buffer, game->depth, game->render_w,
                                 game->render_h, &eng->texture, pv[0], pv[1],
                                 pv[2]);
        }
      }
    }
  }

  char fps_text[32];
  snprintf(fps_text, sizeof(fps_text), "FPS %d", (int)(eng->fps + 0.5f));
  draw_text(game->buffer, game->render_w, (v2i){5, 5}, fps_text, WHITE);

  SDL_UpdateTexture(game->texture, NULL, game->buffer, game->pitch);
  SDL_RenderClear(game->renderer);
  SDL_Rect dest = {0, 0, (int)game->window_w, (int)game->window_h};
  SDL_RenderCopy(game->renderer, game->texture, NULL, &dest);
  SDL_RenderPresent(game->renderer);
}

int engine_run(void)
{
  Engine eng = {0};
  if (!engine_init(&eng))
  {
    return 1;
  }

  while (eng.running)
  {
    Uint32 now = SDL_GetTicks();
    float dt = (now - eng.last_ticks) / 1000.0f;
    eng.last_ticks = now;
    if (dt > 0.0f)
    {
      float inst = 1.0f / dt;
      eng.fps = eng.fps * 0.9f + inst * 0.1f;
    }

    while (SDL_PollEvent(&eng.game.event))
    {
      engine_handle_event(&eng, &eng.game.event);
    }

    engine_frame(&eng, now, dt);
  }

  engine_shutdown(&eng);
  return 0;
}
