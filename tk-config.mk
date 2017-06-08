sdl_path := $(tk_path)/../SDL2

tk_includes := $(tk_path)/generic $(tk_path)/sdl $(tk_path)/xlib \
	$(sdl_path)/include

tk_cflags := \
	-DTK_LIBRARY="\"/assets/sdl2tk8.6\"" \
	-DPLATFORM_SDL=1 \
	-DTK_USE_POLL=1 \
	-DAGG_CUSTOM_ALLOCATOR=1
