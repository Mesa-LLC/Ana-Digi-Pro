/**
 * Ana-Digi Pro - Pebble Watchface
 *
 * A hybrid analog-digital watchface forked off "Casio AnaDigi" by Eric Migicovsky.
 * Ported with Claude Sonnet 4.6 and edited by Dimitrios Vlachos.
 * Date and LCD windows embedded in the lower portion of the dial.
 * Beautiful hands and windows animations when returning to watchface.
 *
 * Target platforms: Aplite (144x168)
 */

#include <pebble.h>

// ============================================================
// Colors
// ============================================================
#ifdef PBL_COLOR
  #define COLOR_DIAL        GColorTiffanyBlue
  #define COLOR_DIAL_DARK   GColorCadetBlue
  #define COLOR_BEZEL       GColorLightGray
  #define COLOR_BEZEL_DARK  GColorDarkGray
  #define COLOR_HAND        GColorWhite
  #define COLOR_HAND_EDGE   GColorDarkGray
  #define COLOR_SECOND      GColorWhite
  #define COLOR_MARKER      GColorWhite
  #define COLOR_MARKER_DIM  GColorLightGray
  #define COLOR_TEXT         GColorWhite
  #define COLOR_LCD_BG      GColorWhite
  #define COLOR_LCD_TEXT     GColorBlack
  #define COLOR_CENTER      GColorLightGray
  #define COLOR_CENTER_DOT  GColorDarkGray
#else
  #define COLOR_DIAL        GColorBlack
  #define COLOR_DIAL_DARK   GColorDarkGray
  #define COLOR_BEZEL       GColorDarkGray
  #define COLOR_BEZEL_DARK  GColorLightGray
  #define COLOR_HAND        GColorWhite
  #define COLOR_HAND_EDGE   GColorWhite
  #define COLOR_SECOND      GColorWhite
  #define COLOR_MARKER      GColorWhite
  #define COLOR_MARKER_DIM  GColorLightGray
  #define COLOR_TEXT         GColorWhite
  #define COLOR_LCD_BG      GColorBlack
  #define COLOR_LCD_TEXT     GColorWhite
  #define COLOR_CENTER      GColorWhite
  #define COLOR_CENTER_DOT  GColorBlack
#endif

// ============================================================
// Globals
// ============================================================
static Window *s_window;
static Layer *s_canvas;
static TextLayer *s_day_layer;

static GPath *s_hour_path;
static GPath *s_min_path;

static struct tm s_time;

// Forward declaration
static void update_time(void);

// ============================================================
// Intro animation state
// ============================================================
#define ANIM_DURATION_MS  2000
#define ANIM_INTERVAL_MS  33   // ~30 fps

static AppTimer *s_anim_timer = NULL;
static uint32_t  s_anim_elapsed = 0;   // ms elapsed so far
static bool      s_animating = false;

// Hand paths (Y negative = toward 12 o'clock)
#ifdef PBL_ROUND
  static const GPathInfo HOUR_HAND_INFO = {
    .num_points = 5,
    .points = (GPoint[]) {
      {-5, 14}, {-3, -52}, {0, -56}, {3, -52}, {5, 14}
    }
  };
  static const GPathInfo MIN_HAND_INFO = {
    .num_points = 5,
    .points = (GPoint[]) {
      {-4, 17}, {-2, -82}, {0, -88}, {2, -82}, {4, 17}
    }
  };
#elif defined(PBL_PLATFORM_APLITE)
  // Aplite 144x168: hands scaled for smaller 68px-radius dial
  static const GPathInfo HOUR_HAND_INFO = {
    .num_points = 5,
    .points = (GPoint[]) {
      {-4, 10}, {-2, -33}, {0, -37}, {2, -33}, {4, 10}
    }
  };
  static const GPathInfo MIN_HAND_INFO = {
    .num_points = 5,
    .points = (GPoint[]) {
      {-3, 12}, {-2, -53}, {0, -57}, {2, -53}, {3, 12}
    }
  };
#else
  static const GPathInfo HOUR_HAND_INFO = {
    .num_points = 5,
    .points = (GPoint[]) {
      {-5, 12}, {-3, -43}, {0, -47}, {3, -43}, {5, 12}
    }
  };
  static const GPathInfo MIN_HAND_INFO = {
    .num_points = 5,
    .points = (GPoint[]) {
      {-4, 14}, {-2, -68}, {0, -73}, {2, -68}, {4, 14}
    }
  };
#endif

// ============================================================
// Rounded-rectangle math
// ============================================================

static int32_t isqrt(int32_t n) {
  if (n <= 0) return 0;
  int32_t x = n;
  int32_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return x;
}

static int16_t rounded_rect_r(int32_t angle, int16_t hw,
                               int16_t hh_top, int16_t hh_bot, int16_t cr) {
  int32_t dx = sin_lookup(angle);
  int32_t dy = -cos_lookup(angle);

  int32_t adx = dx < 0 ? -dx : dx;
  int32_t ady = dy < 0 ? -dy : dy;
  if (adx < 200) adx = 200;
  if (ady < 200) ady = 200;

  int16_t hh = dy <= 0 ? hh_top : hh_bot;

  int32_t tv = (int32_t)hw * TRIG_MAX_RATIO / adx;
  int32_t th = (int32_t)hh * TRIG_MAX_RATIO / ady;
  int32_t t = tv < th ? tv : th;

  int32_t px = t * adx / TRIG_MAX_RATIO;
  int32_t py = t * ady / TRIG_MAX_RATIO;

  if (px > hw - cr && py > hh - cr) {
    int32_t cx = hw - cr;
    int32_t cy = hh - cr;
    int32_t D = (adx * cx + ady * cy) / TRIG_MAX_RATIO;
    int32_t E = (int32_t)cx * cx + (int32_t)cy * cy - (int32_t)cr * cr;
    int32_t disc = D * D - E;
    if (disc >= 0) {
      t = D + isqrt(disc);
    }
  }

  return (int16_t)t;
}

// ============================================================
// Layout helpers
// ============================================================

static void get_clock_geometry(GRect bounds, GPoint *center,
                               int16_t *hw, int16_t *hh_top,
                               int16_t *hh_bot, int16_t *cr) {
  int16_t w = bounds.size.w;
  int16_t h = bounds.size.h;

#ifdef PBL_ROUND
  center->x = w / 2;
  center->y = h / 2;
  int16_t margin = 8;
  int16_t r = w / 2 - margin;
  *hw     = r;
  *hh_top = r;
  *hh_bot = r;
  *cr     = r;
#else
  center->x = w / 2;
  center->y = h * 41 / 100;
  int16_t edge = 8;
  *hw     = w / 2 - edge;
  *hh_top = center->y - edge;
  *hh_bot = h - edge - center->y;
#ifdef PBL_PLATFORM_APLITE
  *cr = 18;
#else
  *cr = 24;
#endif
#endif
}

static GRect get_lcd_rect(GRect bounds) {
  int16_t w = bounds.size.w;
  int16_t h = bounds.size.h;
#ifdef PBL_ROUND
  int16_t lcd_w = w * 55 / 100;
  int16_t lcd_h = h * 14 / 100;
  int16_t lcd_y = h * 72 / 100;
#elif defined(PBL_PLATFORM_APLITE)
  int16_t lcd_w = w * 70 / 100;
  int16_t lcd_h = h * 15 / 100;
  int16_t lcd_y = h * 73 / 100;
#else
  int16_t lcd_w = w * 68 / 100;
  int16_t lcd_h = h * 16 / 100;
  int16_t lcd_y = h * 74 / 100;
#endif
  int16_t lcd_x = (w - lcd_w) / 2;
  return GRect(lcd_x, lcd_y, lcd_w, lcd_h);
}

// ============================================================
// Drawing functions
// ============================================================

static void draw_background(GContext *ctx, GRect bounds) {
  int16_t w = bounds.size.w;
  int16_t h = bounds.size.h;

#ifdef PBL_ROUND
  GPoint screen_center = GPoint(w / 2, h / 2);
  int16_t screen_r = w / 2;
  graphics_context_set_fill_color(ctx, COLOR_BEZEL);
  graphics_fill_circle(ctx, screen_center, screen_r);
  graphics_context_set_fill_color(ctx, COLOR_DIAL);
  graphics_fill_circle(ctx, screen_center, screen_r - 4);
  graphics_context_set_stroke_color(ctx, COLOR_BEZEL_DARK);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, screen_center, screen_r - 3);
#elif defined(PBL_PLATFORM_APLITE)
  // No bezel on Aplite — fill entire screen with dial color
  graphics_context_set_fill_color(ctx, COLOR_DIAL);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#else
  graphics_context_set_fill_color(ctx, COLOR_BEZEL);
  graphics_fill_rect(ctx, bounds, 8, GCornersAll);
  GRect dial = GRect(4, 4, w - 8, h - 8);
  graphics_context_set_fill_color(ctx, COLOR_DIAL);
  graphics_fill_rect(ctx, dial, 5, GCornersAll);
  graphics_context_set_stroke_color(ctx, COLOR_BEZEL_DARK);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, GRect(3, 3, w - 6, h - 6), 6);
#endif
}

static void draw_shimmer(GContext *ctx, GPoint center) {
#ifdef PBL_COLOR
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, COLOR_DIAL_DARK);
  int16_t r0 = 18;
  int16_t r1 = 180;
  for (int i = 0; i < 72; i++) {
    int32_t angle = TRIG_MAX_ANGLE * i / 72;
    GPoint start = {
      .x = center.x + (int16_t)((int32_t)sin_lookup(angle) * r0 / TRIG_MAX_RATIO),
      .y = center.y - (int16_t)((int32_t)cos_lookup(angle) * r0 / TRIG_MAX_RATIO)
    };
    GPoint end = {
      .x = center.x + (int16_t)((int32_t)sin_lookup(angle) * r1 / TRIG_MAX_RATIO),
      .y = center.y - (int16_t)((int32_t)cos_lookup(angle) * r1 / TRIG_MAX_RATIO)
    };
    graphics_draw_line(ctx, start, end);
  }
#else
  // No shimmer on 1-bit Aplite — skipped to save CPU
  (void)ctx;
  (void)center;
#endif
}

// ============================================================
// 7-segment LCD display
// ============================================================

static const uint8_t SEG_MAP[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static void draw_7seg(GContext *ctx, int16_t x, int16_t y,
                      int digit, int16_t dw, int16_t dh, int16_t st) {
  if (digit < 0 || digit > 9) return;
  uint8_t s = SEG_MAP[digit];
  int16_t hh = dh / 2;
  int16_t g = 1;

  if (s & 0x01) graphics_fill_rect(ctx, GRect(x+st+g, y+2,        dw-2*st-2*g, st),      0, GCornerNone);
  if (s & 0x02) graphics_fill_rect(ctx, GRect(x+dw-st, y+st+g,   st, hh-st-2*g),         0, GCornerNone);
  if (s & 0x04) graphics_fill_rect(ctx, GRect(x+dw-st, y+hh+g,   st, hh-st-2*g),         0, GCornerNone);
  if (s & 0x08) graphics_fill_rect(ctx, GRect(x+st+g, y+dh-st-2, dw-2*st-2*g, st),       0, GCornerNone);
  if (s & 0x10) graphics_fill_rect(ctx, GRect(x,       y+hh+g,   st, hh-st-2*g),         0, GCornerNone);
  if (s & 0x20) graphics_fill_rect(ctx, GRect(x,       y+st+g,   st, hh-st-2*g),         0, GCornerNone);
  if (s & 0x40) graphics_fill_rect(ctx, GRect(x+st+g, y+hh-st/2, dw-2*st-2*g, st),       0, GCornerNone);
}

static void draw_digital_time(GContext *ctx, GRect lcd) {
#ifdef PBL_PLATFORM_APLITE
  int16_t dw = 14;
  int16_t dh = 22;
  int16_t st = 2;
  int16_t sp = 2;
  int16_t col_w = 6;
#else
  int16_t dw = 18;
  int16_t dh = 28;
  int16_t st = 3;
  int16_t sp = 3;
  int16_t col_w = 8;
#endif

  int16_t dy = lcd.origin.y + (lcd.size.h - dh) / 2;

  graphics_context_set_fill_color(ctx, COLOR_LCD_TEXT);

  int16_t dx = lcd.origin.x + 3;

  if (s_animating) {
    // During sweep animation: show 88:88
    draw_7seg(ctx, dx, dy, 8, dw, dh, st);
    dx += dw + sp;
    draw_7seg(ctx, dx, dy, 8, dw, dh, st);
    dx += dw + sp;

    int16_t dot = st;
    int16_t col_x = dx + col_w / 2 - dot / 2 - 2;
    graphics_fill_rect(ctx, GRect(col_x, dy + dh / 4 - dot / 2, dot, dot), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(col_x, dy + 3 * dh / 4 - dot / 2, dot, dot), 0, GCornerNone);
    dx += col_w;

    draw_7seg(ctx, dx, dy, 8, dw, dh, st);
    dx += dw + sp;
    draw_7seg(ctx, dx, dy, 8, dw, dh, st);
  } else {
    // Animation done: show real time
    int hour = s_time.tm_hour;
    if (!clock_is_24h_style()) {
      hour = hour % 12;
      if (hour == 0) hour = 12;
    }

    int h1 = hour / 10;
    if (clock_is_24h_style() || h1 > 0)
      draw_7seg(ctx, dx, dy, h1, dw, dh, st);
    dx += dw + sp;

    draw_7seg(ctx, dx, dy, hour % 10, dw, dh, st);
    dx += dw + sp;

    int16_t dot = st;
    int16_t col_x = dx + col_w / 2 - dot / 2 - 2;
    graphics_fill_rect(ctx, GRect(col_x, dy + dh / 4 - dot / 2, dot, dot), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(col_x, dy + 3 * dh / 4 - dot / 2, dot, dot), 0, GCornerNone);
    dx += col_w;

    draw_7seg(ctx, dx, dy, s_time.tm_min / 10, dw, dh, st);
    dx += dw + sp;

    draw_7seg(ctx, dx, dy, s_time.tm_min % 10, dw, dh, st);
  }
}

static void draw_lcd(GContext *ctx, GRect bounds) {
  GRect lcd = get_lcd_rect(bounds);
  graphics_context_set_fill_color(ctx, COLOR_LCD_BG);
  graphics_fill_rect(ctx, lcd, 4, GCornersAll);
  graphics_context_set_stroke_color(ctx, COLOR_BEZEL_DARK);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, lcd, 4);
}

static void draw_markers(GContext *ctx, GPoint center,
                         int16_t hw, int16_t hh_top,
                         int16_t hh_bot, int16_t cr) {
  for (int i = 0; i < 60; i++) {
    int32_t angle = TRIG_MAX_ANGLE * i / 60;
    int16_t outer_r = rounded_rect_r(angle, hw, hh_top, hh_bot, cr);
    int16_t inset;
    int16_t stroke_w;
    GColor color;

    if (i == 0) {
      int16_t outer_12 = rounded_rect_r(angle, hw, hh_top, hh_bot, cr);
#ifdef PBL_PLATFORM_APLITE
      int16_t inner_12 = outer_12 - 12;
      int16_t gap = 2;
#else
      int16_t inner_12 = outer_12 - 18;
      int16_t gap = 3;
#endif
      int32_t px_dir = cos_lookup(angle);
      int32_t py_dir = sin_lookup(angle);
      for (int side = -1; side <= 1; side += 2) {
        GPoint o = {
          .x = center.x + (int16_t)((int32_t)sin_lookup(angle) * outer_12 / TRIG_MAX_RATIO)
                        + (int16_t)(px_dir * side * gap / TRIG_MAX_RATIO),
          .y = center.y - (int16_t)((int32_t)cos_lookup(angle) * outer_12 / TRIG_MAX_RATIO)
                        + (int16_t)(py_dir * side * gap / TRIG_MAX_RATIO)
        };
        GPoint in = {
          .x = center.x + (int16_t)((int32_t)sin_lookup(angle) * inner_12 / TRIG_MAX_RATIO)
                        + (int16_t)(px_dir * side * gap / TRIG_MAX_RATIO),
          .y = center.y - (int16_t)((int32_t)cos_lookup(angle) * inner_12 / TRIG_MAX_RATIO)
                        + (int16_t)(py_dir * side * gap / TRIG_MAX_RATIO)
        };
        graphics_context_set_stroke_color(ctx, COLOR_MARKER);
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_line(ctx, in, o);
      }
      continue;
    } else if (i % 15 == 0) {
#ifdef PBL_PLATFORM_APLITE
      inset = 12; stroke_w = 3;
#else
      inset = 18; stroke_w = 3;
#endif
      color = COLOR_MARKER;
    } else if (i % 5 == 0) {
#ifdef PBL_PLATFORM_APLITE
      inset = 9; stroke_w = 2;
#else
      inset = 13; stroke_w = 2;
#endif
      color = COLOR_MARKER;
    } else {
#ifdef PBL_PLATFORM_APLITE
      continue; // No peripheral dot bezel on Aplite
#else
      inset = 5; stroke_w = 1;
      color = COLOR_MARKER_DIM;
#endif
    }

    int16_t inner_r = outer_r - inset;

    GPoint outer = {
      .x = center.x + (int16_t)((int32_t)sin_lookup(angle) * outer_r / TRIG_MAX_RATIO),
      .y = center.y - (int16_t)((int32_t)cos_lookup(angle) * outer_r / TRIG_MAX_RATIO)
    };
    GPoint inner = {
      .x = center.x + (int16_t)((int32_t)sin_lookup(angle) * inner_r / TRIG_MAX_RATIO),
      .y = center.y - (int16_t)((int32_t)cos_lookup(angle) * inner_r / TRIG_MAX_RATIO)
    };

    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, stroke_w);
    graphics_draw_line(ctx, inner, outer);
  }
}

static void draw_labels(GContext *ctx, GPoint center) {
  graphics_context_set_text_color(ctx, COLOR_TEXT);

#ifdef PBL_ROUND
  int16_t pebble_y = center.y - 77;
  int16_t vibez_y  = center.y - 50;
  int16_t wr_offset = 20;
  int16_t badge_offset = 36;
#elif defined(PBL_PLATFORM_APLITE)
  // Aplite: tighter spacing for 168px height
  int16_t pebble_y = center.y - 44;
  int16_t vibez_y  = center.y - 24;
  int16_t wr_offset = 22;
  int16_t badge_offset = 37;
#else
  int16_t pebble_y = center.y - 55;
  int16_t vibez_y  = center.y - 28;
  int16_t wr_offset = 30;
  int16_t badge_offset = 46;
#endif

  GRect r1 = GRect(center.x - 55, pebble_y, 110, 28);
  graphics_draw_text(ctx, "PEBBLE",
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    r1, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  GRect r2 = GRect(center.x - 45, vibez_y, 90, 16);
  graphics_draw_text(ctx, "VIBEZ",
    fonts_get_system_font(FONT_KEY_GOTHIC_14),
    r2, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  GRect r3 = GRect(center.x - 50, center.y + wr_offset - 1, 100, 16);
  graphics_draw_text(ctx, "WATER RESIST",
    fonts_get_system_font(FONT_KEY_GOTHIC_14),
    r3, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  int16_t bw = 24, bh = 15;
  GRect badge = GRect(center.x - bw / 2, center.y + badge_offset, bw, bh);
  graphics_context_set_stroke_color(ctx, COLOR_TEXT);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, badge, 3);

  static char wr_day_buf[3];
  if (s_animating) {
    snprintf(wr_day_buf, sizeof(wr_day_buf), "--");
  } else {
    snprintf(wr_day_buf, sizeof(wr_day_buf), "%02d", s_time.tm_mday);
  }
  GRect r4 = GRect(center.x - bw / 2, center.y + badge_offset - 2, bw, bh);
  graphics_draw_text(ctx, wr_day_buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_14),
    r4, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Ease-out cubic: t in [0,1] -> smooth deceleration
static int32_t ease_out(int32_t t_num, int32_t t_den) {
  // f(t) = 1 - (1-t)^3  — scaled to avoid floats
  // Use 16-bit fixed-point internally
  int32_t t = t_num * 1024 / t_den;           // t in [0, 1024]
  int32_t inv = 1024 - t;                      // (1-t) in [0, 1024]
  int32_t inv3 = inv * inv / 1024 * inv / 1024; // (1-t)^3 in [0, 1024]
  return 1024 - inv3;                           // result in [0, 1024]
}

static void draw_hands_with_angles(GContext *ctx, GPoint center,
                                   int32_t h_angle, int32_t m_angle) {
#ifdef PBL_PLATFORM_APLITE
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
#endif

  gpath_rotate_to(s_hour_path, h_angle);
  gpath_move_to(s_hour_path, center);
  graphics_context_set_fill_color(ctx, COLOR_HAND);
  graphics_context_set_stroke_color(ctx, COLOR_HAND);
#ifdef PBL_PLATFORM_APLITE
  graphics_context_set_stroke_width(ctx, 2);
#else
  graphics_context_set_stroke_width(ctx, 1);
#endif
  gpath_draw_filled(ctx, s_hour_path);
  gpath_draw_outline(ctx, s_hour_path);

  gpath_rotate_to(s_min_path, m_angle);
  gpath_move_to(s_min_path, center);
  graphics_context_set_fill_color(ctx, COLOR_HAND);
  graphics_context_set_stroke_color(ctx, COLOR_HAND);
#ifdef PBL_PLATFORM_APLITE
  graphics_context_set_stroke_width(ctx, 2);
#else
  graphics_context_set_stroke_width(ctx, 1);
#endif
  gpath_draw_filled(ctx, s_min_path);
  gpath_draw_outline(ctx, s_min_path);

  // Center pinion
#ifdef PBL_PLATFORM_APLITE
  graphics_context_set_fill_color(ctx, COLOR_CENTER);
  graphics_fill_circle(ctx, center, 4);
  graphics_context_set_fill_color(ctx, COLOR_CENTER_DOT);
  graphics_fill_circle(ctx, center, 2);
#else
  graphics_context_set_fill_color(ctx, COLOR_CENTER);
  graphics_fill_circle(ctx, center, 5);
  graphics_context_set_fill_color(ctx, COLOR_CENTER_DOT);
  graphics_fill_circle(ctx, center, 2);
#endif
}

static void draw_hands(GContext *ctx, GPoint center) {
  // Compute the true target angles from current time
  int32_t target_h = TRIG_MAX_ANGLE * (s_time.tm_hour % 12) / 12
                   + TRIG_MAX_ANGLE * s_time.tm_min / (12 * 60);
  int32_t target_m = TRIG_MAX_ANGLE * s_time.tm_min / 60;

  if (s_animating) {
    uint32_t elapsed = s_anim_elapsed;
    if (elapsed > ANIM_DURATION_MS) elapsed = ANIM_DURATION_MS;

    int32_t factor = ease_out((int32_t)elapsed, ANIM_DURATION_MS); // [0, 1024]

    // Interpolate from 0 (12 o'clock) to target
    int32_t h_angle = target_h * factor / 1024;
    int32_t m_angle = target_m * factor / 1024;

    draw_hands_with_angles(ctx, center, h_angle, m_angle);
  } else {
    draw_hands_with_angles(ctx, center, target_h, target_m);
  }
}

// ============================================================
// Intro animation timer
// ============================================================

static void anim_timer_callback(void *context) {
  s_anim_elapsed += ANIM_INTERVAL_MS;

  if (s_anim_elapsed >= ANIM_DURATION_MS) {
    // Animation complete — reveal real time on LCD and day label
    s_animating = false;
    s_anim_timer = NULL;
    update_time();  // refreshes day text layer now that s_animating is false
  } else {
    // Schedule next frame
    s_anim_timer = app_timer_register(ANIM_INTERVAL_MS,
                                      anim_timer_callback, NULL);
  }

  layer_mark_dirty(s_canvas);
}

static void start_intro_animation(void) {
  // Cancel any running animation
  if (s_anim_timer) {
    app_timer_cancel(s_anim_timer);
    s_anim_timer = NULL;
  }
  s_anim_elapsed = 0;
  s_animating = true;
  s_anim_timer = app_timer_register(ANIM_INTERVAL_MS,
                                    anim_timer_callback, NULL);
}

// ============================================================
// Canvas update
// ============================================================

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center;
  int16_t hw, hh_top, hh_bot, cr;
  get_clock_geometry(bounds, &center, &hw, &hh_top, &hh_bot, &cr);

  draw_background(ctx, bounds);
  draw_shimmer(ctx, center);
  draw_markers(ctx, center, hw, hh_top, hh_bot, cr);
  draw_lcd(ctx, bounds);
  draw_digital_time(ctx, get_lcd_rect(bounds));
  draw_labels(ctx, center);
  draw_hands(ctx, center);
}

// ============================================================
// Time handling
// ============================================================

static void update_time(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  s_time = *t;

  static char day_buf[4];
  if (s_animating) {
    // Hide real day during sweep animation (3 dashes = 3-letter day name)
    text_layer_set_text(s_day_layer, "---");
  } else {
    strftime(day_buf, sizeof(day_buf), "%a", t);
    for (int i = 0; day_buf[i]; i++) {
      if (day_buf[i] >= 'a' && day_buf[i] <= 'z') day_buf[i] -= 32;
    }
    text_layer_set_text(s_day_layer, day_buf);
  }

  layer_mark_dirty(s_canvas);
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  update_time();
}

// ============================================================
// Window handlers
// ============================================================

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);

  s_hour_path = gpath_create(&HOUR_HAND_INFO);
  s_min_path = gpath_create(&MIN_HAND_INFO);

  GRect lcd = get_lcd_rect(bounds);

#ifdef PBL_PLATFORM_APLITE
  int16_t day_font_h = 16;
#else
  int16_t day_font_h = 20;
#endif
  int16_t day_y = lcd.origin.y + (lcd.size.h - day_font_h) / 2 - 1;
#ifdef PBL_PLATFORM_APLITE
  int16_t day_x = lcd.origin.x + lcd.size.w - 32;
#else
  int16_t day_x = lcd.origin.x + lcd.size.w - 31;
#endif

  s_day_layer = text_layer_create(GRect(day_x, day_y, 34, day_font_h));
  text_layer_set_background_color(s_day_layer, GColorClear);
  text_layer_set_text_color(s_day_layer, COLOR_LCD_TEXT);
#ifdef PBL_PLATFORM_APLITE
  text_layer_set_font(s_day_layer,
    fonts_get_system_font(FONT_KEY_GOTHIC_14));
#else
  text_layer_set_font(s_day_layer,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
#endif
  text_layer_set_text_alignment(s_day_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_day_layer));

  // MINUTE_UNIT: no second hand on Aplite, saves battery
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  // Set animating before update_time so the very first draw shows
  // placeholder text immediately — no real values flash onscreen.
  s_animating = true;
  update_time();

  // Play intro sweep animation: hands start at 12:00 → current time
  start_intro_animation();
}

// ============================================================
// Focus handlers — split across will_focus and did_focus so that
// placeholder content is in place BEFORE the slide-back animation
// begins, and the sweep starts only once the face is fully visible.
//
// Timeline when returning from a notification / system overlay:
//   1. will_focus(true)  — slide-back animation STARTS (face begins to appear)
//   2. did_focus(true)   — slide-back animation ENDS   (face fully visible)
//
// Using only did_focus means the watchface slides in showing real
// time/date before we ever get a chance to set placeholders — that
// is the flash the user sees.  By arming s_animating in will_focus we
// guarantee the very first pixel of the slide-in already shows 88:88
// and "---", with zero real-value exposure.
// ============================================================

static void app_will_focus_handler(bool in_focus) {
  if (in_focus) {
    // The slide-back animation is about to start.  Lock in full
    // placeholder state — including resetting the elapsed counter to 0
    // so draw_hands() computes factor=0 and places hands at 12:00 —
    // before the system composites a single frame of the slide-in.
    if (s_anim_timer) {
      app_timer_cancel(s_anim_timer);
      s_anim_timer = NULL;
    }
    s_anim_elapsed = 0;
    s_animating = true;
    update_time();  // writes "---" to day layer, syncs s_time, dirty-marks canvas
  }
}

static void app_did_focus_handler(bool in_focus) {
  if (in_focus) {
    // Face is now fully visible — begin the sweep animation from 12:00.
    start_intro_animation();
  }
}

static void window_unload(Window *window) {
  if (s_anim_timer) {
    app_timer_cancel(s_anim_timer);
    s_anim_timer = NULL;
  }
  tick_timer_service_unsubscribe();
  text_layer_destroy(s_day_layer);
  gpath_destroy(s_hour_path);
  gpath_destroy(s_min_path);
  layer_destroy(s_canvas);
}

// ============================================================
// App lifecycle
// ============================================================

static void init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load   = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);
  app_focus_service_subscribe_handlers((AppFocusHandlers) {
    .will_focus = app_will_focus_handler,
    .did_focus  = app_did_focus_handler
  });
}

static void deinit(void) {
  app_focus_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
