tk_includes := $(tk_path)/generic $(tk_path)/sdl $(tk_path)/xlib

tk_cflags := \
	-DTK_LIBRARY="\"/assets/sdl2tk8.6\"" \
	-DPLATFORM_SDL=1
