#ifndef NBT_QNX_SCREEN_H
#define NBT_QNX_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Documented QNX Screen 6.5 ABI subset; use the SDK header when available. */
typedef struct _screen_context *nbt_screen_context;
typedef struct _screen_window *nbt_screen_window;
typedef struct _screen_buffer *nbt_screen_buffer;

enum {
    NBT_SCREEN_APPLICATION_CONTEXT = 0,
    NBT_SCREEN_PROPERTY_BUFFER_SIZE = 5,
    NBT_SCREEN_PROPERTY_FORMAT = 14,
    NBT_SCREEN_PROPERTY_ID_STRING = 20,
    NBT_SCREEN_PROPERTY_POINTER = 34,
    NBT_SCREEN_PROPERTY_RENDER_BUFFERS = 37,
    NBT_SCREEN_PROPERTY_SIZE = 40,
    NBT_SCREEN_PROPERTY_STRIDE = 44,
    NBT_SCREEN_PROPERTY_USAGE = 48,
    NBT_SCREEN_PROPERTY_VISIBLE = 51,
    NBT_SCREEN_USAGE_NATIVE = 1 << 0,
    NBT_SCREEN_USAGE_WRITE = 1 << 2,
    NBT_SCREEN_FORMAT_RGBA8888 = 8,
    NBT_SCREEN_FORMAT_RGBX8888 = 9
};

int screen_create_context(nbt_screen_context *context, int flags);
int screen_destroy_context(nbt_screen_context context);
int screen_create_window(nbt_screen_window *window, nbt_screen_context context);
int screen_destroy_window(nbt_screen_window window);
int screen_set_window_property_iv(nbt_screen_window window, int property, const int *value);
int screen_set_window_property_cv(nbt_screen_window window, int property, int length,
                                  const char *value);
int screen_create_window_buffers(nbt_screen_window window, int count);
int screen_get_window_property_pv(nbt_screen_window window, int property, void **value);
int screen_get_buffer_property_pv(nbt_screen_buffer buffer, int property, void **value);
int screen_get_buffer_property_iv(nbt_screen_buffer buffer, int property, int *value);
int screen_post_window(nbt_screen_window window, nbt_screen_buffer buffer, int count,
                       const int *rectangles, int flags);

#ifdef __cplusplus
}
#endif

#endif
