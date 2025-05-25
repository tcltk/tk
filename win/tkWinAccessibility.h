/* Functions necessary for more than one file. */
typedef struct TkRootAccessible TkRootAccessible;
TkRootAccessible *GetTkAccessibleForWindow(Tk_Window win);
Tk_Window GetTkWindowForHwnd(HWND hwnd);

