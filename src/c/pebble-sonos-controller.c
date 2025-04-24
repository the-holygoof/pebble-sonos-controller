#include <pebble.h>
#include <ctype.h>

enum MessageKeys { KEY_CMD_APP_READY=0, KEY_CMD_PLAY=1, KEY_CMD_PAUSE=2, KEY_CMD_STOP=3, KEY_CMD_VOL_UP=4, KEY_CMD_VOL_DOWN=5, KEY_CMD_PREV_TRACK=6, KEY_CMD_NEXT_TRACK=7, KEY_CMD_GET_STATUS=8, KEY_JS_READY=9, KEY_STATUS_PLAY_STATE=10, KEY_STATUS_VOLUME=11, KEY_STATUS_MUTE_STATE=12, KEY_STATUS_ERROR_MSG=13, KEY_CONFIG_IP_ADDRESS=14, KEY_STATUS_TRACK_TITLE=15, KEY_STATUS_ARTIST_NAME=16, KEY_STATUS_ALBUM_NAME=17 };
enum PlayStates { STATE_STOPPED=0, STATE_PLAYING=1, STATE_PAUSED=2, STATE_TRANSITIONING=3, STATE_ERROR=4, STATE_UNKNOWN=5 };
enum UpDownButtonMode { MODE_VOLUME=0, MODE_TRACK=1 };
enum BottomAreaMode { DISPLAY_TRACK=0, DISPLAY_VOLUME=1 };

static Window *s_main_window;
static TextLayer *s_time_layer, *s_title_layer, *s_artist_layer, *s_album_layer, *s_status_text_layer, *s_volume_text_layer;
static Layer *s_progress_layer;
static ActionBarLayer *s_action_bar_layer;
static GBitmap *s_icon_play, *s_icon_pause, *s_icon_vol_up, *s_icon_vol_down, *s_icon_prev_track, *s_icon_next_track;

static enum PlayStates s_current_play_state = STATE_UNKNOWN;
static int s_current_volume = -1;
static bool s_current_mute_state = false;
static char s_title_buffer[32], s_artist_buffer[32], s_album_buffer[32], s_status_text_buffer[64];
static AppTimer *s_status_update_timer, *s_volume_display_revert_timer, *s_ui_update_timer = NULL;
static AppTimer *s_mode_revert_timer = NULL;
static enum UpDownButtonMode s_up_down_button_mode = MODE_VOLUME;
static enum BottomAreaMode s_bottom_area_mode = DISPLAY_TRACK;

static void send_cmd(uint8_t key);
static void request_status_update();
static void update_time_layer(struct tm *tick_time);
static void update_status_text_layer();
static void update_track_display();
static void update_volume_display();
static void update_progress_layer();
static void update_action_bar_icons();
static void select_click_handler(ClickRecognizerRef recognizer, void *context);
static void up_click_handler(ClickRecognizerRef recognizer, void *context);
static void down_click_handler(ClickRecognizerRef recognizer, void *context);
static void select_long_click_handler(ClickRecognizerRef recognizer, void *context);
static void click_config_provider(void *context);
static void status_update_timer_callback(void *data);
static void start_status_updates();
static void stop_status_updates();
static void volume_display_revert_timer_callback(void *data);
static void start_volume_display_revert_timer();
static void progress_layer_update_proc(Layer *layer, GContext *ctx);
static void inbox_received_callback(DictionaryIterator *iterator, void *context);
static void inbox_dropped_callback(AppMessageResult reason, void *context);
static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context);
static void outbox_sent_callback(DictionaryIterator *iterator, void *context);
static void tick_handler(struct tm *tick_time, TimeUnits units_changed);
static bool are_ui_components_valid();
static void schedule_ui_update();
static void delayed_ui_update_callback(void *context);
static void main_window_load(Window *window);
static void main_window_unload(Window *window);
static void init();
static void deinit();
static void app_ready_timer_callback(void *data);
static char* app_message_result_to_string(AppMessageResult result);
static void mode_revert_timer_callback(void *data);

static void send_cmd(uint8_t key) {
    DictionaryIterator *iter;
    AppMessageResult result = app_message_outbox_begin(&iter);
    if (result != APP_MSG_OK) { APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to begin outbox: %d", (int)result); return; }
    if (!iter) { APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox iter is NULL after successful begin?"); return; }
    dict_write_uint8(iter, key, 1);
    dict_write_end(iter);
    result = app_message_outbox_send();
    if (result != APP_MSG_OK) { APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to send cmd %d: %s (%d)", key, app_message_result_to_string(result), (int)result); }
}
static void request_status_update() { send_cmd(KEY_CMD_GET_STATUS); }

static bool are_ui_components_valid() {
  if (!s_main_window || !window_is_loaded(s_main_window)) { return false; }
  if (!s_time_layer || !s_title_layer || !s_artist_layer || !s_album_layer || !s_status_text_layer || !s_volume_text_layer || !s_progress_layer || !s_action_bar_layer) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "are_ui_components_valid: One or more layers are NULL");
      return false;
  }
  return true;
}

static void delayed_ui_update_callback(void *context) {
    s_ui_update_timer = NULL;
    if (!are_ui_components_valid()) { APP_LOG(APP_LOG_LEVEL_ERROR, "Not updating UI due to invalid components"); return; }
    update_action_bar_icons();
    update_volume_display();
    update_progress_layer();
    update_track_display();
    update_status_text_layer();
}

static void schedule_ui_update() {
    if (s_ui_update_timer) { app_timer_reschedule(s_ui_update_timer, 50); return; }
    s_ui_update_timer = app_timer_register(50, delayed_ui_update_callback, NULL);
    if (!s_ui_update_timer) { APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to schedule UI update timer!"); }
}
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  if (!iterator) { return; }
  bool state_changed = false;
  char received_error_msg[64] = "";
  bool error_received_this_time = false;

  Tuple *t = dict_read_first(iterator);
  while (t != NULL) {
    switch (t->key) {
      case KEY_JS_READY:
        request_status_update();
        break;
      case KEY_STATUS_PLAY_STATE:
        if (s_current_play_state != (enum PlayStates)t->value->int32) {
          s_current_play_state = (enum PlayStates)t->value->int32;
          state_changed = true;
        }
        break;
      case KEY_STATUS_VOLUME:
        if (s_current_volume != t->value->int32) {
          s_current_volume = t->value->int32;
          state_changed = true;
        }
        break;
      case KEY_STATUS_MUTE_STATE:
        if (s_current_mute_state != (bool)t->value->int32) {
          s_current_mute_state = (bool)t->value->int32;
          state_changed = true;
        }
        break;
      case KEY_CONFIG_IP_ADDRESS:
        // Just acknowledge we received the IP, no need to do anything else
        APP_LOG(APP_LOG_LEVEL_INFO, "Received IP address configuration");
        break;
      case KEY_STATUS_ERROR_MSG:
        if (t->type == TUPLE_CSTRING) {
          strncpy(received_error_msg, t->value->cstring, sizeof(received_error_msg) - 1);
          received_error_msg[sizeof(received_error_msg) - 1] = '\0';
          error_received_this_time = true;
        }
        if (s_current_play_state != STATE_ERROR) {
          s_current_play_state = STATE_ERROR;
          state_changed = true;
        }
        break;
      case KEY_STATUS_TRACK_TITLE:
        if (t->type == TUPLE_CSTRING) {
          if (strcmp(s_title_buffer, t->value->cstring) != 0) {
            strncpy(s_title_buffer, t->value->cstring, sizeof(s_title_buffer) - 1);
            s_title_buffer[sizeof(s_title_buffer) - 1] = '\0';
            state_changed = true;
          }
        }
        break;
      case KEY_STATUS_ARTIST_NAME:
        if (t->type == TUPLE_CSTRING) {
          if (strcmp(s_artist_buffer, t->value->cstring) != 0) {
            strncpy(s_artist_buffer, t->value->cstring, sizeof(s_artist_buffer) - 1);
            s_artist_buffer[sizeof(s_artist_buffer) - 1] = '\0';
            state_changed = true;
          }
        }
        break;
      case KEY_STATUS_ALBUM_NAME:
        if (t->type == TUPLE_CSTRING) {
          if (strcmp(s_album_buffer, t->value->cstring) != 0) {
            strncpy(s_album_buffer, t->value->cstring, sizeof(s_album_buffer) - 1);
            s_album_buffer[sizeof(s_album_buffer) - 1] = '\0';
            state_changed = true;
          }
        }
        break;
      default:
        APP_LOG(APP_LOG_LEVEL_WARNING, "Unexpected key received: %d", (int)t->key);
        break;
    }
    t = dict_read_next(iterator);
  }

  if (error_received_this_time) {
    strncpy(s_status_text_buffer, received_error_msg, sizeof(s_status_text_buffer));
    state_changed = true;
  } else if (state_changed) {
    s_status_text_buffer[0] = '\0';
  }

  if (state_changed) {
    schedule_ui_update();
  }
}

static char* app_message_result_to_string(AppMessageResult result) {
  switch (result) {
    case APP_MSG_OK: return "OK"; case APP_MSG_SEND_TIMEOUT: return "Send Timeout"; case APP_MSG_SEND_REJECTED: return "Send Rejected";
    case APP_MSG_NOT_CONNECTED: return "Not Connected"; case APP_MSG_APP_NOT_RUNNING: return "App Not Running"; case APP_MSG_INVALID_ARGS: return "Invalid Args";
    case APP_MSG_BUSY: return "Busy"; case APP_MSG_BUFFER_OVERFLOW: return "Buffer Overflow"; case APP_MSG_ALREADY_RELEASED: return "Already Released";
    case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "Callback Registered"; case APP_MSG_CALLBACK_NOT_REGISTERED: return "Callback Not Registered";
    case APP_MSG_OUT_OF_MEMORY: return "Out of Memory"; case APP_MSG_CLOSED: return "Closed"; case APP_MSG_INTERNAL_ERROR: return "Internal Error";
    default: return "Unknown Error";
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) { s_current_play_state = STATE_ERROR; snprintf(s_status_text_buffer, sizeof(s_status_text_buffer), "Error: RX Drop %d", (int)reason); schedule_ui_update(); }
static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) { s_current_play_state = STATE_ERROR; snprintf(s_status_text_buffer, sizeof(s_status_text_buffer), "Error: TX Fail %s", app_message_result_to_string(reason)); schedule_ui_update(); }
static void outbox_sent_callback(DictionaryIterator *iterator, void *context) { }

static void update_time_layer(struct tm *tick_time) { if (!s_time_layer) return; static char time_buffer[] = "00:00"; if (clock_is_24h_style() == true) { strftime(time_buffer, sizeof(time_buffer), "%H:%M", tick_time); } else { strftime(time_buffer, sizeof(time_buffer), "%I:%M", tick_time); } text_layer_set_text(s_time_layer, time_buffer); }
static void update_status_text_layer() {
    if (!s_status_text_layer) return; 
    bool show_status = false;
    if (s_current_play_state == STATE_ERROR) { if (strlen(s_status_text_buffer) == 0) { snprintf(s_status_text_buffer, sizeof(s_status_text_buffer), "Error"); } show_status = true; }
    else if (s_current_play_state == STATE_STOPPED) { snprintf(s_status_text_buffer, sizeof(s_status_text_buffer), "Nothing playing"); show_status = true; }
    else if (s_current_play_state == STATE_UNKNOWN) { snprintf(s_status_text_buffer, sizeof(s_status_text_buffer), "Connecting..."); show_status = true; }
    layer_set_hidden(text_layer_get_layer(s_status_text_layer), !show_status); if (show_status) { text_layer_set_text(s_status_text_layer, s_status_text_buffer); }
}
static void update_track_display() {
  bool show_track = (s_current_play_state == STATE_PLAYING || s_current_play_state == STATE_PAUSED);

  if (s_title_layer) {
    layer_set_hidden(text_layer_get_layer(s_title_layer), !show_track);
    if (show_track) {
      text_layer_set_text(s_title_layer, s_title_buffer[0] != '\0' ? s_title_buffer : "Unknown Title");
    }
  }

  if (s_artist_layer) {
    layer_set_hidden(text_layer_get_layer(s_artist_layer), !show_track);
    if (show_track) {
      text_layer_set_text(s_artist_layer, s_artist_buffer[0] != '\0' ? s_artist_buffer : "Unknown Artist");
    }
  }

  if (s_album_layer) {
    layer_set_hidden(text_layer_get_layer(s_album_layer), !show_track);
    if (show_track) {
      text_layer_set_text(s_album_layer, s_album_buffer[0] != '\0' ? s_album_buffer : "Unknown Album");
    }
  }
}
static void update_volume_display() { if (s_volume_text_layer) { layer_set_hidden(text_layer_get_layer(s_volume_text_layer), s_bottom_area_mode != DISPLAY_VOLUME); } }
static void update_progress_layer() { if (!s_progress_layer) return; layer_mark_dirty(s_progress_layer); }
static void update_action_bar_icons() { 
  if (!s_action_bar_layer) { return; } 
  if (s_icon_pause && s_icon_play) { 
    action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_SELECT, (s_current_play_state == STATE_PLAYING || s_current_play_state == STATE_TRANSITIONING) ? s_icon_pause : s_icon_play); 
  } 
  if (s_up_down_button_mode == MODE_VOLUME) { 
    if (s_icon_vol_up) { 
      action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_UP, s_icon_vol_up); 
    } 
    if (s_icon_vol_down) { 
      action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_DOWN, s_icon_vol_down); 
    } 
  } else { 
    if (s_icon_next_track) { 
      action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_UP, s_icon_next_track); 
    } 
    if (s_icon_prev_track) { 
      action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_DOWN, s_icon_prev_track); 
    } 
  } 
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_current_play_state == STATE_PLAYING || s_current_play_state == STATE_TRANSITIONING) {
    // Update UI immediately for better responsiveness
    s_current_play_state = STATE_PAUSED;
    schedule_ui_update();
    
    send_cmd(KEY_CMD_PAUSE);
    vibes_enqueue_custom_pattern((VibePattern){ .durations = (uint32_t []) {50}, .num_segments = 1 });
  } else if (s_current_play_state == STATE_PAUSED || s_current_play_state == STATE_STOPPED) {
    // Update UI immediately for better responsiveness
    s_current_play_state = STATE_TRANSITIONING;
    schedule_ui_update();
    
    send_cmd(KEY_CMD_PLAY);
    vibes_enqueue_custom_pattern((VibePattern){ .durations = (uint32_t []) {50}, .num_segments = 1 });
  }
}

#define MODE_REVERT_TIMEOUT_MS (5 * 1000) // 5 seconds

static void mode_revert_timer_callback(void *data) {
  s_mode_revert_timer = NULL;
  if (s_up_down_button_mode == MODE_TRACK) {
    s_up_down_button_mode = MODE_VOLUME;
    update_action_bar_icons();
    vibes_short_pulse(); // Optional feedback
  }
}

static void select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_up_down_button_mode = (s_up_down_button_mode == MODE_VOLUME) ? MODE_TRACK : MODE_VOLUME;
  update_action_bar_icons();
  vibes_enqueue_custom_pattern((VibePattern){ .durations = (uint32_t []) {50}, .num_segments = 1 });
  
  // Cancel any existing timer
  if (s_mode_revert_timer) {
    app_timer_cancel(s_mode_revert_timer);
    s_mode_revert_timer = NULL;
  }
  
  // If we switched to track mode, start a timer to revert back
  if (s_up_down_button_mode == MODE_TRACK) {
    s_mode_revert_timer = app_timer_register(MODE_REVERT_TIMEOUT_MS, mode_revert_timer_callback, NULL);
  }
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) { 
  if (!s_progress_layer || !s_volume_text_layer) return; 
  if (s_up_down_button_mode == MODE_VOLUME) { 
    send_cmd(KEY_CMD_VOL_UP); 
    s_bottom_area_mode = DISPLAY_VOLUME; 
    update_volume_display(); 
    update_progress_layer(); 
    start_volume_display_revert_timer(); 
  } else { 
    send_cmd(KEY_CMD_NEXT_TRACK); 
  } 
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) { 
  if (!s_progress_layer || !s_volume_text_layer) return; 
  if (s_up_down_button_mode == MODE_VOLUME) { 
    send_cmd(KEY_CMD_VOL_DOWN); 
    s_bottom_area_mode = DISPLAY_VOLUME; 
    update_volume_display(); 
    update_progress_layer(); 
    start_volume_display_revert_timer(); 
  } else { 
    send_cmd(KEY_CMD_PREV_TRACK); 
  } 
}
static void click_config_provider(void *context) { window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler); window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long_click_handler, NULL); window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, up_click_handler); window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, down_click_handler); }

#define STATUS_UPDATE_INTERVAL_MS (5 * 1000)
static void status_update_timer_callback(void *data) { request_status_update(); s_status_update_timer = app_timer_register(STATUS_UPDATE_INTERVAL_MS, status_update_timer_callback, NULL); }
static void start_status_updates() { if (s_status_update_timer) { app_timer_cancel(s_status_update_timer); s_status_update_timer = NULL; } request_status_update(); s_status_update_timer = app_timer_register(STATUS_UPDATE_INTERVAL_MS, status_update_timer_callback, NULL); }
static void stop_status_updates() { if (s_status_update_timer) { app_timer_cancel(s_status_update_timer); s_status_update_timer = NULL; } }

#define VOLUME_DISPLAY_TIMEOUT_MS (3 * 1000)
static void volume_display_revert_timer_callback(void *data) { s_volume_display_revert_timer = NULL; if (s_bottom_area_mode == DISPLAY_VOLUME) { s_bottom_area_mode = DISPLAY_TRACK; update_volume_display(); update_progress_layer(); } }
static void start_volume_display_revert_timer() { if (s_volume_display_revert_timer) { app_timer_reschedule(s_volume_display_revert_timer, VOLUME_DISPLAY_TIMEOUT_MS); } else { s_volume_display_revert_timer = app_timer_register(VOLUME_DISPLAY_TIMEOUT_MS, volume_display_revert_timer_callback, NULL); } }

#define VOLUME_BAR_HEIGHT 6
static void progress_layer_update_proc(Layer *layer, GContext *ctx) {
    if (!layer || !ctx || !layer_get_window(layer)) { return; }
    GRect bounds = layer_get_bounds(layer);
    if (bounds.size.w <= 0 || bounds.size.h <= 0) { return; }
    if (s_bottom_area_mode == DISPLAY_VOLUME) {
        graphics_context_set_fill_color(ctx, GColorDarkGray);
        graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, VOLUME_BAR_HEIGHT), 0, GCornerNone);
        if (s_current_volume >= 0 && s_current_volume <= 100) {
            int fg_width = (s_current_volume * bounds.size.w) / 100;
            if (fg_width < 0) { fg_width = 0; }
            if (fg_width > bounds.size.w) { fg_width = bounds.size.w; }
            if (fg_width > 0) {
               graphics_context_set_fill_color(ctx, GColorWhite);
               graphics_fill_rect(ctx, GRect(0, 0, fg_width, VOLUME_BAR_HEIGHT), 0, GCornerNone);
            }
        }
    } else {
        graphics_context_set_fill_color(ctx, GColorFromHEX(0xFFAA00));
        graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) { update_time_layer(tick_time); }

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer); int action_bar_width = ACTION_BAR_WIDTH > 0 ? ACTION_BAR_WIDTH : 20;
  window_set_background_color(s_main_window, GColorFromHEX(0xFFAA00));
  s_icon_play = gbitmap_create_with_resource(RESOURCE_ID_ICON_PLAY); if (!s_icon_play) APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to load ICON_PLAY");
  s_icon_pause = gbitmap_create_with_resource(RESOURCE_ID_ICON_PAUSE); if (!s_icon_pause) APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to load ICON_PAUSE");
  s_icon_vol_up = gbitmap_create_with_resource(RESOURCE_ID_ICON_VOL_UP); if (!s_icon_vol_up) APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to load ICON_VOL_UP");
  s_icon_vol_down = gbitmap_create_with_resource(RESOURCE_ID_ICON_VOL_DOWN); if (!s_icon_vol_down) APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to load ICON_VOL_DOWN");
  s_icon_prev_track = gbitmap_create_with_resource(RESOURCE_ID_ICON_PREV_TRACK); if (!s_icon_prev_track) APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to load ICON_PREV_TRACK");
  s_icon_next_track = gbitmap_create_with_resource(RESOURCE_ID_ICON_NEXT_TRACK); if (!s_icon_next_track) APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to load ICON_NEXT_TRACK");

  int current_y = 0;
  GRect time_frame = GRect(0, current_y, bounds.size.w - action_bar_width, 18);
  s_time_layer = text_layer_create(time_frame); text_layer_set_background_color(s_time_layer, GColorClear); text_layer_set_text_color(s_time_layer, GColorBlack); text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14)); text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter); layer_add_child(window_layer, text_layer_get_layer(s_time_layer)); current_y = time_frame.origin.y + time_frame.size.h + 5;

  GRect title_frame = GRect(5, current_y, bounds.size.w - action_bar_width - 10, 36); // Increased height for wrapping
  s_title_layer = text_layer_create(title_frame); text_layer_set_background_color(s_title_layer, GColorClear); text_layer_set_text_color(s_title_layer, GColorBlack); text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD)); text_layer_set_text_alignment(s_title_layer, GTextAlignmentLeft); text_layer_set_overflow_mode(s_title_layer, GTextOverflowModeWordWrap); layer_add_child(window_layer, text_layer_get_layer(s_title_layer)); current_y = title_frame.origin.y + title_frame.size.h + 5; // Added margin

  GRect artist_frame = GRect(5, current_y, bounds.size.w - action_bar_width - 10, 22);
  s_artist_layer = text_layer_create(artist_frame); text_layer_set_background_color(s_artist_layer, GColorClear); text_layer_set_text_color(s_artist_layer, GColorBlack); text_layer_set_font(s_artist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD)); text_layer_set_text_alignment(s_artist_layer, GTextAlignmentLeft); text_layer_set_overflow_mode(s_artist_layer, GTextOverflowModeTrailingEllipsis); layer_add_child(window_layer, text_layer_get_layer(s_artist_layer)); current_y = artist_frame.origin.y + artist_frame.size.h;

  GRect album_frame = GRect(5, current_y, bounds.size.w - action_bar_width - 10, 36); 
  s_album_layer = text_layer_create(album_frame); text_layer_set_background_color(s_album_layer, GColorClear); text_layer_set_text_color(s_album_layer, GColorBlack); text_layer_set_font(s_album_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18)); text_layer_set_text_alignment(s_album_layer, GTextAlignmentLeft); text_layer_set_overflow_mode(s_album_layer, GTextOverflowModeWordWrap); layer_add_child(window_layer, text_layer_get_layer(s_album_layer)); current_y = album_frame.origin.y + album_frame.size.h + 5;

  GRect status_text_frame = GRect(5, current_y, bounds.size.w - action_bar_width - 10, 40);
  s_status_text_layer = text_layer_create(status_text_frame); text_layer_set_background_color(s_status_text_layer, GColorClear); text_layer_set_text_color(s_status_text_layer, GColorBlack); text_layer_set_font(s_status_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18)); text_layer_set_text_alignment(s_status_text_layer, GTextAlignmentCenter); layer_add_child(window_layer, text_layer_get_layer(s_status_text_layer));

  int progress_area_y = bounds.size.h - 20;
  GRect progress_frame = GRect(5, progress_area_y, bounds.size.w - action_bar_width - 10, VOLUME_BAR_HEIGHT + 2);
  s_progress_layer = layer_create(progress_frame); layer_set_update_proc(s_progress_layer, progress_layer_update_proc); layer_add_child(window_layer, s_progress_layer);

  GRect volume_text_frame = GRect(5, progress_area_y - 20, 50, 18);
  s_volume_text_layer = text_layer_create(volume_text_frame); text_layer_set_background_color(s_volume_text_layer, GColorClear); text_layer_set_text_color(s_volume_text_layer, GColorBlack); text_layer_set_font(s_volume_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14)); text_layer_set_text_alignment(s_volume_text_layer, GTextAlignmentLeft); text_layer_set_text(s_volume_text_layer, "Volume"); layer_add_child(window_layer, text_layer_get_layer(s_volume_text_layer)); layer_set_hidden(text_layer_get_layer(s_volume_text_layer), true);

  s_action_bar_layer = action_bar_layer_create(); if (!s_action_bar_layer) { APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create action bar layer"); return; }
  action_bar_layer_set_background_color(s_action_bar_layer, GColorWhite); action_bar_layer_set_icon_press_animation(s_action_bar_layer, BUTTON_ID_SELECT, ActionBarLayerIconPressAnimationNone); action_bar_layer_set_icon_press_animation(s_action_bar_layer, BUTTON_ID_UP, ActionBarLayerIconPressAnimationNone); action_bar_layer_set_icon_press_animation(s_action_bar_layer, BUTTON_ID_DOWN, ActionBarLayerIconPressAnimationNone); action_bar_layer_add_to_window(s_action_bar_layer, window); window_set_click_config_provider(s_main_window, click_config_provider);

  time_t temp = time(NULL); struct tm *tick_time = localtime(&temp); update_time_layer(tick_time); tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  snprintf(s_status_text_buffer, sizeof(s_status_text_buffer), "Connecting..."); update_status_text_layer(); update_action_bar_icons(); update_track_display(); update_progress_layer(); update_volume_display();
}

static void main_window_unload(Window *window) { 
  tick_timer_service_unsubscribe(); 
  if (s_action_bar_layer) { action_bar_layer_destroy(s_action_bar_layer); s_action_bar_layer = NULL; } 
  if (s_progress_layer) { layer_destroy(s_progress_layer); s_progress_layer = NULL; } 
  if (s_volume_text_layer) { text_layer_destroy(s_volume_text_layer); s_volume_text_layer = NULL; } 
  if (s_album_layer) { text_layer_destroy(s_album_layer); s_album_layer = NULL; } 
  if (s_artist_layer) { text_layer_destroy(s_artist_layer); s_artist_layer = NULL; } 
  if (s_title_layer) { text_layer_destroy(s_title_layer); s_title_layer = NULL; } 
  if (s_status_text_layer) { text_layer_destroy(s_status_text_layer); s_status_text_layer = NULL; } 
  if (s_time_layer) { text_layer_destroy(s_time_layer); s_time_layer = NULL; } 
  if(s_icon_play) { gbitmap_destroy(s_icon_play); s_icon_play = NULL; } 
  if(s_icon_pause) { gbitmap_destroy(s_icon_pause); s_icon_pause = NULL; } 
  if(s_icon_vol_up) { gbitmap_destroy(s_icon_vol_up); s_icon_vol_up = NULL; } 
  if(s_icon_vol_down) { gbitmap_destroy(s_icon_vol_down); s_icon_vol_down = NULL; } 
  if(s_icon_prev_track) { gbitmap_destroy(s_icon_prev_track); s_icon_prev_track = NULL; } 
  if(s_icon_next_track) { gbitmap_destroy(s_icon_next_track); s_icon_next_track = NULL; } 
  stop_status_updates(); 
  if (s_volume_display_revert_timer) { app_timer_cancel(s_volume_display_revert_timer); s_volume_display_revert_timer = NULL; } 
  if (s_ui_update_timer) { app_timer_cancel(s_ui_update_timer); s_ui_update_timer = NULL; }
  if (s_mode_revert_timer) { app_timer_cancel(s_mode_revert_timer); s_mode_revert_timer = NULL; }
}

static void app_ready_timer_callback(void *data) { uint8_t key_to_send = (uint8_t)(uintptr_t)data; send_cmd(key_to_send); }

static void init() {
  s_title_buffer[0] = '\0'; s_artist_buffer[0] = '\0'; s_album_buffer[0] = '\0'; s_status_text_buffer[0] = '\0';
  s_main_window = window_create(); if (!s_main_window) { APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create main window!"); return; }
  window_set_window_handlers(s_main_window, (WindowHandlers) { .load = main_window_load, .unload = main_window_unload, });
  app_message_register_inbox_received(inbox_received_callback); app_message_register_inbox_dropped(inbox_dropped_callback); app_message_register_outbox_failed(outbox_failed_callback); app_message_register_outbox_sent(outbox_sent_callback);
  const uint32_t inbox_size = 256; const uint32_t outbox_size = 64; AppMessageResult result = app_message_open(inbox_size, outbox_size);
  if (result != APP_MSG_OK) { APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to open AppMessage: %s (%d)", app_message_result_to_string(result), (int)result); s_current_play_state = STATE_ERROR; snprintf(s_status_text_buffer, sizeof(s_status_text_buffer), "Error: AppMsg Init"); }
  window_stack_push(s_main_window, true);
  app_timer_register(500, app_ready_timer_callback, (void*)((uintptr_t)KEY_CMD_APP_READY));
  start_status_updates();
}
static void deinit() { stop_status_updates(); if (s_volume_display_revert_timer) { app_timer_cancel(s_volume_display_revert_timer); s_volume_display_revert_timer = NULL; } if (s_ui_update_timer) { app_timer_cancel(s_ui_update_timer); s_ui_update_timer = NULL; } if (s_main_window) window_destroy(s_main_window); }
int main(void) { init(); app_event_loop(); deinit(); }