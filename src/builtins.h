#pragma once

#include <string>
#include <vector>

#include "errors.h"
#include "value.h"

enum BuiltinId {
    B_print,
    B_println,
    B_sleep,
    B_time_ms,
    B_time_s,

    B_type,
    B_key_press,
    B_key_down,
    B_key_up,
    B_key_state,

    B_mouse_move,
    B_mouse_move_rel,
    B_mouse_x,
    B_mouse_y,
    B_mouse_click,
    B_mouse_down,
    B_mouse_up,
    B_mouse_wheel,
    B_mouse_wheel_h,

    B_screen_width,
    B_screen_height,
    B_pixel_color,

    B_clip_get,
    B_clip_set,

    B_input,
    B_exit,

    B_run,
    B_start,
    B_kill,
    B_msgbox,

    B_window_find,
    B_window_active,
    B_window_activate,
    B_window_title,
    B_window_wait,
    B_window_close,
    B_window_exists,

    B_random,
    B_random_float,

    B_to_str,
    B_to_int,
    B_to_float,
    B_to_bool,

    B_len,
    B_upper,
    B_lower,
    B_trim,
    B_replace,
    B_contains,
    B_substr,
    B_ord,
    B_chr,

    B_typeof
};

int builtinId(const std::string& name);
Value callBuiltin(int id, std::vector<Value>& args, int line, int col);
