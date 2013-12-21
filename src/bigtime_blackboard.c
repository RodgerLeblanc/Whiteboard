/*
 
 Big Time watch
 
 A digital watch with large digits.
 
 
 A few things complicate the implementation of this watch:
 
 a) The largest size of the Nevis font which the Pebble handles
 seems to be ~47 units (points or pixels?). But the size of
 characters we want is ~100 points.
 
 This requires us to generate and use images instead of
 fonts--which complicates things greatly.
 
 b) When I started it wasn't possible to load all the images into
 RAM at once--this means we have to load/unload each image when
 we need it. The images are slightly smaller now than they were
 but I figured it would still be pushing it to load them all at
 once, even if they just fit, so I've stuck with the load/unload
 approach.
 
 */

#include "pebble.h"
#include "QTPlus.h"
	

static Window *window;

TextLayer *connection_layer;

//
// There's only enough memory to load about 6 of 10 required images
// so we have to swap them in & out...
//
// We have one "slot" per digit location on screen.
//
// Because layers can only have one parent we load a digit for each
// slot--even if the digit image is already in another slot.
//
// Slot on-screen layout:
//     0 1
//     2 3
//
#define TOTAL_IMAGE_SLOTS 4

#define NUMBER_OF_IMAGES 20

// These images are 72 x 84 pixels (i.e. a quarter of the display),
// black and white with the digit character centered in the image.
// (As generated by the `fonttools/font2png.py` script.)
const int IMAGE_RESOURCE_IDS[NUMBER_OF_IMAGES] = {
    RESOURCE_ID_IMAGE_NUM_0, RESOURCE_ID_IMAGE_NUM_1, RESOURCE_ID_IMAGE_NUM_2,
    RESOURCE_ID_IMAGE_NUM_3, RESOURCE_ID_IMAGE_NUM_4, RESOURCE_ID_IMAGE_NUM_5,
    RESOURCE_ID_IMAGE_NUM_6, RESOURCE_ID_IMAGE_NUM_7, RESOURCE_ID_IMAGE_NUM_8,
    RESOURCE_ID_IMAGE_NUM_9, RESOURCE_ID_IMAGE_NUM_10, RESOURCE_ID_IMAGE_NUM_11, RESOURCE_ID_IMAGE_NUM_12,
    RESOURCE_ID_IMAGE_NUM_13, RESOURCE_ID_IMAGE_NUM_14, RESOURCE_ID_IMAGE_NUM_15,
    RESOURCE_ID_IMAGE_NUM_16, RESOURCE_ID_IMAGE_NUM_17, RESOURCE_ID_IMAGE_NUM_18,
    RESOURCE_ID_IMAGE_NUM_19
};

bool BT_connection_state = true;
bool was_BTconnected_last_time = true;

int min = 61;

static GBitmap *images[TOTAL_IMAGE_SLOTS];
static BitmapLayer *image_layers[TOTAL_IMAGE_SLOTS];

#define EMPTY_SLOT -1

// The state is either "empty" or the digit of the image currently in
// the slot--which was going to be used to assist with de-duplication
// but we're not doing that due to the one parent-per-layer
// restriction mentioned above.
static int image_slot_state[TOTAL_IMAGE_SLOTS] = {EMPTY_SLOT, EMPTY_SLOT, EMPTY_SLOT, EMPTY_SLOT};

static void load_digit_image_into_slot(int slot_number, int digit_value) {
    /*
     
     Loads the digit image from the application's resources and
     displays it on-screen in the correct location.
     
     Each slot is a quarter of the screen.
     
     */
    
    // TODO: Signal these error(s)?
    
    if ((slot_number < 0) || (slot_number >= TOTAL_IMAGE_SLOTS)) {
        return;
    }
    
    if ((digit_value < 0) || (digit_value > 9)) {
        return;
    }
    
    if (image_slot_state[slot_number] != EMPTY_SLOT) {
        return;
    }
    
    image_slot_state[slot_number] = digit_value;
	if ((bluetooth_connection_service_peek())) {
       digit_value = digit_value + 10;
	}
	images[slot_number] = gbitmap_create_with_resource(IMAGE_RESOURCE_IDS[digit_value]);
    GRect frame = (GRect) {
        .origin = { (slot_number % 2) * 72, (slot_number / 2) * 84 },
        .size = images[slot_number]->bounds.size
    };
    BitmapLayer *bitmap_layer = bitmap_layer_create(frame);
    image_layers[slot_number] = bitmap_layer;
    bitmap_layer_set_bitmap(bitmap_layer, images[slot_number]);
    Layer *window_layer = window_get_root_layer(window);
    layer_add_child(window_layer, bitmap_layer_get_layer(bitmap_layer));
}

static void unload_digit_image_from_slot(int slot_number) {
    /*
     
     Removes the digit from the display and unloads the image resource
     to free up RAM.
     
     Can handle being called on an already empty slot.
     
     */
    
    if (image_slot_state[slot_number] != EMPTY_SLOT) {
        layer_remove_from_parent(bitmap_layer_get_layer(image_layers[slot_number]));
        bitmap_layer_destroy(image_layers[slot_number]);
        gbitmap_destroy(images[slot_number]);
        image_slot_state[slot_number] = EMPTY_SLOT;
    }
    
}

static void display_value(unsigned short value, unsigned short row_number, bool show_first_leading_zero) {
    /*
     
     Displays a numeric value between 0 and 99 on screen.
     
     Rows are ordered on screen as:
     
     Row 0
     Row 1
     
     Includes optional blanking of first leading zero,
     i.e. displays ' 0' rather than '00'.
     
     */

	// Set background color
	if (bluetooth_connection_service_peek())  {
		window_set_background_color(window, GColorWhite);
	}
	else {
		window_set_background_color(window, GColorBlack);
	}

	
	value = value % 100; // Maximum of two digits per row.
    
    // Column order is: | Column 0 | Column 1 |
    // (We process the columns in reverse order because that makes
    // extracting the digits from the value easier.)
    for (int column_number = 1; column_number >= 0; column_number--) {
        int slot_number = (row_number * 2) + column_number;
        unload_digit_image_from_slot(slot_number);
        if (!((value == 0) && (column_number == 0) && !show_first_leading_zero)) {
			load_digit_image_into_slot(slot_number, value % 10);
        }
        value = value / 10;
    }
}

static unsigned short get_display_hour(unsigned short hour) {
    
    if (clock_is_24h_style()) {
        return hour;
    }
    
    unsigned short display_hour = hour % 12;
    
    // Converts "0" to "12"
    return display_hour ? display_hour : 12;
    
}

static void display_time(struct tm *tick_time) {
    
    // TODO: Use `units_changed` and more intelligence to reduce
    //       redundant digit unload/load?
    
	// Refresh the screen if BT connection state changed
	if (bluetooth_connection_service_peek())  {
		// Update display if BT connection is made
		if (!(was_BTconnected_last_time))  {
			display_value(get_display_hour(tick_time->tm_hour), 0, false);
			display_value(tick_time->tm_min, 1, true);
		}
	}
	else  {
		// Update display if BT connection is lost
		if ((was_BTconnected_last_time))  {
			vibes_double_pulse();
			display_value(get_display_hour(tick_time->tm_hour), 0, false);
			display_value(tick_time->tm_min, 1, true);
		}
			
	}

	// This is to make sure the screen is not refreshed every second
	// UPDATE : Not refreshing if BT connection is lost
	if (tick_time->tm_min != min)  {
		display_value(get_display_hour(tick_time->tm_hour), 0, false);
		display_value(tick_time->tm_min, 1, true);
		min = tick_time->tm_min;
	}

	// Set the last known BT connection state
	was_BTconnected_last_time = bluetooth_connection_service_peek();
}

static void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
    display_time(tick_time);
}

static void make_vibes(void) {
	vibes_double_pulse();
}

static void init() {
    window = window_create();
    window_stack_push(window, true);

	// Set the last known BT connection state
	was_BTconnected_last_time = bluetooth_connection_service_peek();
	
    // Avoids a blank screen on watch start.
    time_t now = time(NULL);
    struct tm *tick_time = localtime(&now);
    display_time(tick_time);

    tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
}

static void deinit() {
    for (int i = 0; i < TOTAL_IMAGE_SLOTS; i++) {
        unload_digit_image_from_slot(i);
    }
    
    window_destroy(window);
	qtp_app_deinit();
}

int main(void) {
	qtp_conf = QTP_K_SHOW_TIME | QTP_K_AUTOHIDE | QTP_K_INVERT;
	init();
	qtp_setup();
    app_event_loop();
    deinit();
}
