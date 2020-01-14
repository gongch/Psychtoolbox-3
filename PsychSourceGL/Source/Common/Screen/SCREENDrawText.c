/*
    SCREENDrawText.c

    AUTHORS:

        Allen.Ingling@nyu.edu           awi
        mario.kleiner.de@gmail.com      mk

    PLATFORMS:

        All. With OS specific #ifdefs...

    HISTORY:

        11/17/03    awi     Spun off from SCREENTestTexture which also used Quartz and Textures to draw text but did not match the 'DrawText' specifications.
        10/12/04    awi     In useString: changed "SCREEN" to "Screen", and moved commas to inside [].
        2/25/05     awi     Added call to PsychUpdateAlphaBlendingFactorLazily().  Drawing now obeys settings by Screen('BlendFunction').
        5/08/05     mk      Bugfix for "Descenders of letters get cut/eaten away" bug introduced in PTB 1.0.5
        10/12/05    mk      Fix crash in DrawText caused by removing glFinish() while CLIENT_STORAGE is enabled!
                            -> Disabling CLIENT_STORAGE and removing glFinish() is the proper solution...
        11/01/05    mk      Finally the real bugfix for "Descenders of letters get cut/eaten away" bug introduced in PTB 1.0.5!
        11/01/05    mk      Removal of dead code + beautification.
        11/21/05    mk      Code for updating the "Drawing Cursor" and returning NewX, NewY values added.
        01/01/06    mk      Code branch for M$-Windoze implementation of DrawText added.
        11/11/07    mk      New GDI based Windoze text renderer implemented.
        12/27/09    mk      Massive refactoring of code for all platforms and support for plugin-based textrenderers.
                            -> Cleans up the mess of duplicated code and special cases. We share as much code as possible accross platforms.
                            -> Allows for unicode support on all platforms.
                            -> Allows for plugin renderer on Linux and OS/X for unicode, anti-aliasing etc.
                            -> Allows for better handling of unicode and multibyte character encodings.

        11/02/13    mk      Rewrite OSX renderer: Switch from deprecated ATSUI to "new" CoreText as supported on OSX 10.5 and later.

    DESCRIPTION:

        Unified file with text renderers for all platforms (OS/X, Windows, Linux).

    REFERENCES:

        http://www.cl.cam.ac.uk/~mgk25/unicode.html - A good FAQ about Unicode, UTF-8 with a special emphasis on Linux and Posix systems.

*/
#include "GL/glew.h"
#include "Screen.h"

// Reference to external dynamically loaded text renderer plugin:
static void* drawtext_plugin = NULL;
static psych_bool drawtext_plugin_firstcall = TRUE;

// Function prototypes for functions exported by drawtext plugins: Will be dynamically bound & linked:
int (*PsychPluginInitText)(void) = NULL;
int (*PsychPluginShutdownText)(int context) = NULL;
int (*PsychPluginSetTextFont)(int context, const char* fontName) = NULL;
const char* (*PsychPluginGetTextFont)(int context) = NULL;
int (*PsychPluginSetTextStyle)(int context, unsigned int fontStyle) = NULL;
int (*PsychPluginSetTextSize)(int context, double fontSize) = NULL;
void (*PsychPluginSetTextFGColor)(int context, double* color) = NULL;
void (*PsychPluginSetTextBGColor)(int context, double* color) = NULL;
void (*PsychPluginSetTextUseFontmapper)(unsigned int useMapper, unsigned int mapperFlags) = NULL;
void (*PsychPluginSetTextViewPort)(int context, double xs, double ys, double w, double h) = NULL;
int (*PsychPluginDrawText)(int context, double xStart, double yStart, int textLen, double* text) = NULL;
int (*PsychPluginMeasureText)(int context, int textLen, double* text, float* xmin, float* ymin, float* xmax, float* ymax, float* xadvance) = NULL;
void (*PsychPluginSetTextVerbosity)(unsigned int verbosity) = NULL;
void (*PsychPluginSetTextAntiAliasing)(int context, int antiAliasing) = NULL;
void (*PsychPluginSetAffineTransformMatrix)(int context, double matrix[2][3]) = NULL;
void (*PsychPluginGetTextCursor)(int context, double* xp, double* yp, double* height) = NULL;

// If you change useString then also change the corresponding synopsis string in ScreenSynopsis.
static char useString[] = "[newX,newY,textHeight]=Screen('DrawText', windowPtr, text [,x] [,y] [,color] [,backgroundColor] [,yPositionIsBaseline] [,swapTextDirection]);";
//                          1    2    3                              1          2      3    4    5        6                  7                      8

// Synopsis string for DrawText:
static char synopsisString[] =
    "Draw text. \"text\" may include Unicode characters (e.g. Chinese).\n"
    "A standard Matlab/Octave char()acter text string is interpreted according to Screen's "
    "current character encoding setting. By default this is the \"system default locale\", as "
    "selected in the language settings of your user account. You can change the encoding "
    "anytime via a call to Screen('Preference', 'TextEncodingLocale', newencoding); "
    "E.g., for UTF-8 multibyte character encoding you'd call Screen('Preference','TextEncodingLocale','UTF-8');\n"
    "If you have a non-ASCII text string and want to make sure that Matlab or Octave doesn't "
    "meddle with your string, convert it into a uint8() datatype before passing to this function.\n"
    "If you want to pass a string which contains unicode characters directly, convert the "
    "text to a double matrix, e.g., mytext = double(myunicodetext); then pass the double "
    "matrix to this function. Screen will interpret all double numbers directly as unicode "
    "code points.\n"
    "Unicode text drawing is supported on all operating systems if you select the default "
    "high quality text renderer. Of course you also have to select a text font which contains "
    "the unicode character sets you want to draw - not all fonts contain all unicode characters.\n"
    "The following optional parameters allow to control location and color of the drawn text:\n"
    "\"x\" \"y\" defines the text pen start location. Default is the location of the pen from "
    "previous draw text commands, or (0,0) at startup. \"color\" is the CLUT index (scalar or [r "
    "g b] triplet or [r g b a] quadruple) for drawing the text; startup default produces black.\n"
    "\"backgroundColor\" is the color of the background area behind the text. By default, "
    "text is drawn transparent in front of whatever image content is stored in the window. "
    "You need to set an explicit backgroundColor and possibly enable user defined alpha-blending "
    "with Screen('Preference', 'TextAlphaBlending', 1); and Screen('Blendfunction', ...) to make "
    "use of text background drawing. Appearance of the background + text may be different accross "
    "different operating systems and text renderers, or it may not be supported at all, so this is "
    "not a feature to rely on.\n"
    "\"yPositionIsBaseline\" If specified, will override the global preference setting for text "
    "positioning: It defaults to off. If it is set to 1, then the \"y\" pen start location defines "
    "the base line of drawn text, otherwise it defines the top of the drawn text. Old PTB's had a "
    "behaviour equivalent to setting 1, unfortunately this behaviour wasn't replicated in early "
    "versions of Psychtoolbox-3, so now we stick to the new behaviour by default.\n"
    "\"swapTextDirection\" If specified and set to 1, then the direction of the text is swapped "
    "from the default left-to-right to the swapped right-to-left direction, e.g., to handle scripts "
    "with right-to-left writing order like hebrew.\n"
    "\"newX, newY\" optionally return the final pen location.\n"
    "\"textHeight\" optionally return height of current text string. May return zero if this is "
    "not supported by the current text renderer.\n"
    "Btw.: Screen('Preference', ...); provides a couple of interesting text preference "
    "settings that affect text drawing, e.g., setting alpha blending and anti-aliasing modes.\n"
    "Selectable text renderers: The Screen('Preference', 'TextRenderer', Type); command allows "
    "to select among different text rendering engines with different properties:\n"
    "Type 0 is the legacy OS specific text renderer: On Linux this is implemented as a fast, "
    "but low quality OpenGL display list renderer without any support for unicode or text "
    "anti-aliasing. On MS-Windows, this is currently a GDI based renderer. On OSX this currently "
    "selects Apples CoreText text renderer, which is slow but "
    "does support anti-aliasing, unicode and other features. Normally you really don't want to use "
    "the type 0 legacy renderer. It is provided for backwards compatibility to old experiment scripts "
    "and may need to get removed completely in future versions of Psychtoolbox due to circumstances "
    "out of our control.\n"
    "Type 1 is the high quality renderer: It supports unicode, anti-aliasing, and many "
    "other interesting features. This is a renderer loaded from an external plugin, and based on FTGL "
    "for fast high quality text drawing with OpenGL.\n"
    "This function doesn't provide support for text layout. Use the higher level DrawFormattedText() function "
    "if you need basic support for text layout, e.g, centered text output, line wrapping etc.\n";

static char seeAlsoString[] = "TextBounds TextSize TextFont TextStyle TextColor TextBackgroundColor Preference";

// Implementations for Windows and Linux/X11:

#if PSYCH_SYSTEM == PSYCH_WINDOWS


// Microsoft-Windows implementation of DrawText...
// The code below will need to be restructured and moved to the proper
// places in PTB's source tree when things have stabilized a bit...

/* PsychOSReBuildFont
 *
 * (Re)Build a font for the specified winRec, based on OpenGL display lists.
 *
 * This routine examines the font settings for winRec and builds proper
 * OpenGL display lists that represent a font as close as possible to the
 * requested font. These routines are specific to Microsoft Windows, so they
 * need to be reimplemented for other OS'es...
 */
psych_bool PsychOSRebuildFont(PsychWindowRecordType *winRec)
{
    GLYPHMETRICSFLOAT   gmf[256];       // Address Buffer For Font Storage
    HFONT               font, oldfont;  // Windows Font ID
    GLuint              base;
    int                 i;

    // Does font need to be rebuild?
    if (!winRec->textAttributes.needsRebuild) {
        // No rebuild needed. We don't have anything to do.
        return(TRUE);
    }

    // Rebuild needed. Do we have already a display list?
    if (winRec->textAttributes.DisplayList > 0) {
        // Yep. Destroy it...
        glDeleteLists(winRec->textAttributes.DisplayList, 256);
        winRec->textAttributes.DisplayList=0;
    }

    // Create Windows font object with requested properties:
    font = NULL;
    font = CreateFont(((int) (-MulDiv(winRec->textAttributes.textSize, GetDeviceCaps(winRec->targetSpecific.deviceContext, LOGPIXELSY), 72))), // Height Of Font, aka textSize
                      0,                                                                // Width Of Font: 0=Match to height
                      0,                                                                // Angle Of Escapement
                      0,                                                                // Orientation Angle
                      ((winRec->textAttributes.textStyle & 1) ? FW_BOLD : FW_NORMAL),   // Font Weight
                      ((winRec->textAttributes.textStyle & 2) ? TRUE : FALSE),          // Italic
                      ((winRec->textAttributes.textStyle & 4) ? TRUE : FALSE),          // Underline
                      FALSE,                                                            // Strikeout: Set it to false until we know what it actually means...
                      ANSI_CHARSET,                                                     // Character Set Identifier: Would need to be set different for "WingDings" fonts...
                      OUT_TT_PRECIS,                                                    // Output Precision:   We try to get TrueType fonts if possible, but allow fallback to low-quality...
                      CLIP_DEFAULT_PRECIS,                                              // Clipping Precision: Use system default.
                      ANTIALIASED_QUALITY,                                              // Output Quality:     We want antialiased smooth looking fonts.
                      FF_DONTCARE|DEFAULT_PITCH,                                        // Family And Pitch:   Use system default.
                      (char*) winRec->textAttributes.textFontName);                     // Font Name as requested by user.

    // Child-protection:
    if (font==NULL) {
        // Something went wrong...
        PsychErrorExitMsg(PsychError_user, "Couldn't select the requested font with the requested font settings from Windows-OS! ");
        return(FALSE);
    }

    // Select the font we created: Retain old font handle for restore below...
    oldfont=SelectObject(winRec->targetSpecific.deviceContext, font);        // Selects The Font We Created

    // Activate OpenGL context:
    PsychSetGLContext(winRec);

    // Generate 256 display lists, one for each ASCII character:
    base = glGenLists(256);

    // Build the display lists from the font: We want an outline font instead of a bitmapped one.
    // Characters of outline fonts are build as real OpenGL 3D objects (meshes of connected polygons)
    // with normals, texture coordinates and so on, so they can be rendered and transformed in 3D, including
    // proper texturing and lighting...
    wglUseFontOutlines(winRec->targetSpecific.deviceContext,            // Select The Current DC
                        0,                                              // Starting Character is ASCII char zero.
                        256,                                            // Number Of Display Lists To Build: 256 for all 256 chars.
                        base,                                           // Starting Display List handle.
                        0.0f,                                           // Deviation From The True Outlines: Smaller value=Smoother, but more geometry.
                        0.2f,                                           // Font Thickness In The Z Direction for 3D rendering.
                        ((winRec->textAttributes.textStyle & 8) ? WGL_FONT_LINES : WGL_FONT_POLYGONS),  // Type of rendering: Filled polygons or just outlines?
                        gmf);                                           // Address Of Buffer To receive font metrics data.

    // Assign new display list:
    winRec->textAttributes.DisplayList = base;
    // Clear the rebuild flag:
    winRec->textAttributes.needsRebuild = FALSE;

    // Copy glyph geometry info into winRec:
    for(i=0; i<256; i++) {
        winRec->textAttributes.glyphWidth[i]=(float) gmf[i].gmfCellIncX;
        winRec->textAttributes.glyphHeight[i]=(float) gmf[i].gmfCellIncY;
    }

    // Clean up after font creation:
    SelectObject(winRec->targetSpecific.deviceContext, oldfont);
    DeleteObject(font);

    // Our new font is ready to rock!
    return(TRUE);
}

#endif

#if PSYCH_SYSTEM == PSYCH_WINDOWS

// GDI based text-renderer for MS-Windows:
//
// It's sloooow. However it provides accurate text positioning, Unicode rendering,
// anti-aliasing, proper text size and a higher quality text output in general.
//
// It uses GDI text renderer to render text to a memory device context,
// backed by a DIB device independent memory bitmap. Then it converts the
// DIB to an OpenGL compatible RGBA format and draws it via OpenGL,
// currently via glDrawPixels, in the future maybe via texture mapping if
// that should be faster.
//
// Reasons for slowness: GDI is slow and CPU only -- no GPU acceleration,
// GDI->OpenGL data format conversion (and our trick to get an anti-aliased
// alpha-channel) is slow and compute intense, data upload and blit in GL
// is slow due to hostmemory -> VRAM copy.

// The following variables must be released at Screen flush time the latest.
// The exit routine PsychCleanupTextRenderer() does this when invoked
// from the ScreenCloseAllWindows() function, as part of a Screen flush,
// error abort, or Screen('CloseAll').

// The current (last used) font for GDI text drawing:
static HFONT                    font=NULL;        // Handle to current font.

// These static variables hold the memory bitmap buffers (device contexts)
// for GDI based text drawing. We keep them accross calls to DrawText, and
// only allocate them on first invocation, or reallocate them when the size
// of the target window has changed.
static HDC                      dc = NULL;        // Handle to current memory device context.
static BYTE*                    pBits = NULL;    // Pointer to dc's DIB bitmap memory.
static HBITMAP                  hbmBuffer;        // DIB.
static HBITMAP                  defaultDIB;
static int                      oldWidth=-1;    // Size of last target window for drawtext.
static int                      oldHeight=-1;    // dto.
static PsychWindowRecordType*   oldWin = NULL; // Last window to which text was drawn to.


// End of Windows specific part...
#endif

// End of non-OS/X (= Linux & Windows) specific part...

#if PSYCH_SYSTEM == PSYCH_WINDOWS
// MS-Windows:
#include <locale.h>

// When building against Octave-3 or the Microsoft Windows common C runtime MSCRT.dll,
// we don't have support for _locale_t datatype and associated functions like mbstowcs_l.
// Therefore we always use setlocale() and mbstowcs() instead to set/query/use the global
// process-wide locale instead to avoid special cases. Our code will backup the old/current locale,
// then apply the requested locale and use it for text conversion, then restore the old locale,
// so that the process global locale setting is only temporarily changed during execution of our
// text conversion function on the main thread. This should hopefully be fine:
static char     oldmswinlocale[256] = { 0 };
static char     drawtext_localestring[256] = { 0 };
unsigned int    drawtext_codepage = 0;

#endif

// Allocate in a text string argument, either in some string or bytestring format or as double-vector.
// Return the strings representation as a double vector in Unicode encoding.
//
// 'position' the position of the string argument.
// 'isRequired' Is the string required or optional, or required to be of a specific type?
// 'textLength' On return, store length of text string in characters at the int pointer target location.
// 'unicodeText' On return, the double* to which unicodeText points, shall contain the start adress of a vector of
//               doubles. Each double encodes the unicode value of one unicode character in the string. Length of
//               the array as given in 'textLength'.
//
//  Returns TRUE on successfull allocation of an input string in unicode format. FALSE on any error.
//
psych_bool PsychAllocInTextAsUnicode(int position, PsychArgRequirementType isRequired, int *textLength, double **unicodeText)
{
    int                 dummy1, dummy2;
    unsigned char       *textByteString = NULL;
    char                *textCString = NULL;
    wchar_t             *textUniString = NULL;
    int                 stringLengthBytes = 0;

	//// Anything provided as argument? This checks for presence of the required arg. If an arg
	//// of mismatching type (not char or double) is detected, it errors-out. Otherwise it returns
	//// true on presence of a correct argument, false if argument is absent and optional.
	//if (!PsychCheckInputArgType(position, isRequired, (PsychArgType_char | PsychArgType_double | PsychArgType_uint8))) {
	//	// The optional argument isn't present. That means there ain't any work for us to do:
	//	goto allocintext_skipped;
	//}

	//// Some text string available, either double vector or char vector.
	//
	//// Text string at 'position' passed as C-language encoded character string or string of uint8 bytes?
 //   if ((PsychGetArgType(position) == PsychArgType_char) || (PsychGetArgType(position) == PsychArgType_uint8)) {
	//	// Try to allocate in as unsigned byte string:
	//	if (PsychAllocInUnsignedByteMatArg(position, kPsychArgAnything, &dummy1, &stringLengthBytes, &dummy2, &textByteString)) {
	//		// Yep: Convert to null-terminated string for further processing:
	//		if (dummy2!=1) PsychErrorExitMsg(PsychError_user, "Byte text matrices must be 2D matrices!");
	//		stringLengthBytes = stringLengthBytes * dummy1;
	//		
	//		// Nothing to do on empty string:
	//		if (stringLengthBytes < 1 || textByteString[0] == 0) goto allocintext_skipped;
	//		
	//		// A bytestring. Is it null-terminated? If not we need to make it so:
	//		if (textByteString[stringLengthBytes-1] != 0) {
	//			// Not null-terminated: Create a 1 byte larger temporary copy which is null-terminated:
	//			textCString = (char*) PsychMallocTemp(stringLengthBytes + 1);
	//			memcpy((void*) textCString, (void*) textByteString, stringLengthBytes);
	//			textCString[stringLengthBytes] = 0;
	//		}
	//		else {
	//			// Already null-terminated: Nice :-)
	//			textCString = (char*) textByteString;
	//		}
	//	}
	//	else {
	//		// Null terminated C-Language text string ie., a sequence of bytes. Get it:
	//		PsychAllocInCharArg(position, TRUE, &textCString);
	//	}
	//	
	//	// Get length in bytes, derived from location of null-terminator character:
	//	stringLengthBytes = strlen(textCString);
	//	
	//	// Empty string? If so, we skip processing:
	//	if (stringLengthBytes < 1) goto allocintext_skipped;
	//	
	//	#if PSYCH_SYSTEM == PSYCH_WINDOWS
	//		// Windows:
	//		// Compute number of Unicode wchar_t chars after conversion of multibyte C-String:
	//		if (drawtext_codepage) {
	//			// Codepage-based text conversion:
	//			*textLength = MultiByteToWideChar(drawtext_codepage, 0, textCString, -1, NULL, 0) - 1;
	//			if (*textLength <= 0) {
	//				printf("PTB-ERROR: MultiByteToWideChar() returned conversion error code %i.", (int) GetLastError());
	//				PsychErrorExitMsg(PsychError_user, "Invalid multibyte character sequence detected! Can't convert given char() string to Unicode for DrawText!");
	//			}
	//		}
	//		else {
	//			// Locale-based text conversion:
	//			#if defined(MATLAB_R11) || defined(PTBOCTAVE3MEX)
	//					*textLength = mbstowcs(NULL, textCString, 0);
	//			#else
	//					*textLength = mbstowcs_l(NULL, textCString, 0, drawtext_locale);
	//			#endif
	//		}
	//	#else
	//		// Unix: OS/X, Linux:
	//		*textLength = mbstowcs_l(NULL, textCString, 0, drawtext_locale);
	//	#endif
	//	
	//	if (*textLength < 0) PsychErrorExitMsg(PsychError_user, "Invalid multibyte character sequence detected! Can't convert given char() string to Unicode for DrawText!");
	//	
	//	// Empty string provided? Skip, if so.
	//	if (*textLength < 1) goto allocintext_skipped;
	//	
	//	// Allocate wchar_t buffer of sufficient size to hold converted unicode string:
	//	textUniString = (wchar_t*) PsychMallocTemp((*textLength + 1) * sizeof(wchar_t));
	//	
	//	// Perform conversion of multibyte character sequence to Unicode wchar_t:
	//	#if PSYCH_SYSTEM == PSYCH_WINDOWS
	//		// Windows:
	//		if (drawtext_codepage) {
	//			// Codepage-based text conversion:
	//			if (MultiByteToWideChar(drawtext_codepage, 0, textCString, -1, textUniString, (*textLength + 1)) <= 0) {
	//				printf("PTB-ERROR: MultiByteToWideChar() II returned conversion error code %i.", (int) GetLastError());
	//				PsychErrorExitMsg(PsychError_user, "Invalid multibyte character sequence detected! Can't convert given char() string to Unicode for DrawText!");
	//			}
	//		}
	//		else {
	//			// Locale-based text conversion:
	//			#if defined(MATLAB_R11) || defined(PTBOCTAVE3MEX)
	//				mbstowcs(textUniString, textCString, (*textLength + 1));
	//			#else
	//				mbstowcs_l(textUniString, textCString, (*textLength + 1), drawtext_locale);
	//			#endif
	//		}
	//	#else
	//		// Unix:
	//		mbstowcs_l(textUniString, textCString, (*textLength + 1), drawtext_locale);			
	//	#endif
	//	
	//	// Allocate temporary output vector of doubles and copy unicode string into it:
	//	*unicodeText = (double*) PsychMallocTemp((*textLength + 1) * sizeof(double));
	//	for (dummy1 = 0; dummy1 < (*textLength + 1); dummy1++) (*unicodeText)[dummy1] = (double) textUniString[dummy1];
	//}
	//else {
	//	// Not a character string: Check if it is a double matrix which directly encodes Unicode text:
	//	//PsychAllocInDoubleMatArg(position, TRUE, &dummy1, &stringLengthBytes, &dummy2, unicodeText);
	//	if (dummy2!=1) PsychErrorExitMsg(PsychError_user, "Unicode text matrices must be 2D matrices!");
	//	stringLengthBytes = stringLengthBytes * dummy1;
	//	
	//	// Empty string? If so, we skip processing:
	//	if(stringLengthBytes < 1) goto allocintext_skipped;

	//	// Nope. Assign output arguments. We can pass-through the unicode double vector as it is
	//	// already in the proper format:
	//	*textLength = stringLengthBytes;
	//}

	//if (PsychPrefStateGet_Verbosity() > 9) {
	//	printf("PTB-DEBUG: Allocated unicode string: ");
	//	for (dummy1 = 0; dummy1 < *textLength; dummy1++) printf("%f ", (float) (*unicodeText)[dummy1]);	
	//	printf("\n");
	//}

	// Successfully allocated a text string as Unicode double vector:
	return(TRUE);

// We reach this jump-label via goto if there isn't any text string to return:
allocintext_skipped:
    *textLength = 0;
    *unicodeText = NULL;
    return(FALSE);
}
