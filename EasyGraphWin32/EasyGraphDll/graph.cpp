#include "graph.h"
// Private declarations goes here
#pragma comment (lib, "gdi32.lib")
#pragma comment (lib, "User32.lib")
#pragma comment (lib, "Opengl32.lib")
#pragma comment (lib, "Glu32.lib")

enum { MAX_SIGNAL_COUNT = 16 };
enum { NBPOINTS = 10000 };

VOID InitGL(HGRAPH hGraph, int Width, int Height);
VOID SetGLView(int Width, int Height);
VOID CheckErr(VOID);

GLvoid BuildFont(HGRAPH hGraph);
GLvoid KillFont(GLvoid);
GLvoid glPrint(const char* fmt, ...);

BOOL FindGlobalMaxScale(HGRAPH hGraph, float& Xmin, float& Xmax, float& Ymin, float& Ymax);
VOID DrawWave(HGRAPH hGraph);
VOID DrawString(float x, float y, char* string);
VOID DrawGraphSquare(VOID);
VOID DrawGridLines(VOID);
VOID DrawCursor(float x, float y);


inline float TakeFiniteNumber(float x);
float FindFirstFiniteNumber(float* tab, int length);
LPSTR ftos(LPSTR str, int len, float value);
float GetStandardizedData(float X, float min, float max);
VOID normalize_data(HGRAPH hGraph, float Xmin, float Xmax, float Ymin, float Ymax);
VOID UpdateBorder(HGRAPH hGraph);
VOID update_border(HGRAPH hGraph);

inline long long PerformanceFrequency();
inline long long PerformanceCounter();

typedef struct {
	float X[NBPOINTS];
	float Y[NBPOINTS];
	float Xnorm[NBPOINTS];
	float Ynorm[NBPOINTS];
	float Xmin;
	float Xmax;
	float Ymin;
	float Ymax;
}DATA;

#pragma warning(disable : 4200)					// Disable warning: DATA* signal[] -> Array size [0], See CreateGraph for signal allocation specifics
typedef struct {
	HWND hParentWnd;							// Parent handle of the object
	HWND hGraphWnd;								// Graph handle
	HDC hDC;									// OpenGL device context
	HGLRC hRC;									// OpenGL rendering context
	INT signalcount;							// Total signals in the struct
	INT cur_nbpoints;							// Current total points in the arrays
	BOOL bRunning;								// Status of the graph
	BOOL bLogging;								// Logging active
	DATA* signal[];								// ! (flexible array member) Array of pointers for every signal to be store by the struct - Must be last member of the struct
}GRAPHSTRUCT, * PGRAPHSTRUCT;					// Declaration of the struct. To be cast from HGRAPH api

// Global access

DATA* SnapPlot;									// SnapPlot: work with temp data on signals[], used to convert standard values to normalized values
RECT DispArea;									// RECT struct for the OpenGL area dimensions stored in WinProc
GLuint  base;                                   // Base Display List For The Font Set
SIZE dispStringWidth;							// The size  in pixel of "-0.000" displayed on screen
CRITICAL_SECTION cs;							// Sync purpose
FILE* logfile;									// The log file

	// High precision time measurements

long long frequency;
long long start;
long long finish;

GLuint    PixelFormat;                          //Defining the pixel format to display OpenGL 
PIXELFORMATDESCRIPTOR pfd =
{
	sizeof(PIXELFORMATDESCRIPTOR),              // Size Of This Pixel Format Descriptor
	1,                                          // Version Number (?)
	PFD_DRAW_TO_WINDOW |                        // Format Must Support Window
	PFD_SUPPORT_OPENGL |						// Format Must Support OpenGL
	PFD_DOUBLEBUFFER,							// Must Support Double Buffering
	PFD_TYPE_RGBA,								// Request An RGBA Format
	32,											// Select A 32Bit Color Depth
	0, 0, 0, 0, 0, 0,							// Color Bits Ignored (?)
	0,											// No Alpha Buffer
	0,											// Shift Bit Ignored (?)
	0,											// No Accumulation Buffer
	0, 0, 0, 0,									// Accumulation Bits Ignored (?)
	24,											// 32Bit Z-Buffer (Depth Buffer)
	8,											// No Stencil Buffer
	0,											// No Auxiliary Buffer (?)
	PFD_MAIN_PLANE,								// Main Drawing Layer
	0,											// Reserved (?)
	0, 0, 0										// Layer Masks Ignored (?)
};

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call,
	LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return true;
}


BOOL StartGraph(HGRAPH hGraph)
{
	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	DATA* pDATA;

	// Sanity check

	if (NULL == pgraph || TRUE == pgraph->bRunning)
		return FALSE;

	// reset counters and data array of signals

	pgraph->cur_nbpoints = 0;
	for (int index = 0; index < pgraph->signalcount; index++)
	{
		pDATA = pgraph->signal[0];
		memset(pDATA, 0, sizeof(DATA));									// zero each structs
	}

	// Save the start time x=0

	frequency = PerformanceFrequency();
	start = PerformanceCounter();

	// Create the log file
	if (pgraph->bLogging == TRUE)
	{
		logfile = NULL;
		char szFilename[MAX_PATH] = "Log.txt";
		fopen_s(&logfile, szFilename, "w");
		if (logfile)
		{
			fprintf(logfile, "Time(s)\tForce (N)\n");					// make logfile header
		}
		else
		{
			return FALSE;
		}
	}

	// Update status -> Graph ON

	pgraph->bRunning = TRUE;

	return TRUE;
}

VOID StopGraph(HGRAPH hGraph)
{
	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;

	if (NULL == pgraph)
		return;

	//Close the log file properly

	if (pgraph->bLogging == TRUE)
	{
		if (logfile)
		{
			fclose(logfile);
			logfile = NULL;
		}
	}

	// Update status -> Graph OFF

	pgraph->bRunning = FALSE;
	pgraph->cur_nbpoints = 0;
}

VOID FreeGraph(HGRAPH hGraph)
{
	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	if (pgraph)
	{

		DATA* pDATA;
		for (int index = 0; index < pgraph->signalcount; index++)
		{
			pDATA = pgraph->signal[index];
			if (pDATA)
				free(pDATA);
		}

		wglMakeCurrent(pgraph->hDC, NULL);
		wglDeleteContext(pgraph->hRC);
		ReleaseDC(pgraph->hParentWnd, pgraph->hDC);
		free(pgraph);

		if (SnapPlot)
			free(SnapPlot);

		if (logfile)
		{
			fclose(logfile);
			logfile = NULL;
		}

		pgraph = NULL;
		DeleteCriticalSection(&cs);

	}
}

/*-------------------------------------------------------------------------
	CreateGraph: Initialize the structure, signals,
	OpenGL and critical section. return HGRAPH
  -------------------------------------------------------------------------*/

HGRAPH CreateGraph(HWND hWnd, RECT GraphArea, INT SignalCount, BOOL logging)
{
	int PFDID;
	GRAPHSTRUCT* pgraph = NULL;

	// Sanity check

	if (NULL == hWnd)
		return NULL;

	if (0 == SignalCount || MAX_SIGNAL_COUNT < SignalCount)
		return NULL;

	// Initialyze sync

	InitializeCriticalSection(&cs);

	// Init struct

	if (NULL == (pgraph = (GRAPHSTRUCT*)malloc(sizeof(GRAPHSTRUCT) + sizeof(void*) * SignalCount)))		// Carefully taking in account signal declaration DATA*signal[], so allocate space for each new ptr on the fly
		return NULL;	// Otherwize Heap will be corrupted

		// Struct memory zero at startup

	memset(pgraph, 0, sizeof(GRAPHSTRUCT) + sizeof(void*) * SignalCount);

	// Allocate each signal buffers on the Heap and fill with zero

	for (int i = 0; i < SignalCount; i++)
	{
		if (NULL == (pgraph->signal[i] = (DATA*)malloc(sizeof(DATA))))
			return NULL;
		DATA* pDATA = pgraph->signal[i];
		memset(pDATA, 0, sizeof(DATA));
		pgraph->signalcount++;
	}

	// Allocate a temp struct for computing

	SnapPlot = (DATA*)malloc(sizeof(DATA));
	if (!SnapPlot)
	{
		return NULL;
	}

	pgraph->hParentWnd = hWnd;

	// Graph created in a "Static" control windows class named "Graph"
	// When redrawn the control will be painted with the graph in place of

	pgraph->hGraphWnd = CreateWindow(
		"Static",													// Predefined class; Unicode assumed 
		"Graph",													// The text will be erased by OpenGL
		WS_EX_TRANSPARENT | WS_VISIBLE | WS_CHILD,					// Styles WS_EX_TRANSPARENT mandatory
		GraphArea.left,												// x pos
		GraphArea.top,												// y pos
		GraphArea.right,											// Graph width
		GraphArea.bottom,											// Graph height
		hWnd,														// Parent window
		NULL,														// No menu.
		(HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE),			// HINST of the app
		NULL);														// No parameters

		// Sanity check

	if (NULL == pgraph->hGraphWnd)
		return NULL;

	pgraph->hDC = GetDC(pgraph->hGraphWnd);

	// Sanity check

	if (NULL == pgraph->hDC)
		return NULL;

	// Load OpenGL specific to legacy version

	ZeroMemory(&pfd, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cAlphaBits = 8;
	pfd.cDepthBits = 24;

	// To support advanced pixel format it is needed to load modern OpenGL functions
	// Only the legacy version is supported here

	PFDID = ChoosePixelFormat(pgraph->hDC, &pfd);
	if (PFDID == 0)
	{
		MessageBox(0, "Can't Find A Suitable PixelFormat.", "Error", MB_OK | MB_ICONERROR);
		PostQuitMessage(0);
		return NULL;
	}

	if (SetPixelFormat(pgraph->hDC, PFDID, &pfd) == false)
	{
		MessageBox(0, "Can't Set The PixelFormat.", "Error", MB_OK | MB_ICONERROR);
		PostQuitMessage(0);
		return NULL;
	}

	// Rendering Context

	pgraph->hRC = wglCreateContext(pgraph->hDC);

	if (pgraph->hRC == 0)
	{
		MessageBox(0, "Can't Create A GL Rendering Context.", "Error", MB_OK | MB_ICONERROR);
		PostQuitMessage(0);
		return NULL;
	}

	if (wglMakeCurrent(pgraph->hDC, pgraph->hRC) == false)
	{
		MessageBox(0, "Can't activate GLRC.", "Error", MB_OK | MB_ICONERROR);
		PostQuitMessage(0);
		return NULL;
	}
	pgraph->bLogging = logging;
	GetClientRect(pgraph->hGraphWnd, &DispArea);
	InitGL(pgraph, DispArea.right, DispArea.bottom);
	return pgraph;
}

/*-------------------------------------------------------------------------
	GetGraphRC: return the graph state from bRunning
  -------------------------------------------------------------------------*/

BOOL GetGraphState(HGRAPH hGraph)
{
	PGRAPHSTRUCT pgraph = NULL;
	pgraph = (PGRAPHSTRUCT)hGraph;
	if (NULL == pgraph)
		return FALSE;

	return pgraph->bRunning;

}

/*-------------------------------------------------------------------------
	GetGraphRC: return the graph render context
  -------------------------------------------------------------------------*/

HGLRC GetGraphRC(HGRAPH hGraph)
{
	// Sanity check

	if (NULL == hGraph)
		return NULL;

	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	if (NULL == pgraph->hRC)
		return NULL;

	return pgraph->hRC;
}

/*-------------------------------------------------------------------------
	GetGraphDC: return the graph device context
  -------------------------------------------------------------------------*/

HDC GetGraphDC(HGRAPH hGraph)
{
	// Sanity check

	if (NULL == hGraph)
		return NULL;

	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	if (NULL == pgraph->hDC)
		return NULL;

	return pgraph->hDC;
}

/*-------------------------------------------------------------------------
	GetGraphParentWnd: return the graph parent HWND
  -------------------------------------------------------------------------*/

HWND GetGraphParentWnd(HGRAPH hGraph)
{
	// Sanity check

	if (NULL == hGraph)
		return NULL;

	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	if (NULL == pgraph->hParentWnd)
		return NULL;

	return pgraph->hParentWnd;
}

/*-------------------------------------------------------------------------
	GetGraphWnd: return the graph HWND
  -------------------------------------------------------------------------*/

HWND GetGraphWnd(HGRAPH hGraph)
{
	// Sanity check

	if (NULL == hGraph)
		return NULL;

	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	if (NULL == pgraph->hGraphWnd)
		return NULL;

	return pgraph->hGraphWnd;
}

/*-------------------------------------------------------------------------
	GetGraphSignalNumber: return the total signals number
  -------------------------------------------------------------------------*/

INT GetGraphSignalCount(HGRAPH hGraph)
{
	// Sanity check

	if (NULL == hGraph)
		return NULL;

	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	if (NULL == pgraph->signalcount)
		return NULL;

	return pgraph->signalcount;
}


VOID AddPoints(HGRAPH hGraph, float* y, INT PointsCount)
{
	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	DATA* pDATA = NULL;
	if (cs.DebugInfo == NULL)
	{
		return;
	}

	EnterCriticalSection(&cs);

	// Sanity check

	if (NULL == pgraph || FALSE == pgraph->bRunning)
	{
		LeaveCriticalSection(&cs);
		return;
	}

	// TODO: Check if signalcount = length of y!

	if (pgraph->signalcount != PointsCount)
	{
		LeaveCriticalSection(&cs);
		return;
	}

	// If the maximum points are reach 
	// in the buffer, shift left the array
	// dec the current number of points

	if (pgraph->cur_nbpoints == NBPOINTS)
	{
		for (int index = 0; index < pgraph->signalcount; index++)
		{
			pDATA = pgraph->signal[index];
			for (int j = 0; j < NBPOINTS - 1; j++)
			{
				pDATA->X[j] = pDATA->X[j + 1];																// Shift left X
				pDATA->Y[j] = pDATA->Y[j + 1];																// Shift left Y
			}
		}
		pgraph->cur_nbpoints--;																				// Update the current point number
	}

	// Save the actual timestamp

	if (pgraph->cur_nbpoints == 0)
	{
		finish = start;
	}
	else
	{
		finish = PerformanceCounter();
	}

	if (logfile)
	{
		fprintf(logfile, "%lf\t", (float)((finish - start)) / frequency);
	}
	for (int index = 0; index < pgraph->signalcount; index++)
	{
		pDATA = pgraph->signal[index];
		if (NULL == pDATA)
		{
			LeaveCriticalSection(&cs);
			return;
		}

		// Add points to the selected buffer	

		pDATA->X[pgraph->cur_nbpoints] = (float)((finish - start)) / frequency;								// Save in X the elapsed time from start
		pDATA->Y[pgraph->cur_nbpoints] = y[index];															// Save Y
		if (logfile)
		{
			fprintf(logfile, "%lf\t", y[index]);
		}
	}
	if (logfile)
	{
		fprintf(logfile, "\n");
	}
	pgraph->cur_nbpoints++;

	// Inc the number of points
	LeaveCriticalSection(&cs);
}

/*-------------------------------------------------------------------------
	Render: Analyze the data buffers and print waves to the
	OpenGL device context
  -------------------------------------------------------------------------*/

BOOL Render(HGRAPH hGraph)
{
	// Sanity check

	if (NULL == hGraph)
		return FALSE;

	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	if (NULL == pgraph->hGraphWnd)
		return FALSE;

	if (pgraph->cur_nbpoints > 0)
	{
		EnterCriticalSection(&cs);

		//long long t1 = PerformanceCounter(); //DBG

		//UpdateBorder(hGraph); // DBG: Y display overflow
		update_border(hGraph);																			// border determination for each signal: meaning finding X and Y min max values
		//DATA** sig = pgraph->signal; //DBG

		//long long t2 = PerformanceCounter(); //DBG
		//double freq = (double)((t2 - t1)) / frequency * 1000; //DBG
		//printf("\rupdate_border take: %lf ms\r", freq); //DBG

		memset(SnapPlot, 0, sizeof(DATA));																// Reset computing datas
		FindGlobalMaxScale(hGraph, SnapPlot->Xmin, SnapPlot->Xmax, SnapPlot->Ymin, SnapPlot->Ymax);		// Finding the Y min and max of all the signals to scale on ite
		normalize_data(hGraph, SnapPlot->Xmin, SnapPlot->Xmax, SnapPlot->Ymin, SnapPlot->Ymax);			// normalize between [0;1]
		GetClientRect(pgraph->hGraphWnd, &DispArea);

		// Use the Projection Matrix

		glMatrixMode(GL_PROJECTION);

		// Reset Matrix

		glLoadIdentity();

		// Set the correct perspective.

		gluOrtho2D(-0.08, 1.04, 0 - 0.08, 1 + 0.02);
		glViewport(0, 0, DispArea.right, DispArea.bottom);

		// Use the Model Matrix

		glMatrixMode(GL_MODELVIEW);

		// Reset Matrix

		glLoadIdentity();

		// Clear Color and Depth Buffers

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// to run once

		glClear(GL_COLOR_BUFFER_BIT);

		DrawGraphSquare();
		DrawGridLines();

		if (SnapPlot->Ymax != SnapPlot->Ymin)
		{
			float txtlen = 0.0;
			float txtheight = 0.0;
			char value[32];
			char Xname[] = "Time (s)";
			float reelval = SnapPlot->Ymin;
			int div = 5;

			// draw points

			DrawWave(hGraph);

			// draw indicators

			glColor3f(0.1f, 0.1f, 0.1f);

			// zero

			float zero = GetStandardizedData(0, SnapPlot->Ymin, SnapPlot->Ymax);
			if (zero > 0 && zero < 1)
			{
				DrawString(-0.02f, zero - 0.01f, (char*)"0");
				DrawCursor(0.0f, zero);
			}

			// Determine the length of a typical string for resizing purpose

			RECT r;
			GetWindowRect(GetGraphWnd(hGraph), &r);
			txtlen = (float)dispStringWidth.cx / r.right; // Normalize the width of the Y value characters between [0-1]
			txtheight = (float)dispStringWidth.cy / r.bottom;

			//Xmin

			DrawString(-txtlen / 1.2f, -0.05f, ftos(value, sizeof(value), SnapPlot->Xmin));

			//Xmax

			DrawString(1 - txtlen / 1.2f, -0.05f, ftos(value, sizeof(value), SnapPlot->Xmax));

			//Time (s)

			DrawString(0.5f - (txtlen / 2.0f), -0.05f, Xname);

			// Ymin to Ymax values

			for (float ytmp = 0.0f; ytmp <= 1.0f; ytmp += 1.0f / div)
			{
				DrawString(-txtlen * 1.8f, ytmp - ((txtheight * 0.8f) / 2.0f), ftos(value, sizeof(value), reelval));
				reelval += (SnapPlot->Ymax - SnapPlot->Ymin) / div;
			}
		}
		LeaveCriticalSection(&cs);
		SwapBuffers(GetGraphDC(hGraph));
	}
	else if (pgraph->cur_nbpoints == 0 && start == 0.0f)														// Display a void graph when app start only
	{
		char value[32];
		char Xname[] = "Time (s)";
		float txtlen = 0.0;
		float txtheight = 0.0;
		RECT r;
		SnapPlot->Xmin = 0.0f;
		SnapPlot->Xmax = 1.0f;
		SnapPlot->Ymin = 0.0f;
		SnapPlot->Ymax = 1.0f;
		GetClientRect(pgraph->hGraphWnd, &DispArea);

		// Use the Projection Matrix

		glMatrixMode(GL_PROJECTION);

		// Reset Matrix

		glLoadIdentity();

		// Set the correct perspective.

		gluOrtho2D(-0.08, 1.04, 0 - 0.08, 1 + 0.02);
		glViewport(0, 0, DispArea.right, DispArea.bottom);

		// Use the Projection Matrix

		glMatrixMode(GL_MODELVIEW);

		// Reset Matrix

		glLoadIdentity();

		// Clear Color and Depth Buffers

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// to run once

		glClear(GL_COLOR_BUFFER_BIT);
		DrawGraphSquare();
		DrawGridLines();

		// draw indicators

		glColor3f(0.1f, 0.1f, 0.1f);

		// zero

		float zero = GetStandardizedData(0, SnapPlot->Ymin, SnapPlot->Ymax);
		if (zero > 0 && zero < 1)
		{
			//DrawString(-0.05, zero, (char*)"0");
			DrawCursor(0.0f, zero);
		}

		// Determine the length of a typical string for resizing purpose

		GetWindowRect(GetGraphWnd(hGraph), &r);
		txtlen = (float)dispStringWidth.cx / r.right; // Normalize the width of the Y value characters between [0-1]
		txtheight = (float)dispStringWidth.cy / r.bottom;

		//Xmin

		DrawString(-txtlen / 1.2f, -0.05f, ftos(value, sizeof(value), SnapPlot->Xmin));

		//Xmax

		DrawString(1 - txtlen / 1.2f, -0.05f, ftos(value, sizeof(value), SnapPlot->Xmax));

		//Time (s)

		DrawString(0.5f - (txtlen / 2.0f), -0.05f, Xname);

		// Ymin to Ymax values

		float reelval = SnapPlot->Ymin;
		int div = 5;
		for (float ytmp = 0.0f; ytmp <= 1.0f; ytmp += 1.0f / div)
		{
			DrawString(-txtlen * 1.8f, ytmp - ((txtheight * 0.8f) / 2.0f), ftos(value, sizeof(value), reelval));
			reelval += (SnapPlot->Ymax - SnapPlot->Ymin) / div;
		}
		SwapBuffers(GetGraphDC(hGraph));
	}
	return TRUE;
}

/*-------------------------------------------------------------------------
	ReshapeGraph: When resize message is proceed update graph pos
  -------------------------------------------------------------------------*/

VOID ReshapeGraph(HGRAPH hGraph, int left, int top, int right, int bottom)
{
	HWND hitem = GetGraphWnd(hGraph);
	if (SetWindowPos(hitem, NULL, left, top, right, bottom, SWP_SHOWWINDOW))
	{
		SetGLView(right, bottom);
	}
	else
	{
		printf("    [!] Error at SetWindowPos() with code: 0x%x\n", GetLastError());
	}
}

/*-------------------------------------------------------------------------
	InitGL: Setup the font used, set the correct OpenGL view at init
  -------------------------------------------------------------------------*/

VOID InitGL(HGRAPH hGraph, int Width, int Height)    // Called after the main window is created
{
	glShadeModel(GL_SMOOTH);                // Enable Smooth Shading
	BuildFont(hGraph);                        // Build The Font

	SetGLView(Width, Height);
}

/*-------------------------------------------------------------------------
	SetGLView: Make background and check for errors
  -------------------------------------------------------------------------*/

VOID SetGLView(int Width, int Height)
{
	glClearColor(0.98f, 0.98f, 0.98f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	//CheckErr();
}

/*-------------------------------------------------------------------------
	BuildFont: Create a Windows Bitmap font to write the device context with
  -------------------------------------------------------------------------*/
  //https://nehe.gamedev.net/tutorial/bitmap_fonts/17002/

GLvoid BuildFont(HGRAPH hGraph)
{
	HFONT   font;															// Windows Font ID
	HFONT   oldfont;														// Used For Good House Keeping

	base = glGenLists(96);													// Storage For 96 Characters ( NEW )

	font = CreateFont(-12,													// Height Of Font ( NEW )
		0,																	// Width Of Font
		0,																	// Angle Of Escapement
		0,																	// Orientation Angle
		FW_NORMAL,															// Font Weight
		FALSE,																// Italic
		FALSE,																// Underline
		FALSE,																// Strikeout
		ANSI_CHARSET,														// Character Set Identifier
		OUT_TT_PRECIS,														// Output Precision
		CLIP_DEFAULT_PRECIS,												// Clipping Precision
		ANTIALIASED_QUALITY,												// Output Quality
		FF_DONTCARE | DEFAULT_PITCH,										// Family And Pitch
		"Verdana");         // Font Name
	oldfont = (HFONT)SelectObject(GetGraphDC(hGraph), font);				// Selects The Font We Want
	wglUseFontBitmaps(GetGraphDC(hGraph), 32, 96, base);					// Builds 96 Characters Starting At Character 32
	SelectObject(GetGraphDC(hGraph), oldfont);								// Selects The Font We Want

	// Badly retrieving the length in pixel of "-0.0000" string for sizing purpose: to be check
	///////////////////////////////////////////////////////////////////////////////////////////
	char text[] = "-0.0000";
	SelectObject(GetGraphDC(hGraph), font);
	SetTextCharacterExtra(GetGraphDC(hGraph), 1);
	GetTextExtentPoint32A(GetGraphDC(hGraph), text, strlen(text), &dispStringWidth);
	dispStringWidth.cx -= GetTextCharacterExtra(GetGraphDC(hGraph)) * (strlen(text) - 2);
	///////////////////////////////////////////////////////////////////////////////////////////
	DeleteObject(font);														// Delete The Font
}

/*-------------------------------------------------------------------------
	KillFont: Free the font from OpenGL
  -------------------------------------------------------------------------*/
  //https://nehe.gamedev.net/tutorial/bitmap_fonts/17002/

GLvoid KillFont(GLvoid)														// Delete The Font List
{
	glDeleteLists(base, 96);												// Delete All 96 Characters ( NEW )
}

/*-------------------------------------------------------------------------
	glPrint: TBD
  -------------------------------------------------------------------------*/
  //https://nehe.gamedev.net/tutorial/bitmap_fonts/17002/

GLvoid glPrint(const char* fmt, ...)										// Custom GL "Print" Routine
{
	char        text[256];													// Holds Our String
	va_list     ap;															// Pointer To List Of Arguments

	if (fmt == NULL)														// If There's No Text
		return;																// Do Nothing

	va_start(ap, fmt);														// Parses The String For Variables
	vsprintf_s(text, fmt, ap);												// And Converts Symbols To Actual Numbers
	va_end(ap);																// Results Are Stored In Text
	glPushAttrib(GL_LIST_BIT);												// Pushes The Display List Bits     ( NEW )
	glListBase(base - 32);													// Sets The Base Character to 32    ( NEW )
	glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);						// Draws The Display List Text  ( NEW )
	glPopAttrib();															// Pops The Display List Bits   ( NEW )
}

/*-------------------------------------------------------------------------
	glPrint: Scanning arrays to determine the border
  -------------------------------------------------------------------------*/

BOOL FindGlobalMaxScale(HGRAPH hGraph, float& Xmin, float& Xmax, float& Ymin, float& Ymax)
{
	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	if (0 != pgraph->signalcount)
	{
		Xmin = pgraph->signal[0]->Xmin;										// save first value
		Xmax = pgraph->signal[0]->Xmax;										// save first value	
		Ymin = pgraph->signal[0]->Ymin;										// save first value
		Ymax = pgraph->signal[0]->Ymax;										// save first value

		for (int index = 1; index < pgraph->signalcount; index++)			// Iterate
		{
			if (Xmin > pgraph->signal[index]->Xmin)
				Xmin = pgraph->signal[index]->Xmin;							// Update if needed

			if (Xmax < pgraph->signal[index]->Xmax)
				Xmax = pgraph->signal[index]->Xmax;							// Update if needed

			if (Ymin > pgraph->signal[index]->Ymin)
				Ymin = pgraph->signal[index]->Ymin;							// Update if needed

			if (Ymax < pgraph->signal[index]->Ymax)
				Ymax = pgraph->signal[index]->Ymax;							// Update if needed
		}
		return TRUE;
	}
	return FALSE;
}



/*-------------------------------------------------------------------------
	DrawWave: Compute every signals for display
  -------------------------------------------------------------------------*/

VOID DrawWave(HGRAPH hGraph)
{
	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	DATA* pDATA;
	glLineWidth(2);

	// Color map

	float COLORS[16][3] =
	{
		{0.1f, 0.15f, 0.15f},
		{0.5f, 0.15f, 0.15f},
		{1.0f, 0.15f, 0.15f},
		{0.1f, 0.5f, 0.15f},
		{0.1f, 0.5f, 0.9f},
		{0.9f, 0.5f, 0.15f},
		{0.1f, 0.15f, 0.5f},
		{0.1f, 0.9f, 0.15f},
		{0.9f, 0.15f, 0.15f},
		{0.1f, 0.15f, 0.15f},
		{0.1f, 0.4f, 0.15f},
		{0.1f, 0.15f, 0.15f},
		{0.1f, 0.7f, 0.15f},
		{0.1f, 0.15f, 0.15f},
		{0.8f, 0.15f, 0.15f},
		{0.1f, 0.8f, 0.8f}
	};

	for (int index = 0; index < pgraph->signalcount; index++)
	{
		pDATA = pgraph->signal[index];
		glColor3f(COLORS[index][0], COLORS[index][1], COLORS[index][2]); // Colors of the signal
		glBegin(GL_LINE_STRIP);
		for (int i = 0; i < pgraph->cur_nbpoints; i++)
		{
			// prevent NAN
			if (pDATA->Xnorm[i] != pDATA->Xnorm[i] || pDATA->Ynorm[i] != pDATA->Ynorm[i])
				continue;

			glVertex2f(TakeFiniteNumber(pDATA->Xnorm[i]), TakeFiniteNumber(pDATA->Ynorm[i])); // Create the curve in memory
		}
		glEnd();
	}
}

/*-------------------------------------------------------------------------
	DrawString: Display character in OpenGL
  -------------------------------------------------------------------------*/

VOID DrawString(float x, float y, char* string)
{
	glRasterPos2f(x, y);
	glPrint(string);  // Print GL Text To The Screen
}

/*-------------------------------------------------------------------------
	DrawGraphSquare: Make the graph boxing here
  -------------------------------------------------------------------------*/

VOID DrawGraphSquare(VOID)
{
	// Set boxing

	glLineWidth(3);
	glColor3f(0.0f, 0.0f, 0.0f);
	glBegin(GL_LINE_STRIP);
	glVertex2i(0, 0);
	glVertex2i(1, 0);
	glVertex2i(1, 1);
	glVertex2i(0, 1);
	glVertex2i(0, 0);
	glEnd();

	// Set colored font rectangle

	glColor3f(0.988f, 0.99f, 1.0f);
	glBegin(GL_POLYGON);
	glVertex2i(0, 0);
	glVertex2i(0, 1);
	glVertex2i(1, 1);
	glVertex2i(1, 0);
	glEnd();
}

/*-------------------------------------------------------------------------
	DrawGridLines: draw a grid in the square
  -------------------------------------------------------------------------*/

VOID DrawGridLines(VOID)
{
	// Draw fine grid 

	glLineWidth(0.5);
	glColor3f(0.5F, 0.5F, 0.5F);
	glBegin(GL_LINES);
	for (float xtmp = 0.0f; xtmp < 1.0f; xtmp += 0.05f)
	{
		glVertex2f(xtmp, 0.0);
		glVertex2f(xtmp, 1.0);
		glVertex2f(0.0, xtmp);
		glVertex2f(1.0, xtmp);
	};
	glEnd();

	//Draw Grid 

	glLineWidth(1);
	glColor3f(0.3F, 0.1F, 0.0F);
	glBegin(GL_LINES);
	for (float xtmp = 0.0f; xtmp < 1.0f; xtmp += 0.20f)
	{
		glVertex2f(xtmp, 0.0);
		glVertex2f(xtmp, 1.0);
		glVertex2f(0.0, xtmp);
		glVertex2f(1.0, xtmp);
	};
	glEnd();
}

/*-------------------------------------------------------------------------
	DrawCursor: draw a moveable triangle on the graph
  -------------------------------------------------------------------------*/

VOID DrawCursor(float x, float y)
{
	glLineWidth(2.0f);
	glColor3f(0.0f, 0.0f, 0.0f);
	glBegin(GL_TRIANGLES);
	glVertex2f(x - 0.01f, y + 0.01f);
	glVertex2f(x, y);
	glVertex2f(x - 0.01f, y - 0.01f);
	glEnd();
}

/*-------------------------------------------------------------------------
	TakeFiniteNumber: Ensure to return a reel value as the graph can't plot
	+-INF value. A rounding happen here
  -------------------------------------------------------------------------*/

inline float TakeFiniteNumber(float x)
{
	// used to prevent -INF && +INF for computation
	// always return a real value closest to the limit

	if (x <= -FLT_MAX)
	{
		return -FLT_MAX;
	}
	if (x >= +FLT_MAX)
	{
		return FLT_MAX;
	}
	return x;
}

/*-------------------------------------------------------------------------
	FindFirstFiniteNumber: Iterate the array and returning the first reel
	finite number
  -------------------------------------------------------------------------*/

float FindFirstFiniteNumber(float* tab, int length)
{
	int i = 0;
	do
	{
		if (tab[i] == tab[i])
		{
			return tab[i];
		}
		i++;
	} while (i <= length);
	return 0;
}

/*-------------------------------------------------------------------------
	ftos: format a float value to a str representation
  -------------------------------------------------------------------------*/

LPSTR ftos(LPSTR str, int len, float value)
{
	sprintf_s(str, len, "%.1lf", value);
	return str;
}

float GetStandardizedData(float X, float min, float max)
{
	float ret;
	ret = (X - min) / (max - min);
	// initial value = (max - X) / (max - min);
	return ret;
}

VOID normalize_data(HGRAPH hGraph, float Xmin, float Xmax, float Ymin, float Ymax)
{
	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	for (int index = 0; index < pgraph->signalcount; index++)
	{
		for (int x = 0; x < pgraph->cur_nbpoints; x++)
		{

			// prevent Nan numbers

			if (pgraph->signal[index]->Y[x] != pgraph->signal[index]->Y[x] || Xmax == Xmin)
				continue;

			// Xnorm = (X - min) / (max - min);

			pgraph->signal[index]->Xnorm[x] = (pgraph->signal[index]->X[x] - Xmin) / (Xmax - Xmin);
			pgraph->signal[index]->Ynorm[x] = (pgraph->signal[index]->Y[x] - Ymin) / (Ymax - Ymin);

			// prevent Nan numbers in normalized buffer
			////////////////////////////////////////////////////////////////////////////////!
			// Error prone = 0.0

			if (pgraph->signal[index]->Ynorm[x] != pgraph->signal[index]->Ynorm[x])
				pgraph->signal[index]->Y[x] = 0.0;


		}
	}
}

VOID UpdateBorder(HGRAPH hGraph)
{
	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;
	static int AnalizedPts = 0;
	if (0 == pgraph->cur_nbpoints)
	{
		for (int index = 0; index <= pgraph->signalcount - 1; index++)
		{
			pgraph->signal[index]->Xmin = 0.0f;
			pgraph->signal[index]->Xmax = 0.0f;
			pgraph->signal[index]->Ymin = 0.0f;
			pgraph->signal[index]->Ymax = 0.0f;
		}
		return;
	}

	if (1 == pgraph->cur_nbpoints)
	{
		for (int index = 0; index <= pgraph->signalcount - 1; index++)
		{
			pgraph->signal[index]->Xmin = TakeFiniteNumber(pgraph->signal[index]->X[AnalizedPts]);
			pgraph->signal[index]->Xmax = TakeFiniteNumber(pgraph->signal[index]->X[AnalizedPts]);
			pgraph->signal[index]->Ymin = TakeFiniteNumber(pgraph->signal[index]->Y[AnalizedPts]);
			pgraph->signal[index]->Ymax = TakeFiniteNumber(pgraph->signal[index]->Y[AnalizedPts]);
		}
	}

	if (1 < pgraph->cur_nbpoints)
	{

		for (int index = 0; index <= pgraph->signalcount - 1; index++)
		{
			int CurrentPoint = AnalizedPts;
			for (CurrentPoint; CurrentPoint < pgraph->cur_nbpoints; CurrentPoint++)
			{
				//if (TakeFiniteNumber(pgraph->signal[index]->X[CurrentPoint]) < pgraph->signal[index]->Xmin)
					//pgraph->signal[index]->Xmin = TakeFiniteNumber(pgraph->signal[index]->X[CurrentPoint]);

				pgraph->signal[index]->Xmin = TakeFiniteNumber(pgraph->signal[index]->X[0]);

				if (TakeFiniteNumber(pgraph->signal[index]->X[CurrentPoint]) > pgraph->signal[index]->Xmax)
					pgraph->signal[index]->Xmax = TakeFiniteNumber(pgraph->signal[index]->X[CurrentPoint]);

				if (TakeFiniteNumber(pgraph->signal[index]->Y[CurrentPoint]) < pgraph->signal[index]->Ymin)
					pgraph->signal[index]->Ymin = TakeFiniteNumber(pgraph->signal[index]->Y[CurrentPoint]);

				if (TakeFiniteNumber(pgraph->signal[index]->Y[CurrentPoint]) > pgraph->signal[index]->Ymax)
					pgraph->signal[index]->Ymax = TakeFiniteNumber(pgraph->signal[index]->Y[CurrentPoint]);
			}
		}
	}
	AnalizedPts = pgraph->cur_nbpoints - 1;
	if (pgraph->cur_nbpoints == NBPOINTS)
		AnalizedPts--;
}

VOID update_border(HGRAPH hGraph)
{
	PGRAPHSTRUCT pgraph = (PGRAPHSTRUCT)hGraph;

	for (int index = 0; index <= pgraph->signalcount - 1; index++)
	{
		// Xmin determination

		// prevent Nan numbers
		if (pgraph->signal[index]->X[0] != pgraph->signal[index]->X[0])
		{
			if (pgraph->cur_nbpoints > 0)
			{
				// check next value until the end of buff
				for (int i = 1; i < pgraph->cur_nbpoints; i++)
				{
					// prevent Nan numbers
					if (pgraph->signal[index]->X[i] != pgraph->signal[index]->X[i])
					{
						continue;
					}
					// Prevent INF values with TakeFiniteNumber
					pgraph->signal[index]->Xmin = TakeFiniteNumber(pgraph->signal[index]->X[i]);
				}
			}
			else
			{
				// No points to compute: this value is already checked in render, overcheck?
			}
		}
		else
		{
			// Prevent INF values with TakeFiniteNumber

			pgraph->signal[index]->Xmin = TakeFiniteNumber(pgraph->signal[index]->X[0]);
		}

		// Xmax determination

		if (pgraph->cur_nbpoints - 1 > 0)
			pgraph->signal[index]->Xmax = TakeFiniteNumber(pgraph->signal[index]->X[pgraph->cur_nbpoints - 1]);
		else
			pgraph->signal[index]->Xmax = 0.0f;

		// Ymin determination

		pgraph->signal[index]->Ymin = FindFirstFiniteNumber(pgraph->signal[index]->Y, pgraph->cur_nbpoints - 1);
		// check next value until the end of buff
		for (int i = 0; i < pgraph->cur_nbpoints; i++)
		{
			// prevent Nan numbers

			if (pgraph->signal[index]->Y[i] != pgraph->signal[index]->Y[i])
			{
				continue;
			}
			//prevent INF values
			if (pgraph->signal[index]->Y[i] < pgraph->signal[index]->Ymin)
				pgraph->signal[index]->Ymin = TakeFiniteNumber(pgraph->signal[index]->Y[i]);
		}

		// Ymax determination

		pgraph->signal[index]->Ymax = FindFirstFiniteNumber(pgraph->signal[index]->Y, pgraph->cur_nbpoints - 1);
		// check next value until the end of buff
		for (int i = 0; i < pgraph->cur_nbpoints; i++)
		{
			// prevent Nan numbers

			if (pgraph->signal[index]->Y[i] != pgraph->signal[index]->Y[i])
			{
				continue;
			}

			//prevent INF values

			if (pgraph->signal[index]->Y[i] > pgraph->signal[index]->Ymax)
				pgraph->signal[index]->Ymax = TakeFiniteNumber(pgraph->signal[index]->Y[i]);
		}
	}
}

//https://www.pluralsight.com/blog/software-development/how-to-measure-execution-time-intervals-in-c--
inline long long PerformanceFrequency()
{
	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);
	return li.QuadPart;
}
//https://www.pluralsight.com/blog/software-development/how-to-measure-execution-time-intervals-in-c--
inline long long PerformanceCounter()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}



/*-------------------------------------------------------------------------
	CheckErr: try to catch openGL error messages
  -------------------------------------------------------------------------*/

VOID CheckErr(VOID)
{
	GLenum err = 0;
	err = glGetError();
	switch (err)
	{
	case GL_NO_ERROR:
		printf("	[!] GL_NO_ERROR\n");
		break;

	case GL_INVALID_ENUM:
		printf("	[!] GL_INVALID_ENUM");
		break;

	case GL_INVALID_VALUE:
		printf("	[!] GL_INVALID_VALUE");
		break;

	case GL_INVALID_OPERATION:
		printf("	[!] GL_INVALID_OPERATION");
		break;

	case GL_STACK_OVERFLOW:
		printf("	[!] GL_STACK_OVERFLOW");
		break;

	case GL_STACK_UNDERFLOW:
		printf("	[!] GL_STACK_UNDERFLOW");
		break;

	case GL_OUT_OF_MEMORY:
		printf("	[!] GL_OUT_OF_MEMORY");
		break;

	default:
		break;
	}
}