// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, R. Belmont
//============================================================
//
//  window.c - SDL window handling
//
//  SDLMAME by Olivier Galibert and R. Belmont
//
//============================================================

// MAME headers
#include "emu.h"
#include "emuopts.h"
#include "render.h"
#include "screen.h"
#include "ui/uimain.h"

// OSD headers
#include "window.h"
#include "osdsdl.h"
#include "modules/monitor/monitor_common.h"

// standard SDL headers
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

// standard C headers
#include <cmath>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include <list>
#include <memory>

#ifdef SDLMAME_WIN32
#include <windows.h>
#endif


//============================================================
//  PARAMETERS
//============================================================

// these are arbitrary values since AFAIK there's no way to make X/SDL tell you
#define WINDOW_DECORATION_WIDTH (8) // should be more than plenty
#define WINDOW_DECORATION_HEIGHT (48)   // title bar + bottom drag region

// minimum window dimension
#define MIN_WINDOW_DIM                  200

#ifndef SDLMAME_WIN32
#define WMSZ_TOP            (0)
#define WMSZ_BOTTOM         (1)
#define WMSZ_BOTTOMLEFT     (2)
#define WMSZ_BOTTOMRIGHT    (3)
#define WMSZ_LEFT           (4)
#define WMSZ_TOPLEFT        (5)
#define WMSZ_TOPRIGHT       (6)
#define WMSZ_RIGHT          (7)
#endif

#define SDL_VERSION_EQUALS(v1, vnum2) (SDL_VERSIONNUM(v1.major, v1.minor, v1.patch) == vnum2)

class SDL_DM_Wrapper
{
public:
	SDL_DisplayMode mode;
};


//============================================================
//  window_init
//  (main thread)
//============================================================

bool sdl_osd_interface::window_init()
{
	osd_printf_verbose("Enter sdlwindow_init\n");

	// We may want to set a number of the hints SDL2 provides.
	// The code below will document which hints were set.
	char const *const hints[] = {
			SDL_HINT_FRAMEBUFFER_ACCELERATION,
			SDL_HINT_RENDER_DRIVER, SDL_HINT_RENDER_OPENGL_SHADERS,
			SDL_HINT_RENDER_SCALE_QUALITY,
			SDL_HINT_RENDER_VSYNC,
			SDL_HINT_VIDEO_X11_XVIDMODE, SDL_HINT_VIDEO_X11_XINERAMA,
			SDL_HINT_VIDEO_X11_XRANDR, SDL_HINT_GRAB_KEYBOARD,
			SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, SDL_HINT_IDLE_TIMER_DISABLED,
			SDL_HINT_ORIENTATIONS,
			SDL_HINT_XINPUT_ENABLED, SDL_HINT_GAMECONTROLLERCONFIG,
			SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, SDL_HINT_ALLOW_TOPMOST,
			SDL_HINT_TIMER_RESOLUTION,
			SDL_HINT_RENDER_DIRECT3D_THREADSAFE, SDL_HINT_VIDEO_ALLOW_SCREENSAVER,
			SDL_HINT_ACCELEROMETER_AS_JOYSTICK, SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK,
			SDL_HINT_VIDEO_WIN_D3DCOMPILER, SDL_HINT_VIDEO_WINDOW_SHARE_PIXEL_FORMAT,
			SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, SDL_HINT_MOUSE_RELATIVE_MODE_WARP,
			SDL_HINT_RENDER_DIRECT3D11_DEBUG, SDL_HINT_VIDEO_HIGHDPI_DISABLED,
			SDL_HINT_WINRT_PRIVACY_POLICY_URL, SDL_HINT_WINRT_PRIVACY_POLICY_LABEL,
			SDL_HINT_WINRT_HANDLE_BACK_BUTTON,
			};

	osd_printf_verbose("\nHints:\n");
	for (auto const hintname : hints)
	{
		char const *const hintvalue(SDL_GetHint(hintname));
		osd_printf_verbose("\t%-40s %s\n", hintname, hintvalue ? hintvalue : "(NULL)");
	}

	// set up the window list
	osd_printf_verbose("Leave sdlwindow_init\n");
	return true;
}


void sdl_osd_interface::update_slider_list()
{
	for (auto const &window : osd_common_t::window_list())
	{
		// check if any window has dirty sliders
		if (window->renderer().sliders_dirty())
		{
			build_slider_list();
			return;
		}
	}
}

void sdl_osd_interface::build_slider_list()
{
	m_sliders.clear();

	for (auto const &window : osd_common_t::window_list())
	{
		std::vector<ui::menu_item> window_sliders = window->renderer().get_slider_list();
		m_sliders.insert(m_sliders.end(), window_sliders.begin(), window_sliders.end());
	}
}

//============================================================
//  sdlwindow_exit
//  (main thread)
//============================================================

void sdl_osd_interface::window_exit()
{
	osd_printf_verbose("Enter sdlwindow_exit\n");

	// free all the windows
	m_focus_window = nullptr;
	while (!osd_common_t::s_window_list.empty())
	{
		auto window = std::move(osd_common_t::s_window_list.back());
		s_window_list.pop_back();
		window->destroy();
	}

	osd_printf_verbose("Leave sdlwindow_exit\n");
}

void sdl_window_info::capture_pointer()
{
	if (!m_mouse_captured)
	{
		SDL_SetWindowGrab(platform_window(), SDL_TRUE);
		SDL_SetRelativeMouseMode(SDL_TRUE);
		m_mouse_captured = true;
	}
}

void sdl_window_info::release_pointer()
{
	if (m_mouse_captured)
	{
		SDL_SetWindowGrab(platform_window(), SDL_FALSE);
		SDL_SetRelativeMouseMode(SDL_FALSE);
		m_mouse_captured = false;
	}
}

void sdl_window_info::hide_pointer()
{
	if (!m_mouse_hidden)
	{
		SDL_ShowCursor(SDL_DISABLE);
		m_mouse_hidden = true;
	}
}

void sdl_window_info::show_pointer()
{
	if (m_mouse_hidden)
	{
		SDL_ShowCursor(SDL_ENABLE);
		m_mouse_hidden = false;
	}
}


//============================================================
//  sdlwindow_resize
//============================================================

void sdl_window_info::resize(int32_t width, int32_t height)
{
	osd_dim cd = get_size();

	if (width != cd.width() || height != cd.height())
	{
		SDL_SetWindowSize(platform_window(), width, height);
		renderer().notify_changed();
	}
}


//============================================================
//  sdlwindow_clear_surface
//============================================================

void sdl_window_info::notify_changed()
{
	renderer().notify_changed();
}


//============================================================
//  sdlwindow_toggle_full_screen
//============================================================

void sdl_window_info::toggle_full_screen()
{
	// if we are in debug mode, never go full screen
	if (machine().debug_flags & DEBUG_FLAG_OSD_ENABLED)
		return;

	// If we are going fullscreen (leaving windowed) remember our windowed size
	if (!fullscreen())
	{
		m_windowed_dim = get_size();
	}

	// kill off the drawers
	renderer_reset();
	bool is_osx = false;
#ifdef SDLMAME_MACOSX
	// FIXME: This is weird behaviour and certainly a bug in SDL
	is_osx = true;
#endif
	if (fullscreen() && (video_config.switchres || is_osx))
	{
		SDL_SetWindowFullscreen(platform_window(), 0);    // Try to set mode
		SDL_SetWindowDisplayMode(platform_window(), &m_original_mode->mode);    // Try to set mode
		SDL_SetWindowFullscreen(platform_window(), SDL_WINDOW_FULLSCREEN);    // Try to set mode
	}
	SDL_DestroyWindow(platform_window());
	set_platform_window(nullptr);
	downcast<sdl_osd_interface &>(machine().osd()).release_keys();

	// toggle the window mode
	set_fullscreen(!fullscreen());

	complete_create();
}

void sdl_window_info::modify_prescale(int dir)
{
	int new_prescale = prescale();

	if (dir > 0 && prescale() < 20)
		new_prescale = prescale() + 1;
	if (dir < 0 && prescale() > 1)
		new_prescale = prescale() - 1;

	if (new_prescale != prescale())
	{
		if (m_fullscreen && video_config.switchres)
		{
			complete_destroy();

			m_prescale = new_prescale;

			complete_create();
		}
		else
		{
			m_prescale = new_prescale;
			notify_changed();
		}
		machine().ui().popup_time(1, "Prescale %d", prescale());
	}
}

//============================================================
//  sdlwindow_update_cursor_state
//  (main or window thread)
//============================================================

void sdl_window_info::update_cursor_state()
{
#if (USE_XINPUT && USE_XINPUT_WII_LIGHTGUN_HACK)
	// Hack for wii-lightguns:
	// they stop working with a grabbed mouse;
	// even a ShowCursor(SDL_DISABLE) already does this.
	// To make the cursor disappear, we'll just set an empty cursor image.
	unsigned char data[]={0,0,0,0,0,0,0,0};
	SDL_Cursor *c;
	c=SDL_CreateCursor(data, data, 8, 8, 0, 0);
	SDL_SetCursor(c);
#else
	// do not do mouse capture if the debugger's enabled to avoid
	// the possibility of losing control
	if (!(machine().debug_flags & DEBUG_FLAG_OSD_ENABLED))
	{
		bool should_hide_mouse = downcast<sdl_osd_interface&>(machine().osd()).should_hide_mouse();

		if (!fullscreen() && !should_hide_mouse)
		{
			show_pointer();
			release_pointer();
		}
		else
		{
			hide_pointer();
			capture_pointer();
		}

		SDL_SetCursor(nullptr); // Force an update in case the underlying driver has changed visibility
	}
#endif
}

int sdl_window_info::xy_to_render_target(int x, int y, int *xt, int *yt)
{
	return renderer().xy_to_render_target(x, y, xt, yt);
}

//============================================================
//  sdlwindow_video_window_create
//  (main thread)
//============================================================

int sdl_window_info::window_init()
{
	// set the initial maximized state
	// FIXME: Does not belong here
	m_startmaximized = downcast<sdl_options &>(machine().options()).maximize();

	create_target();

	int result = complete_create();

	// handle error conditions
	if (result == 1)
		goto error;

	return 0;

error:
	destroy();
	return 1;
}


//============================================================
//  sdlwindow_video_window_destroy
//============================================================

void sdl_window_info::complete_destroy()
{
	// Release pointer grab and hide if needed
	show_pointer();
	release_pointer();

	if (fullscreen() && video_config.switchres)
	{
		SDL_SetWindowFullscreen(platform_window(), 0);    // Try to set mode
		SDL_SetWindowDisplayMode(platform_window(), &m_original_mode->mode);    // Try to set mode
		SDL_SetWindowFullscreen(platform_window(), SDL_WINDOW_FULLSCREEN);    // Try to set mode
	}

	renderer_reset();
	SDL_DestroyWindow(platform_window());
	set_platform_window(nullptr);
	downcast<sdl_osd_interface &>(machine().osd()).release_keys();
}


//============================================================
//  pick_best_mode
//============================================================

osd_dim sdl_window_info::pick_best_mode()
{
	int minimum_width, minimum_height, target_width, target_height;
	int i;
	int num;
	float size_score, best_score = 0.0f;
	osd_dim ret(0,0);

	// determine the minimum width/height for the selected target
	target()->compute_minimum_size(minimum_width, minimum_height);

	// use those as the target for now
	target_width = minimum_width * std::max(1, prescale());
	target_height = minimum_height * std::max(1, prescale());

	// if we're not stretching, allow some slop on the minimum since we can handle it
	{
		minimum_width -= 4;
		minimum_height -= 4;
	}

	// FIXME: this should be provided by monitor !
	num = SDL_GetNumDisplayModes(monitor()->oshandle());

	if (num == 0)
	{
		osd_printf_error("SDL: No modes available?!\n");
		exit(-1);
	}
	else
	{
		for (i = 0; i < num; ++i)
		{
			SDL_DisplayMode mode;
			SDL_GetDisplayMode(monitor()->oshandle(), i, &mode);

			// compute initial score based on difference between target and current
			size_score = 1.0f / (1.0f + abs((int32_t)mode.w - target_width) + abs((int32_t)mode.h - target_height));

			// if the mode is too small, give a big penalty
			if (mode.w < minimum_width || mode.h < minimum_height)
				size_score *= 0.01f;

			// if mode is smaller than we'd like, it only scores up to 0.1
			if (mode.w < target_width || mode.h < target_height)
				size_score *= 0.1f;

			// if we're looking for a particular mode, that's a winner
			if (mode.w == m_win_config.width && mode.h == m_win_config.height)
				size_score = 2.0f;

			// refresh adds some points
			if (m_win_config.refresh)
				size_score *= 1.0f / (1.0f + abs(m_win_config.refresh - mode.refresh_rate) / 10.0f);

			osd_printf_verbose("%4dx%4d@%2d -> %f\n", (int)mode.w, (int)mode.h, (int) mode.refresh_rate, (double) size_score);

			// best so far?
			if (size_score > best_score)
			{
				best_score = size_score;
				ret = osd_dim(mode.w, mode.h);
			}

		}
	}
	return ret;
}

//============================================================
//  sdlwindow_video_window_update
//  (main thread)
//============================================================

void sdl_window_info::update()
{
	// adjust the cursor state
	update_cursor_state();

	// if we're visible and running and not in the middle of a resize, draw
	if (target() != nullptr)
	{
		int tempwidth, tempheight;

		// see if the games video mode has changed
		target()->compute_minimum_size(tempwidth, tempheight);
		if (osd_dim(tempwidth, tempheight) != m_minimum_dim)
		{
			m_minimum_dim = osd_dim(tempwidth, tempheight);

			if (!this->m_fullscreen)
			{
				//Don't resize window without user interaction;
				//window_resize(blitwidth, blitheight);
			}
			else if (video_config.switchres)
			{
				osd_dim tmp = this->pick_best_mode();
				resize(tmp.width(), tmp.height());
			}
		}

		osd_ticks_t event_wait_ticks;
		if (video_config.waitvsync && video_config.syncrefresh)
			event_wait_ticks = osd_ticks_per_second(); // block at most a second
		else
			event_wait_ticks = 0;

		if (m_rendered_event.wait(event_wait_ticks))
		{
			const int update = 1;

			// ensure the target bounds are up-to-date, and then get the primitives

			render_primitive_list &primlist = *renderer().get_primitives();

			// and redraw now

			// Check whether window has vector screens

			{
				const screen_device *screen = screen_device_enumerator(machine().root_device()).byindex(index());
				if ((screen != nullptr) && (screen->screen_type() == SCREEN_TYPE_VECTOR))
					renderer().set_flags(osd_renderer::FLAG_HAS_VECTOR_SCREEN);
				else
					renderer().clear_flags(osd_renderer::FLAG_HAS_VECTOR_SCREEN);
			}

			m_primlist = &primlist;

			if (m_primlist == nullptr)
			{
				// if no bitmap, just fill
			}
			else
			{
				// otherwise, render with our drawing system
				if (video_config.perftest)
					measure_fps(update);
				else
					renderer().draw(update);
			}

			// all done, ready for next
			m_rendered_event.set();
		}
	}
}


//============================================================
//  complete_create
//============================================================

int sdl_window_info::complete_create()
{
	osd_dim temp(0,0);

	// clear out original mode. Needed on OSX
	if (fullscreen())
	{
		// default to the current mode exactly
		temp = monitor()->position_size().dim();

		// if we're allowed to switch resolutions, override with something better
		if (video_config.switchres)
			temp = pick_best_mode();
	}
	else if (m_windowed_dim.width() > 0)
	{
		// if we have a remembered size force the new window size to it
		temp = m_windowed_dim;
	}
	else if (m_startmaximized)
	{
		temp = get_max_bounds(keepaspect());
	}
	else
	{
		temp = get_min_bounds(keepaspect());
	}

	// create the window .....

	/* FIXME: On Ubuntu and potentially other Linux OS you should use
	 * to disable panning. This has to be done before every invocation of mame.
	 *
	 * xrandr --output HDMI-0 --panning 0x0+0+0 --fb 0x0
	 *
	 */
	osd_printf_verbose("Enter sdl_info::create\n");
	if (renderer_sdl_needs_opengl())
	{
		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		m_extra_flags = SDL_WINDOW_OPENGL;
	}
	else
	{
		m_extra_flags = 0;
	}

	// create the SDL window
	// soft driver also used | SDL_WINDOW_INPUT_GRABBED | SDL_WINDOW_MOUSE_FOCUS
	m_extra_flags |= (fullscreen() ?
			SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_FULLSCREEN : SDL_WINDOW_RESIZABLE);

#if defined(SDLMAME_WIN32)
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
#endif

	// get monitor work area for centering
	osd_rect work = monitor()->usuable_position_size();

	// create or attach to an existing window
	SDL_Window *sdlwindow;
#ifdef SDLMAME_X11
	const char *attach_window = downcast<sdl_options &>(machine().options()).attach_window();
#else
	const char *attach_window = nullptr;
#endif
	if (attach_window && *attach_window)
	{
		// we're attaching to an existing window; parse the argument
		unsigned long long attach_window_value;
		try
		{
			attach_window_value = std::stoull(attach_window, nullptr, 0);
		}
		catch (std::invalid_argument &)
		{
			osd_printf_error("Invalid -attach_window value: %s\n", attach_window);
			return 1;
		}

		// and attach to it
		sdlwindow = SDL_CreateWindowFrom((void *)attach_window_value);
		if (!sdlwindow)
		{
			osd_printf_error("Failed to attach to window \"%s\": %s\n", attach_window, SDL_GetError());
			return 1;
		}

		// perform SDL subsystem-specific tasks
		SDL_SysWMinfo swmi;
		SDL_VERSION(&swmi.version);
		if (SDL_GetWindowWMInfo(sdlwindow, &swmi))
		{
			switch (swmi.subsystem)
			{
#ifdef SDLMAME_X11
			case SDL_SYSWM_X11:
				// by default, SDL_CreateWindowFrom() doesn't ensure that we're getting the events that we
				// expect
				XSelectInput(swmi.info.x11.display, swmi.info.x11.window,
					FocusChangeMask | EnterWindowMask | LeaveWindowMask |
					PointerMotionMask | KeyPressMask | KeyReleaseMask |
					PropertyChangeMask | StructureNotifyMask |
					ExposureMask | KeymapStateMask);
				break;
#endif // SDLMAME_X11

			default:
				break;
			}
		}
	}
	else
	{
		// create the SDL window
		sdlwindow = SDL_CreateWindow(title().c_str(),
			work.left() + (work.width() - temp.width()) / 2,
			work.top() + (work.height() - temp.height()) / 2,
			temp.width(), temp.height(), m_extra_flags);
	}

	//window().sdl_window() = SDL_CreateWindow(window().m_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
	//      width, height, m_extra_flags);

	if  (sdlwindow == nullptr )
	{
		if (renderer_sdl_needs_opengl())
			osd_printf_error("OpenGL not supported on this driver: %s\n", SDL_GetError());
		else
			osd_printf_error("Window creation failed: %s\n", SDL_GetError());
		return 1;
	}

	set_platform_window(sdlwindow);
	renderer_create();

	if (fullscreen() && video_config.switchres)
	{
		SDL_DisplayMode mode;
		//SDL_GetCurrentDisplayMode(window().monitor()->handle, &mode);
		SDL_GetWindowDisplayMode(platform_window(), &mode);
		m_original_mode->mode = mode;
		mode.w = temp.width();
		mode.h = temp.height();
		if (m_win_config.refresh)
			mode.refresh_rate = m_win_config.refresh;

		SDL_SetWindowDisplayMode(platform_window(), &mode);    // Try to set mode
#ifndef SDLMAME_WIN32
		/* FIXME: Warp the mouse to 0,0 in case a virtual desktop resolution
		 * is in place after the mode switch - which will most likely be the case
		 * This is a hack to work around a deficiency in SDL2
		 */
		SDL_WarpMouseInWindow(platform_window(), 1, 1);
#endif
	}
	else
	{
		//SDL_SetWindowDisplayMode(window().sdl_window(), nullptr); // Use desktop
	}

	// show window

	SDL_ShowWindow(platform_window());
	//SDL_SetWindowFullscreen(window->sdl_window(), 0);
	//SDL_SetWindowFullscreen(window->sdl_window(), window->fullscreen());
	SDL_RaiseWindow(platform_window());

#ifdef SDLMAME_WIN32
	if (fullscreen())
		SDL_SetWindowGrab(platform_window(), SDL_TRUE);
#endif

	// update monitor resolution after mode change to ensure proper pixel aspect
	monitor()->refresh();
	if (fullscreen() && video_config.switchres)
		monitor()->update_resolution(temp.width(), temp.height());

	// initialize the drawing backend
	if (renderer().create())
		return 1;

	// Make sure we have a consistent state
	SDL_ShowCursor(SDL_DISABLE);
	SDL_ShowCursor(SDL_ENABLE);

	return 0;
}


//============================================================
//  draw_video_contents
//  (window thread)
//============================================================

void sdl_window_info::measure_fps(int update)
{
	const unsigned long frames_skip4fps = 100;
	static int64_t lastTime=0, sumdt=0, startTime=0;
	static unsigned long frames = 0;
	int64_t currentTime, t0;
	double dt;
	double tps;
	osd_ticks_t tps_t;

	tps_t = osd_ticks_per_second();
	tps = (double) tps_t;

	t0 = osd_ticks();

	renderer().draw(update);

	frames++;
	currentTime = osd_ticks();
	if(startTime==0||frames==frames_skip4fps)
		startTime=currentTime;
	if( frames>=frames_skip4fps )
		sumdt+=currentTime-t0;
	if( (currentTime-lastTime)>1L*osd_ticks_per_second() && frames>frames_skip4fps )
	{
		dt = (double) (currentTime-startTime) / tps; // in decimale sec.
		osd_printf_info("%6.2lfs, %4lu F, "
				"avrg game: %5.2lf FPS %.2lf ms/f, "
				"avrg video: %5.2lf FPS %.2lf ms/f, "
				"last video: %5.2lf FPS %.2lf ms/f\n",
			dt, frames-frames_skip4fps,
			(double)(frames-frames_skip4fps)/dt,                             // avrg game fps
			( (currentTime-startTime) / ((frames-frames_skip4fps)) ) * 1000.0 / osd_ticks_per_second(),
			(double)(frames-frames_skip4fps)/((double)(sumdt) / tps), // avrg vid fps
			( sumdt / ((frames-frames_skip4fps)) ) * 1000.0 / tps,
			1.0/((currentTime-t0) / osd_ticks_per_second()), // this vid fps
			(currentTime-t0) * 1000.0 / tps
		);
		lastTime = currentTime;
	}
}

int sdl_window_info::wnd_extra_width()
{
	return m_fullscreen ? 0 : WINDOW_DECORATION_WIDTH;
}

int sdl_window_info::wnd_extra_height()
{
	return m_fullscreen ? 0 : WINDOW_DECORATION_HEIGHT;
}


//============================================================
//  constrain_to_aspect_ratio
//  (window thread)
//============================================================

osd_rect sdl_window_info::constrain_to_aspect_ratio(const osd_rect &rect, int adjustment)
{
	int32_t extrawidth = wnd_extra_width();
	int32_t extraheight = wnd_extra_height();
	int32_t propwidth, propheight;
	int32_t minwidth, minheight;
	int32_t maxwidth, maxheight;
	int32_t viswidth, visheight;
	int32_t adjwidth, adjheight;
	float pixel_aspect;

	// get the pixel aspect ratio for the target monitor
	pixel_aspect = monitor()->pixel_aspect();

	// determine the proposed width/height
	propwidth = rect.width() - extrawidth;
	propheight = rect.height() - extraheight;

	// based on which edge we are adjusting, take either the width, height, or both as gospel
	// and scale to fit using that as our parameter
	switch (adjustment)
	{
		case WMSZ_BOTTOM:
		case WMSZ_TOP:
			target()->compute_visible_area(10000, propheight, pixel_aspect, target()->orientation(), propwidth, propheight);
			break;

		case WMSZ_LEFT:
		case WMSZ_RIGHT:
			target()->compute_visible_area(propwidth, 10000, pixel_aspect, target()->orientation(), propwidth, propheight);
			break;

		default:
			target()->compute_visible_area(propwidth, propheight, pixel_aspect, target()->orientation(), propwidth, propheight);
			break;
	}

	// get the minimum width/height for the current layout
	target()->compute_minimum_size(minwidth, minheight);

	// clamp against the absolute minimum
	propwidth = std::max(propwidth, MIN_WINDOW_DIM);
	propheight = std::max(propheight, MIN_WINDOW_DIM);

	// clamp against the minimum width and height
	propwidth = std::max(propwidth, minwidth);
	propheight = std::max(propheight, minheight);

	// clamp against the maximum (fit on one screen for full screen mode)
	if (m_fullscreen)
	{
		maxwidth = monitor()->position_size().width() - extrawidth;
		maxheight = monitor()->position_size().height() - extraheight;
	}
	else
	{
		maxwidth = monitor()->usuable_position_size().width() - extrawidth;
		maxheight = monitor()->usuable_position_size().height() - extraheight;

		// further clamp to the maximum width/height in the window
		if (m_win_config.width != 0)
			maxwidth = std::min(maxwidth, m_win_config.width + extrawidth);
		if (m_win_config.height != 0)
			maxheight = std::min(maxheight, m_win_config.height + extraheight);
	}

	// clamp to the maximum
	propwidth = std::min(propwidth, maxwidth);
	propheight = std::min(propheight, maxheight);

	// compute the visible area based on the proposed rectangle
	target()->compute_visible_area(propwidth, propheight, pixel_aspect, target()->orientation(), viswidth, visheight);

	// clamp visable area to the proposed rectangle
	viswidth = std::min(viswidth, propwidth);
	visheight = std::min(visheight, propheight);

	// compute the adjustments we need to make
	adjwidth = (viswidth + extrawidth) - rect.width();
	adjheight = (visheight + extraheight) - rect.height();

	// based on which corner we're adjusting, constrain in different ways
	osd_rect ret(rect);

	switch (adjustment)
	{
		case WMSZ_BOTTOM:
		case WMSZ_BOTTOMRIGHT:
		case WMSZ_RIGHT:
			ret = rect.resize(rect.width() + adjwidth, rect.height() + adjheight);
			break;

		case WMSZ_BOTTOMLEFT:
			ret = rect.move_by(-adjwidth, 0).resize(rect.width() + adjwidth, rect.height() + adjheight);
			break;

		case WMSZ_LEFT:
		case WMSZ_TOPLEFT:
		case WMSZ_TOP:
			ret = rect.move_by(-adjwidth, -adjheight).resize(rect.width() + adjwidth, rect.height() + adjheight);
			break;

		case WMSZ_TOPRIGHT:
			ret = rect.move_by(0, -adjheight).resize(rect.width() + adjwidth, rect.height() + adjheight);
			break;
}
	return ret;
}



//============================================================
//  get_min_bounds
//  (window thread)
//============================================================

osd_dim sdl_window_info::get_min_bounds(int constrain)
{
	int32_t minwidth, minheight;

	//assert(GetCurrentThreadId() == window_threadid);

	// get the minimum target size
	target()->compute_minimum_size(minwidth, minheight);

	// check if visible area is bigger
	int32_t viswidth, visheight;
	target()->compute_visible_area(minwidth, minheight, monitor()->aspect(), target()->orientation(), viswidth, visheight);
	minwidth = std::max(viswidth, minwidth);
	minheight = std::max(visheight, minheight);

	// expand to our minimum dimensions
	if (minwidth < MIN_WINDOW_DIM)
		minwidth = MIN_WINDOW_DIM;
	if (minheight < MIN_WINDOW_DIM)
		minheight = MIN_WINDOW_DIM;

	// account for extra window stuff
	minwidth += wnd_extra_width();
	minheight += wnd_extra_height();

	// if we want it constrained, figure out which one is larger
	if (constrain)
	{
		// first constrain with no height limit
		osd_rect test1(0,0,minwidth,10000);
		test1 = constrain_to_aspect_ratio(test1, WMSZ_BOTTOMRIGHT);

		// then constrain with no width limit
		osd_rect test2(0,0,10000,minheight);
		test2 = constrain_to_aspect_ratio(test2, WMSZ_BOTTOMRIGHT);

		// pick the larger
		if (test1.width() > test2.width())
		{
			minwidth = test1.width();
			minheight = test1.height();
		}
		else
		{
			minwidth = test2.width();
			minheight = test2.height();
		}
	}

	// remove extra window stuff
	minwidth -= wnd_extra_width();
	minheight -= wnd_extra_height();

	return osd_dim(minwidth, minheight);
}

//============================================================
//  get_size
//============================================================

osd_dim sdl_window_info::get_size()
{
	int w=0; int h=0;
	SDL_GetWindowSize(platform_window(), &w, &h);
	return osd_dim(w,h);
}


//============================================================
//  get_max_bounds
//  (window thread)
//============================================================

osd_dim sdl_window_info::get_max_bounds(int constrain)
{
	//assert(GetCurrentThreadId() == window_threadid);

	// compute the maximum client area
	// monitor()->refresh();
	osd_rect maximum = monitor()->usuable_position_size();

	// clamp to the window's max
	int tempw = maximum.width();
	int temph = maximum.height();
	if (m_win_config.width != 0)
	{
		int temp = m_win_config.width + wnd_extra_width();
		if (temp < maximum.width())
			tempw = temp;
	}
	if (m_win_config.height != 0)
	{
		int temp = m_win_config.height + wnd_extra_height();
		if (temp < maximum.height())
			temph = temp;
	}

	maximum = maximum.resize(tempw, temph);

	// constrain to fit
	if (constrain)
		maximum = constrain_to_aspect_ratio(maximum, WMSZ_BOTTOMRIGHT);

	// remove extra window stuff
	maximum = maximum.resize(maximum.width() - wnd_extra_width(), maximum.height() - wnd_extra_height());

	return maximum.dim();
}

//============================================================
//  construction and destruction
//============================================================

sdl_window_info::sdl_window_info(
		running_machine &a_machine,
		render_module &renderprovider,
		int index,
		const std::shared_ptr<osd_monitor_info> &a_monitor,
		const osd_window_config *config)
	: osd_window_t(a_machine, renderprovider, index, std::move(a_monitor), *config)
	, m_startmaximized(0)
	// Following three are used by input code to defer resizes
	, m_minimum_dim(0, 0)
	, m_windowed_dim(0, 0)
	, m_rendered_event(0, 1)
	, m_extra_flags(0)
	, m_mouse_captured(false)
	, m_mouse_hidden(false)
{
	//FIXME: these should be per_window in config-> or even better a bit set
	m_fullscreen = !video_config.windowed;
	m_prescale = video_config.prescale;

	m_windowed_dim = osd_dim(config->width, config->height);
	m_original_mode = std::make_unique<SDL_DM_Wrapper>();
}

sdl_window_info::~sdl_window_info()
{
}


//============================================================
//  osd_set_aggressive_input_focus
//============================================================

void osd_set_aggressive_input_focus(bool aggressive_focus)
{
	// dummy implementation for now
}
