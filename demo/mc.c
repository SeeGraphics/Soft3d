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

#define GRAVITY 14
#define PLAYER_RADIUS 0.3f
#define PLAYER_HEIGHT 1.6f
#define JUMP_VELOCITY 6.0f
#define WALK_SPEED 4.0f

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

typedef enum
{
  BLOCK_AIR = 0,
  BLOCK_GRASS,
  BLOCK_DIRT,
  BLOCK_STONE,
} BlockType;

typedef struct
{
  Vertex3D v[3];
  Texture *tex;
} Face;

typedef struct
{
  v3f view_pos;
  v2f uv;
} ClipVert;

typedef struct
{
  Game game;
  Camera camera;
  Texture dirt_tex;
  Texture stone_tex;
  bool wireframe;
  bool noclip;
  float fps;
  int culled_faces_count;
  int rendered_faces_count;
  v3f velocity;
  bool grounded;
  Uint32 last_ticks;
  bool running;
  int render_scale;
  float near_plane;
  float mouse_sens;
  Face *faces;
  int face_count;
  int face_cap;
  BlockType *blocks;
  int size_x;
  int size_y;
  int size_z;
  bool mesh_dirty;
} Demo;

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

  buffer_reallocate(&game->buffer, game->render_w, game->render_h,
                    sizeof(u32));
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

static bool load_texture(Texture *tex, const char *path)
{
  return texture_load(tex, path);
}

static void add_face(Face *faces, int *count, Texture *tex, v3f p0, v3f p1,
                     v3f p2, v3f p3)
{
  faces[*count].v[0] = (Vertex3D){p0, {0.0f, 1.0f}};
  faces[*count].v[1] = (Vertex3D){p1, {1.0f, 1.0f}};
  faces[*count].v[2] = (Vertex3D){p2, {1.0f, 0.0f}};
  faces[*count].tex = tex;
  (*count)++;

  faces[*count].v[0] = (Vertex3D){p0, {0.0f, 1.0f}};
  faces[*count].v[1] = (Vertex3D){p2, {1.0f, 0.0f}};
  faces[*count].v[2] = (Vertex3D){p3, {0.0f, 0.0f}};
  faces[*count].tex = tex;
  (*count)++;
}

static inline int block_index(const Demo *demo, int x, int y, int z)
{
  return (y * demo->size_z + z) * demo->size_x + x;
}

static inline BlockType block_get(const Demo *demo, int x, int y, int z)
{
  if (x < 0 || x >= demo->size_x || y < 0 || y >= demo->size_y || z < 0 ||
      z >= demo->size_z)
  {
    return BLOCK_AIR;
  }
  return demo->blocks[block_index(demo, x, y, z)];
}

static inline void block_set(Demo *demo, int x, int y, int z, BlockType t)
{
  if (x < 0 || x >= demo->size_x || y < 0 || y >= demo->size_y || z < 0 ||
      z >= demo->size_z)
  {
    return;
  }
  demo->blocks[block_index(demo, x, y, z)] = t;
  demo->mesh_dirty = true;
}

static void resolve_collisions(Demo *demo)
{
  float pmin_x = demo->camera.pos.x - PLAYER_RADIUS;
  float pmax_x = demo->camera.pos.x + PLAYER_RADIUS;
  float pmin_y = demo->camera.pos.y - PLAYER_HEIGHT;
  float pmax_y = demo->camera.pos.y;
  float pmin_z = demo->camera.pos.z - PLAYER_RADIUS;
  float pmax_z = demo->camera.pos.z + PLAYER_RADIUS;

  float pcx = (pmin_x + pmax_x) * 0.5f;
  float pcy = (pmin_y + pmax_y) * 0.5f;
  float pcz = (pmin_z + pmax_z) * 0.5f;

  int ix_min = (int)floorf(pmin_x + (float)demo->size_x * 0.5f);
  int ix_max = (int)floorf(pmax_x + (float)demo->size_x * 0.5f);
  int iz_min = (int)floorf(pmin_z + (float)demo->size_z * 0.5f);
  int iz_max = (int)floorf(pmax_z + (float)demo->size_z * 0.5f);
  int iy_min = (int)floorf(-pmax_y);
  int iy_max = (int)floorf(-pmin_y);

  if (ix_min < 0)
    ix_min = 0;
  if (iy_min < 0)
    iy_min = 0;
  if (iz_min < 0)
    iz_min = 0;
  if (ix_max >= demo->size_x)
    ix_max = demo->size_x - 1;
  if (iy_max >= demo->size_y)
    iy_max = demo->size_y - 1;
  if (iz_max >= demo->size_z)
    iz_max = demo->size_z - 1;

  demo->grounded = false;

  for (int x = ix_min; x <= ix_max; x++)
  {
    for (int z = iz_min; z <= iz_max; z++)
    {
      for (int y = iy_min; y <= iy_max; y++)
      {
        if (block_get(demo, x, y, z) == BLOCK_AIR)
        {
          continue;
        }

        float bmin_x = (float)x - (float)demo->size_x * 0.5f;
        float bmax_x = bmin_x + 1.0f;
        float bmin_z = (float)z - (float)demo->size_z * 0.5f;
        float bmax_z = bmin_z + 1.0f;
        float bmin_y = -(float)(y + 1);
        float bmax_y = -(float)y;

        float ox = fminf(pmax_x, bmax_x) - fmaxf(pmin_x, bmin_x);
        float oy = fminf(pmax_y, bmax_y) - fmaxf(pmin_y, bmin_y);
        float oz = fminf(pmax_z, bmax_z) - fmaxf(pmin_z, bmin_z);

        if (ox > 0.0f && oy > 0.0f && oz > 0.0f)
        {
          if (ox <= oy && ox <= oz)
          {
            float dir = (pcx < (bmin_x + bmax_x) * 0.5f) ? -ox : ox;
            demo->camera.pos.x += dir;
            pmin_x += dir;
            pmax_x += dir;
            pcx += dir;
          }
          else if (oy <= ox && oy <= oz)
          {
            float dir = (pcy < (bmin_y + bmax_y) * 0.5f) ? -oy : oy;
            demo->camera.pos.y += dir;
            pmin_y += dir;
            pmax_y += dir;
            pcy += dir;
            demo->velocity.y = 0.0f;
            if (dir > 0.0f)
            {
              demo->grounded = true;
            }
          }
          else
          {
            float dir = (pcz < (bmin_z + bmax_z) * 0.5f) ? -oz : oz;
            demo->camera.pos.z += dir;
            pmin_z += dir;
            pmax_z += dir;
            pcz += dir;
          }
        }
      }
    }
  }
}
static void rebuild_faces(Demo *demo)
{
  demo->face_count = 0;
  const int max_faces = demo->size_x * demo->size_y * demo->size_z * 12;
  if (!demo->faces || demo->face_cap < max_faces)
  {
    free(demo->faces);
    demo->faces = malloc((size_t)max_faces * sizeof(Face));
    demo->face_cap = max_faces;
  }

#define IS_SOLID(ix, iy, iz) (block_get(demo, (ix), (iy), (iz)) != BLOCK_AIR)

  for (int x = 0; x < demo->size_x; x++)
  {
    for (int z = 0; z < demo->size_z; z++)
    {
      for (int y = 0; y < demo->size_y; y++)
      {
        BlockType type = block_get(demo, x, y, z);
        if (type == BLOCK_AIR)
        {
          continue;
        }

        Texture *tex = (type == BLOCK_DIRT) ? &demo->dirt_tex : &demo->stone_tex;

        float bx = (float)x - (demo->size_x * 0.5f) + 0.5f;
        float by = -(float)y - 0.5f;
        float bz = (float)z - (demo->size_z * 0.5f) + 0.5f;

        float x0 = bx - 0.5f, x1 = bx + 0.5f;
        float y0 = by - 0.5f, y1 = by + 0.5f;
        float z0 = bz - 0.5f, z1 = bz + 0.5f;

        if (!IS_SOLID(x, y - 1, z))
        { // top (+y in world)
          add_face(demo->faces, &demo->face_count, tex,
                   (v3f){x0, y1, z1}, (v3f){x1, y1, z1}, (v3f){x1, y1, z0},
                   (v3f){x0, y1, z0});
        }
        if (!IS_SOLID(x, y + 1, z))
        { // bottom (-y in world)
          add_face(demo->faces, &demo->face_count, tex,
                   (v3f){x0, y0, z0}, (v3f){x1, y0, z0}, (v3f){x1, y0, z1},
                   (v3f){x0, y0, z1});
        }
        if (!IS_SOLID(x, y, z + 1))
        { // front (+z)
          add_face(demo->faces, &demo->face_count, tex,
                   (v3f){x0, y0, z1}, (v3f){x1, y0, z1}, (v3f){x1, y1, z1},
                   (v3f){x0, y1, z1});
        }
        if (!IS_SOLID(x, y, z - 1))
        { // back (-z)
          add_face(demo->faces, &demo->face_count, tex,
                   (v3f){x1, y0, z0}, (v3f){x0, y0, z0}, (v3f){x0, y1, z0},
                   (v3f){x1, y1, z0});
        }
        if (!IS_SOLID(x - 1, y, z))
        { // left (-x)
          add_face(demo->faces, &demo->face_count, tex,
                   (v3f){x0, y0, z0}, (v3f){x0, y0, z1}, (v3f){x0, y1, z1},
                   (v3f){x0, y1, z0});
        }
        if (!IS_SOLID(x + 1, y, z))
        { // right (+x)
          add_face(demo->faces, &demo->face_count, tex,
                   (v3f){x1, y0, z1}, (v3f){x1, y0, z0}, (v3f){x1, y1, z0},
                   (v3f){x1, y1, z1});
        }
      }
    }
  }
#undef IS_SOLID
  demo->mesh_dirty = false;
}

static bool project_vertex(const ClipVert *cv, const mat4 *proj, int render_w,
                           int render_h, VertexPC *out, int *mask_out)
{
  v4f clip = mat4_mul_v4(*proj,
                         (v4f){cv->view_pos.x, cv->view_pos.y, cv->view_pos.z,
                               1.0f});
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

static bool raycast_block(Demo *demo, v3f origin, v3f dir, float max_dist,
                          int *hx, int *hy, int *hz, v3f *hnormal)
{
  float gx = origin.x + (float)demo->size_x * 0.5f;
  float gy = -origin.y;
  float gz = origin.z + (float)demo->size_z * 0.5f;

  float gdx = dir.x;
  float gdy = -dir.y;
  float gdz = dir.z;

  int ix = (int)floorf(gx);
  int iy = (int)floorf(gy);
  int iz = (int)floorf(gz);

  int stepX = (gdx > 0.0f) ? 1 : -1;
  int stepY = (gdy > 0.0f) ? 1 : -1;
  int stepZ = (gdz > 0.0f) ? 1 : -1;

  float invX = (gdx != 0.0f) ? (1.0f / fabsf(gdx)) : INFINITY;
  float invY = (gdy != 0.0f) ? (1.0f / fabsf(gdy)) : INFINITY;
  float invZ = (gdz != 0.0f) ? (1.0f / fabsf(gdz)) : INFINITY;

  float tMaxX = (gdx != 0.0f)
                    ? ((stepX > 0 ? ((float)(ix + 1) - gx) : (gx - (float)ix)) *
                       invX)
                    : INFINITY;
  float tMaxY = (gdy != 0.0f)
                    ? ((stepY > 0 ? ((float)(iy + 1) - gy) : (gy - (float)iy)) *
                       invY)
                    : INFINITY;
  float tMaxZ = (gdz != 0.0f)
                    ? ((stepZ > 0 ? ((float)(iz + 1) - gz) : (gz - (float)iz)) *
                       invZ)
                    : INFINITY;

  v3f normal = {0};
  float t = 0.0f;
  while (t <= max_dist)
  {
    if (ix >= 0 && ix < demo->size_x && iy >= 0 && iy < demo->size_y &&
        iz >= 0 && iz < demo->size_z)
    {
      if (block_get(demo, ix, iy, iz) != BLOCK_AIR)
      {
        *hx = ix;
        *hy = iy;
        *hz = iz;
        *hnormal = normal;
        return true;
      }
    }

    if (tMaxX < tMaxY)
    {
      if (tMaxX < tMaxZ)
      {
        t = tMaxX;
        tMaxX += invX;
        ix += stepX;
        normal = (v3f){(float)-stepX, 0.0f, 0.0f};
      }
      else
      {
        t = tMaxZ;
        tMaxZ += invZ;
        iz += stepZ;
        normal = (v3f){0.0f, 0.0f, (float)-stepZ};
      }
    }
    else
    {
      if (tMaxY < tMaxZ)
      {
        t = tMaxY;
        tMaxY += invY;
        iy += stepY;
        normal = (v3f){0.0f, (float)-stepY, 0.0f};
      }
      else
      {
        t = tMaxZ;
        tMaxZ += invZ;
        iz += stepZ;
        normal = (v3f){0.0f, 0.0f, (float)-stepZ};
      }
    }
    if (t > max_dist)
    {
      break;
    }
  }
  return false;
}

static bool demo_init(Demo *demo)
{
  *demo = (Demo){0};
  demo->game.window_w = 960;
  demo->game.window_h = 540;
  demo->render_scale = 2;
  demo->near_plane = 0.1f;
  demo->mouse_sens = 0.0025f;
  demo->camera = (Camera){.pos = {0.0f, 1.5f, 6.0f}, .yaw = 0.0f, .pitch = 0.0f};
  resize_render(&demo->game, (int)demo->game.window_w,
                (int)demo->game.window_h, demo->render_scale);
  demo->size_x = 16;
  demo->size_z = 16;
  demo->size_y = 3;

  if (SDL_Init(SDL_INIT_VIDEO) != 0)
  {
    SDL_Log("Failed to Initialize SDL: %s\n", SDL_GetError());
    return false;
  }

  if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP) &
        (IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP)))
  {
    SDL_Log("Failed to init SDL_image: %s\n", IMG_GetError());
    SDL_Quit();
    return false;
  }

  if (!load_texture(&demo->dirt_tex, "assets/dirt.webp") ||
      !load_texture(&demo->stone_tex, "assets/stone.webp"))
  {
    IMG_Quit();
    SDL_Quit();
    return false;
  }

  const char *title = "Demo: Chunk";
  demo->game.window = SDL_CreateWindow(
      title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      (int)demo->game.window_w, (int)demo->game.window_h,
      SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_RESIZABLE);
  if (demo->game.window == NULL)
  {
    SDL_Log("Failed to create Window: %s\n", SDL_GetError());
    texture_destroy(&demo->dirt_tex);
    texture_destroy(&demo->stone_tex);
    IMG_Quit();
    SDL_Quit();
    return false;
  }
  SDL_RaiseWindow(demo->game.window);

  demo->game.renderer =
      SDL_CreateRenderer(demo->game.window, -1, SDL_RENDERER_ACCELERATED);
  if (demo->game.renderer == NULL)
  {
    SDL_Log("Failed to create Renderer: %s\n", SDL_GetError());
    SDL_DestroyWindow(demo->game.window);
    texture_destroy(&demo->dirt_tex);
    texture_destroy(&demo->stone_tex);
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
  demo->culled_faces_count = 0;
  demo->rendered_faces_count = 0;
  demo->velocity = (v3f){0};
  demo->grounded = false;
  demo->last_ticks = SDL_GetTicks();
  demo->running = true;

  int block_count = demo->size_x * demo->size_y * demo->size_z;
  demo->blocks = malloc((size_t)block_count * sizeof(BlockType));
  for (int x = 0; x < demo->size_x; x++)
  {
    for (int z = 0; z < demo->size_z; z++)
    {
      for (int y = 0; y < demo->size_y; y++)
      {
        block_set(demo, x, y, z, (y == 0) ? BLOCK_DIRT : BLOCK_STONE);
      }
    }
  }
  rebuild_faces(demo);
  return true;
}

static void demo_shutdown(Demo *demo)
{
  if (demo->faces)
  {
    free(demo->faces);
    demo->faces = NULL;
  }
  if (demo->blocks)
  {
    free(demo->blocks);
    demo->blocks = NULL;
  }
  if (demo->game.buffer)
  {
    free(demo->game.buffer);
    demo->game.buffer = NULL;
  }
  if (demo->game.depth)
  {
    free(demo->game.depth);
    demo->game.depth = NULL;
  }
  texture_destroy(&demo->dirt_tex);
  texture_destroy(&demo->stone_tex);
  if (demo->game.texture)
  {
    SDL_DestroyTexture(demo->game.texture);
    demo->game.texture = NULL;
  }
  if (demo->game.renderer)
  {
    SDL_DestroyRenderer(demo->game.renderer);
    demo->game.renderer = NULL;
  }
  if (demo->game.window)
  {
    SDL_DestroyWindow(demo->game.window);
    demo->game.window = NULL;
  }
  IMG_Quit();
  SDL_Quit();
}

static void demo_handle_event(Demo *demo, const SDL_Event *event)
{
  Game *game = &demo->game;
  switch (event->type)
  {
  case SDL_QUIT:
    demo->running = false;
    break;
  case SDL_WINDOWEVENT:
    if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
    {
      resize_render(&demo->game, event->window.data1, event->window.data2,
                    demo->render_scale);
      demo->mesh_dirty = true;
    }
    break;
  case SDL_MOUSEMOTION:
    if (game->mouse_grabbed)
    {
      demo->camera.yaw += (float)event->motion.xrel * demo->mouse_sens;
      demo->camera.pitch -= (float)event->motion.yrel * demo->mouse_sens;
    }
    break;
  case SDL_KEYDOWN:
    if (event->key.keysym.sym == SDLK_ESCAPE)
    {
      demo->running = false;
    }
    if (event->key.keysym.sym == SDLK_v)
    {
      demo->noclip = !demo->noclip;
      demo->velocity = (v3f){0};
      demo->grounded = true;
    }
    if (event->key.keysym.sym == SDLK_r)
    {
      demo->wireframe = !demo->wireframe;
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
              game->window,
              is_full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) == 0)
      {
        int w, h;
        SDL_GetWindowSize(game->window, &w, &h);
        resize_render(&demo->game, w, h, demo->render_scale);
      }
    }
    break;
  case SDL_MOUSEBUTTONDOWN:
    if (event->button.button == SDL_BUTTON_LEFT ||
        event->button.button == SDL_BUTTON_RIGHT)
    {
      int hx, hy, hz;
      v3f normal;
      if (raycast_block(demo, demo->camera.pos,
                        camera_forward(&demo->camera), 6.0f, &hx, &hy, &hz,
                        &normal))
      {
        if (event->button.button == SDL_BUTTON_LEFT)
        {
          if (block_get(demo, hx, hy, hz) != BLOCK_AIR)
          {
            block_set(demo, hx, hy, hz, BLOCK_AIR);
          }
        }
        else
        {
          int tx = hx + (int)normal.x;
          int ty = hy + (int)normal.y;
          int tz = hz + (int)normal.z;
          if (block_get(demo, tx, ty, tz) == BLOCK_AIR)
          {
            block_set(demo, tx, ty, tz, BLOCK_DIRT);
          }
        }
      }
    }
    break;
  default:
    break;
  }
}

static void demo_frame(Demo *demo, Uint32 now, float dt)
{
  Game *game = &demo->game;
  const float world_up_y = 1.0f;
  demo->culled_faces_count = 0;
  demo->rendered_faces_count = 0;

  const Uint8 *state = SDL_GetKeyboardState(NULL);
  v3f forward = camera_forward(&demo->camera);
  v3f world_up = {0.0f, world_up_y, 0.0f};
  v3f right = v3_normalize(v3_cross(forward, world_up));

  if (demo->noclip)
  {
    float move_speed = 4.0f * dt;
    if (state[SDL_SCANCODE_W])
    {
      demo->camera.pos = v3_add(demo->camera.pos, v3_scale(forward, move_speed));
    }
    if (state[SDL_SCANCODE_S])
    {
      demo->camera.pos = v3_sub(demo->camera.pos, v3_scale(forward, move_speed));
    }
    if (state[SDL_SCANCODE_A])
    {
      demo->camera.pos = v3_sub(demo->camera.pos, v3_scale(right, move_speed));
    }
    if (state[SDL_SCANCODE_D])
    {
      demo->camera.pos = v3_add(demo->camera.pos, v3_scale(right, move_speed));
    }
    if (state[SDL_SCANCODE_SPACE])
    {
      demo->camera.pos.y += move_speed;
    }
    if (state[SDL_SCANCODE_LCTRL])
    {
      demo->camera.pos.y -= move_speed;
    }
    demo->velocity = (v3f){0};
    demo->grounded = true;
  }
  else
  {
    // flatten forward for ground movement so looking up/down doesn't move vertically
    v3f forward_flat = {forward.x, 0.0f, forward.z};
    if (v3_dot(forward_flat, forward_flat) > 0.0f)
    {
      forward_flat = v3_normalize(forward_flat);
    }

    v3f move_dir = {0};
    if (state[SDL_SCANCODE_W])
    {
      move_dir = v3_add(move_dir, forward_flat);
    }
    if (state[SDL_SCANCODE_S])
    {
      move_dir = v3_sub(move_dir, forward_flat);
    }
    if (state[SDL_SCANCODE_A])
    {
      move_dir = v3_sub(move_dir, right);
    }
    if (state[SDL_SCANCODE_D])
    {
      move_dir = v3_add(move_dir, right);
    }
    if (v3_dot(move_dir, move_dir) > 0.0f)
    {
      move_dir = v3_normalize(move_dir);
      demo->camera.pos = v3_add(demo->camera.pos, v3_scale(move_dir, WALK_SPEED * dt));
    }
  }

  float look_speed = 1.5f * dt;
  if (state[SDL_SCANCODE_LEFT])
  {
    demo->camera.yaw -= look_speed;
  }
  if (state[SDL_SCANCODE_RIGHT])
  {
    demo->camera.yaw += look_speed;
  }
  if (state[SDL_SCANCODE_UP])
  {
    demo->camera.pitch += look_speed;
  }
  if (state[SDL_SCANCODE_DOWN])
  {
    demo->camera.pitch -= look_speed;
  }
  float max_pitch = (float)M_PI_2 - 0.1f;
  if (demo->camera.pitch > max_pitch)
    demo->camera.pitch = max_pitch;
  if (demo->camera.pitch < -max_pitch)
    demo->camera.pitch = -max_pitch;

  if (!demo->noclip)
  {
    // Jump: only allow when grounded
    if (demo->grounded && state[SDL_SCANCODE_SPACE])
    {
      demo->velocity.y = JUMP_VELOCITY;
      demo->grounded = false;
    }

  // Apply gravity and integrate vertical velocity
  demo->velocity.y -= GRAVITY * dt;
  demo->camera.pos.y += demo->velocity.y * dt;

  resolve_collisions(demo);
}

  memset(game->buffer, 0, game->render_w * game->render_h * sizeof(u32));
  clear_depth(game->depth, (size_t)game->render_w * (size_t)game->render_h);
  if (demo->mesh_dirty)
  {
    rebuild_faces(demo);
  }

  float aspect = (float)game->render_w / (float)game->render_h;
  mat4 model = mat4_identity();
  mat4 view = mat4_look_at(
      demo->camera.pos,
      v3_add(demo->camera.pos, camera_forward(&demo->camera)), world_up);
  mat4 proj = mat4_perspective((float)M_PI / 3.0f, aspect, demo->near_plane,
                               100.0f);
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
  CachedVertex tri[3];

  for (int i = 0; i < demo->face_count; i++)
  {
    Face *face = &demo->faces[i];
    bool skip = false;
    for (int j = 0; j < 3; j++)
    {
      v4f world = {face->v[j].pos.x, face->v[j].pos.y, face->v[j].pos.z,
                   1.0f};
      v4f view_pos4 = mat4_mul_v4(mv, world);
      v4f clip = mat4_mul_v4(proj, view_pos4);

      tri[j].uv = face->v[j].uv;
      tri[j].view_pos = (v3f){view_pos4.x, view_pos4.y, view_pos4.z};

      int mask = 0;
      if (clip.w == 0.0f)
      {
        tri[j].clip_mask = 0x3F; // force cull
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
    if (skip)
    {
      continue;
    }

    if ((tri[0].clip_mask & tri[1].clip_mask & tri[2].clip_mask) != 0)
    {
      demo->culled_faces_count++;
      continue; // frustum culled
    }

    bool near_in[3] = {tri[0].view_pos.z <= -demo->near_plane,
                       tri[1].view_pos.z <= -demo->near_plane,
                       tri[2].view_pos.z <= -demo->near_plane};
    bool needs_clip = !(near_in[0] && near_in[1] && near_in[2]);

    if (!needs_clip)
    {
      if (!tri[0].depth_ok || !tri[1].depth_ok || !tri[2].depth_ok)
      {
        continue;
      }
      v3f edge1 = v3_sub(tri[1].view_pos, tri[0].view_pos);
      v3f edge2 = v3_sub(tri[2].view_pos, tri[0].view_pos);
      v3f normal = v3_cross(edge1, edge2);
      if (v3_dot(normal, tri[0].view_pos) >= 0.0f)
      {
        continue;
      }
      VertexPC pv[3] = {
          {.pos = tri[0].screen, .uv = tri[0].uv, .inv_w = tri[0].inv_w, .depth = tri[0].depth},
          {.pos = tri[1].screen, .uv = tri[1].uv, .inv_w = tri[1].inv_w, .depth = tri[1].depth},
          {.pos = tri[2].screen, .uv = tri[2].uv, .inv_w = tri[2].inv_w, .depth = tri[2].depth},
      };

      if (demo->wireframe)
      {
        draw_triangle(game->buffer, game->render_w, game->render_h, pv[0].pos,
                      pv[1].pos, pv[2].pos, WHITE, WIREFRAME);
      }
      else
      {
        draw_textured_triangle(game->buffer, game->depth, game->render_w,
                               game->render_h, face->tex, pv[0], pv[1], pv[2]);
      }
      demo->rendered_faces_count++;
    }
    else
    {
      ClipVert in_poly[4] = {
          {.view_pos = tri[0].view_pos, .uv = tri[0].uv},
          {.view_pos = tri[1].view_pos, .uv = tri[1].uv},
          {.view_pos = tri[2].view_pos, .uv = tri[2].uv},
      };
      int in_count = 3;
      ClipVert out_poly[4];
      int out_count = 0;

      for (int v = 0; v < in_count; v++)
      {
        ClipVert a = in_poly[v];
        ClipVert b = in_poly[(v + 1) % in_count];
        bool a_in = a.view_pos.z <= -demo->near_plane;
        bool b_in = b.view_pos.z <= -demo->near_plane;

        if (a_in && b_in)
        {
          out_poly[out_count++] = b;
        }
        else if (a_in && !b_in)
        {
          float t = (-demo->near_plane - a.view_pos.z) /
                    (b.view_pos.z - a.view_pos.z);
          ClipVert inter = {
              .view_pos = {a.view_pos.x + (b.view_pos.x - a.view_pos.x) * t,
                           a.view_pos.y + (b.view_pos.y - a.view_pos.y) * t,
                           -demo->near_plane},
              .uv = {a.uv.x + (b.uv.x - a.uv.x) * t,
                     a.uv.y + (b.uv.y - a.uv.y) * t}};
          out_poly[out_count++] = inter;
        }
        else if (!a_in && b_in)
        {
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
        if (v3_dot(normal, a->view_pos) >= 0.0f)
        {
          continue;
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

        if (demo->wireframe)
        {
          draw_triangle(game->buffer, game->render_w, game->render_h, pv[0].pos,
                        pv[1].pos, pv[2].pos, WHITE, WIREFRAME);
        }
        else
        {
          draw_textured_triangle(game->buffer, game->depth, game->render_w,
                                 game->render_h, face->tex, pv[0], pv[1], pv[2]);
        }
        demo->rendered_faces_count++;
      }
    }
  }

  char fps_text[32];
  snprintf(fps_text, sizeof(fps_text), "FPS: %d", (int)(demo->fps + 0.5f));
  draw_text(game->buffer, game->render_w, (v2i){5, 5}, fps_text, WHITE);

  char culled_text[256];
  snprintf(culled_text, sizeof(culled_text), "CULLED FACES: %d", demo->culled_faces_count);
  draw_text(game->buffer, game->render_w, (v2i){5, 20}, culled_text, WHITE);

  char rendered_text[256];
  snprintf(rendered_text, sizeof(rendered_text), "RENDERED FACES: %d", demo->rendered_faces_count);
  draw_text(game->buffer, game->render_w, (v2i){5, 35}, rendered_text, WHITE);

  // Crosshair at the render center
  v2i center = {(int)(game->render_w / 2), (int)(game->render_h / 2)};
  int len = 6;
  draw_linei(game->buffer, game->render_w, game->render_h,
             (v2i){center.x - len, center.y}, (v2i){center.x + len, center.y},
             WHITE);
  draw_linei(game->buffer, game->render_w, game->render_h,
             (v2i){center.x, center.y - len}, (v2i){center.x, center.y + len},
             WHITE);

  SDL_UpdateTexture(game->texture, NULL, game->buffer, game->pitch);
  SDL_RenderClear(game->renderer);
  SDL_Rect dest = {0, 0, (int)game->window_w, (int)game->window_h};
  SDL_RenderCopy(game->renderer, game->texture, NULL, &dest);
  SDL_RenderPresent(game->renderer);
}

int main(void)
{
  Demo mc = {0};
  if (!demo_init(&mc))
  {
    return 1;
  }

  while (mc.running)
  {
    Uint32 now = SDL_GetTicks();
    float dt = (now - mc.last_ticks) / 1000.0f;
    if (dt > 0.1f)
    {
      dt = 0.1f; // clamp to avoid huge steps during resize/fullscreen toggles
    }
    mc.last_ticks = now;
    if (dt > 0.0f)
    {
      float inst = 1.0f / dt;
      mc.fps = mc.fps * 0.9f + inst * 0.1f;
    }

    while (SDL_PollEvent(&mc.game.event))
    {
      demo_handle_event(&mc, &mc.game.event);
    }

    demo_frame(&mc, now, dt);
  }

  demo_shutdown(&mc);
  return 0;
}
