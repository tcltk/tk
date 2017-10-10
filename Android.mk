LOCAL_PATH := $(call my-dir)

###########################
#
# Tk shared library
#
###########################

include $(CLEAR_VARS)

tcl_path := $(LOCAL_PATH)/../tcl

include $(tcl_path)/tcl-config.mk

LOCAL_ADDITIONAL_DEPENDENCIES += $(tcl_path)/tcl-config.mk

tk_path := $(LOCAL_PATH)

include $(tk_path)/tk-config.mk

LOCAL_ADDITIONAL_DEPENDENCIES += $(tk_path)/tk-config.mk

LOCAL_MODULE := tk

LOCAL_ARM_MODE := arm

LOCAL_C_INCLUDES := $(tk_includes) \
	$(LOCAL_PATH)/xlib \
	$(LOCAL_PATH)/bitmaps \
	$(LOCAL_PATH)/sdl/agg-2.4/include \
	$(LOCAL_PATH)/sdl/agg-2.4/font_freetype \
	$(LOCAL_PATH)/sdl/agg-2.4/agg2d \
	$(tcl_includes) \
	$(LOCAL_PATH) \
	$(LOCAL_PATH)/../freetype/include

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_C_INCLUDES)

LOCAL_SRC_FILES := \
        generic/tk3d.c \
	generic/tkArgv.c \
        generic/tkAtom.c \
	generic/tkBind.c \
	generic/tkBitmap.c \
	generic/tkBusy.c \
	generic/tkClipboard.c \
	generic/tkCmds.c \
	generic/tkColor.c \
	generic/tkConfig.c \
	generic/tkCursor.c \
	generic/tkError.c \
	generic/tkEvent.c \
	generic/tkFocus.c \
	generic/tkFont.c \
	generic/tkGet.c \
	generic/tkGC.c \
	generic/tkGeometry.c \
	generic/tkGrab.c \
	generic/tkGrid.c \
	generic/tkConsole.c \
	generic/tkZipMain.c \
	generic/tkOption.c \
	generic/tkPack.c \
	generic/tkPlace.c \
	generic/tkSelect.c \
	generic/tkStyle.c \
	generic/tkUndo.c \
	generic/tkUtil.c \
	generic/tkVisual.c \
	generic/tkWindow.c \
	generic/tkButton.c \
	generic/tkObj.c \
	generic/tkEntry.c \
	generic/tkFrame.c \
	generic/tkListbox.c \
	generic/tkMenu.c \
	generic/tkMenubutton.c \
	generic/tkMenuDraw.c \
	generic/tkMessage.c \
	generic/tkPanedWindow.c \
	generic/tkPointer.c \
	generic/tkScale.c \
	generic/tkScrollbar.c \
	generic/tkCanvas.c \
	generic/tkCanvArc.c \
	generic/tkCanvBmap.c \
	generic/tkCanvImg.c \
	generic/tkCanvLine.c \
	generic/tkCanvPoly.c \
	generic/tkCanvPs.c \
	generic/tkCanvText.c \
	generic/tkCanvUtil.c \
	generic/tkCanvWind.c \
	generic/tkRectOval.c \
	generic/tkTrig.c \
	generic/tkImage.c \
	generic/tkImgBmap.c \
	generic/tkImgGIF.c \
	generic/tkImgPNG.c \
	generic/tkImgPPM.c \
	generic/tkImgUtil.c \
	generic/tkImgPhoto.c \
	generic/tkImgPhInstance.c \
	generic/tkText.c \
	generic/tkTextBTree.c \
	generic/tkTextDisp.c \
	generic/tkTextImage.c \
	generic/tkTextIndex.c \
	generic/tkTextMark.c \
	generic/tkTextTag.c \
	generic/tkTextWind.c \
	generic/tkOldConfig.c \
	generic/tkStubInit.c \
	generic/ttk/ttkBlink.c \
	generic/ttk/ttkButton.c \
	generic/ttk/ttkCache.c \
	generic/ttk/ttkClamTheme.c \
	generic/ttk/ttkClassicTheme.c \
	generic/ttk/ttkDefaultTheme.c \
	generic/ttk/ttkElements.c \
	generic/ttk/ttkEntry.c \
	generic/ttk/ttkFrame.c \
	generic/ttk/ttkImage.c \
	generic/ttk/ttkInit.c \
	generic/ttk/ttkLabel.c \
	generic/ttk/ttkLayout.c \
	generic/ttk/ttkManager.c \
	generic/ttk/ttkNotebook.c \
	generic/ttk/ttkPanedwindow.c \
	generic/ttk/ttkProgress.c \
	generic/ttk/ttkScale.c \
	generic/ttk/ttkScrollbar.c \
	generic/ttk/ttkScroll.c \
	generic/ttk/ttkSeparator.c \
	generic/ttk/ttkState.c \
	generic/ttk/ttkTagSet.c \
	generic/ttk/ttkTheme.c \
	generic/ttk/ttkTrace.c \
	generic/ttk/ttkTrack.c \
	generic/ttk/ttkTreeview.c \
	generic/ttk/ttkWidget.c \
	generic/ttk/ttkStubInit.c \
	xlib/xcolors.c \
        sdl/tkSDL.c \
	sdl/tkSDL3d.c \
	sdl/tkSDLButton.c \
	sdl/tkSDLColor.c \
	sdl/tkSDLConfig.c \
	sdl/tkSDLCursor.c \
	sdl/tkSDLDraw.c \
	sdl/tkSDLEmbed.c \
	sdl/tkSDLEvent.c \
	sdl/tkSDLFocus.c \
	sdl/tkSDLFont.c \
	sdl/tkSDLInit.c \
	sdl/tkSDLKey.c \
	sdl/tkSDLMenu.c \
	sdl/tkSDLMenubu.c \
	sdl/tkSDLScale.c \
	sdl/tkSDLScrlbr.c \
	sdl/tkSDLSelect.c \
	sdl/tkSDLSend.c \
	sdl/tkSDLWm.c \
	sdl/tkSDLXId.c \
	sdl/SdlTkDecframe.c \
	sdl/SdlTkGfx.c \
	sdl/SdlTkInt.c \
	sdl/SdlTkUtils.c \
	sdl/SdlTkX.c \
	sdl/Region.c \
	sdl/PolyReg.c \
	sdl/SdlTkAGG.cpp \
	sdl/agg-2.4/src/agg_arc.cpp \
	sdl/agg-2.4/src/agg_bezier_arc.cpp \
	sdl/agg-2.4/src/agg_curves.cpp \
	sdl/agg-2.4/src/agg_vcgen_dash.cpp \
	sdl/agg-2.4/src/agg_vcgen_stroke.cpp \
	sdl/agg-2.4/src/agg_line_aa_basics.cpp \
	sdl/agg-2.4/src/agg_line_profile_aa.cpp \
	sdl/agg-2.4/src/agg_sqrt_tables.cpp \
	sdl/agg-2.4/src/agg_trans_affine.cpp \
	sdl/agg-2.4/src/agg_rounded_rect.cpp \
	sdl/agg-2.4/src/agg_image_filters.cpp \
	sdl/agg-2.4/font_freetype/agg_font_freetype.cpp \
	sdl/agg-2.4/agg2d/agg2d.cpp

LOCAL_CFLAGS := $(tcl_cflags) $(tk_cflags) \
	-DUSE_SYMBOLA_CTRL=1 \
	-DPACKAGE_NAME=\"tk\" \
	-DPACKAGE_VERSION=\"8.6\" \
	-DBUILD_tk=1 \
	-O2

LOCAL_SHARED_LIBRARIES := libtcl libSDL2 libfreetype

LOCAL_LDLIBS := -llog

include $(BUILD_SHARED_LIBRARY)
