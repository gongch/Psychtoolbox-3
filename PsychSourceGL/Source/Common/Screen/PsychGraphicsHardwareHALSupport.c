/*
    PsychToolbox3/Source/Common/Screen/PsychGraphicsHardwareHALSupport.c

    AUTHORS:

        mario.kleiner.de@gmail.com  mk

    PLATFORMS:

        All. However with dependencies on OS specific glue-layers which are mostly Linux/OSX for now...

    HISTORY:

    01/12/2008  mk      Created.

    DESCRIPTION:

    This file is a container for miscellaneous routines that take advantage of specific low level
    features of graphics/related hardware and the target operating system to achieve special tasks.

    Most of the routines here are more tied to specific displays (screens) than to windows and usually
    only a subset of these routines is available for a specific system configuration with a specific
    model of graphics card. Other layers of PTB should not rely on these routines being supported on
    a given system config and should be prepared to have fallback-implementations.

    Many of the features are experimental in nature!

    TO DO:

*/

#include "PsychGraphicsHardwareHALSupport.h"

// Include specifications of the GPU registers:
#include "PsychGraphicsCardRegisterSpecs.h"

// Array with register offsets of the CRTCs used by AMD/ATI gpus.
// Initialized by OS specific screen glue, accessed from different locations:
unsigned int crtcoff[kPsychMaxPossibleCrtcs];

// Maps screenid's to Graphics hardware pipelines: Used to choose pipeline for beampos-queries and similar
// GPU crtc specific stuff. Each screen can have up to kPsychMaxPossibleCrtcs assigned. Slot 0 contains the
// primary crtc, used for beamposition timestamping, framerate queries etc. A -1 value in a slot terminates
// the sequence of assigned crtc's.
static int  displayScreensToCrtcIds[kPsychMaxPossibleDisplays][kPsychMaxPossibleCrtcs];
static int  displayScreensToPipes[kPsychMaxPossibleDisplays][kPsychMaxPossibleCrtcs];
static psych_bool displayScreensToCrtcIdsUserOverride = FALSE;
static psych_bool displayScreensToPipesAutoDetected = FALSE;

// Corrective values for beamposition queries to correct for any constant and systematic offsets in
// the scanline positions returned by low-level code:
static int  screenBeampositionBias[kPsychMaxPossibleDisplays];
static int  screenBeampositionVTotal[kPsychMaxPossibleDisplays];

/* PsychSynchronizeDisplayScreens() -- (Try to) synchronize display refresh cycles of multiple displays
 *
 * This tries whatever method is available/appropriate/or requested to synchronize the video refresh
 * cycles of multiple graphics cards physical display heads -- corresponding to PTB logical Screens.
 *
 * The method may or may not be supported on a specific OS/gfx-card combo. It will return a PsychError_unimplemented
 * if it can't do what core wants.
 *
 * numScreens   =   Ptr to the number of display screens to sync. If numScreens>0, all screens with the screenIds stored
 *                  in the integer array 'screenIds' will be synched. If numScreens == 0, PTB will try to sync all
 *                  available screens in the system. On return, the location will contain the count of synced screens.
 *
 * screenIds    =   Either a list with 'numScreens' screenIds for the screens to sync, or NULL if numScreens == 0.
 *
 * residuals    =   List with 'numScreens' (on return) values indicating the residual sync error wrt. to the first
 *                  screen (the reference). Ideally all items should contain zero for perfect sync on return.
 *
 * syncMethod   =   Numeric Id for the sync method to use: 0 = Don't care, whatever is appropriate. 1 = Only hard
 *                  sync, which is fast and reliable if supported. 2 = Soft sync via drift-syncing. More to come...
 *
 * syncTimeOut  =   If some non-immediate method is requested/chosen, it should give up after syncTimeOut seconds if
 *                  it doesn't manage to bring the displays in sync in that timeframe.
 *
 * allowedResidual = How many scanlines offset after sync are acceptable? Will retry until syncTimeOut if criterion not met.
 *
 */
PsychError PsychSynchronizeDisplayScreens(int *numScreens, int* screenIds, int* residuals, unsigned int syncMethod, double syncTimeOut, int allowedResidual)
{
    // Currently, we only support a hard, immediate sync of all display heads of a single dual-head gfx-card,
    // so we ignore most of our arguments. Well, still check them for validity, but then ignore them after
    // successfull validation ;-)

    if (numScreens == NULL) PsychErrorExitMsg(PsychError_internal, "NULL-Ptr passed as numScreens argument!");
    if (*numScreens < 0 || *numScreens > PsychGetNumDisplays()) PsychErrorExitMsg(PsychError_internal, "Invalid number passed as numScreens argument! (Negative or more than available screens)");
    if (syncMethod > 2) PsychErrorExitMsg(PsychError_internal, "Invalid syncMethod argument passed!");
    if (syncTimeOut < 0) PsychErrorExitMsg(PsychError_internal, "Invalid (negative) syncTimeOut argument passed!");
    if (allowedResidual < 0) PsychErrorExitMsg(PsychError_internal, "Invalid (negative) allowedResidual argument passed!");

    // System support:
    #if PSYCH_SYSTEM == PSYCH_WINDOWS
        if(PsychPrefStateGet_Verbosity() > 1) printf("PTB-WARNING: Synchronization of graphics display heads requested, but this is not supported on MS-Windows.\n");
        return(PsychError_unimplemented);
    #endif

    // Often not reached...
    return(PsychError_none);
}

/* PsychSetOutputDithering() - Control bit depth control and dithering on digital display output encoder:
 *
 * This function enables or disables bit depths truncation or dithering of digital display output ports of supported
 * graphics hardware. Currently the ATI Radeon X1000/HD2000/HD3000/HD4000/HD5000 and later cards should allow this.
 *
 * This needs support from the Psychtoolbox kernel level support driver for low-level register reads
 * and writes to the GPU registers.
 *
 *
 * 'windowRecord'	Is used to find the Id of the screen for which mode should be changed. If set to NULL then...
 * 'screenId'       ... is used to determine the screenId for the screen. Otherwise 'screenId' is ignored.
 * 'ditherEnable'   Zero = Disable any dithering. Non-Zero Reenable dithering after it has been disabled by us,
 *                  or if it wasn't disabled beforehand, enable it with a control mode as specified by the numeric
 *                  value of 'ditherEnable'. The value is GPU specific.
 *
 */
psych_bool PsychSetOutputDithering(PsychWindowRecordType* windowRecord, int screenId, unsigned int ditherEnable)
{
    // This cool stuff not supported on the uncool Windows OS:
    if(PsychPrefStateGet_Verbosity() > 1) printf("PTB-WARNING: GPU dithering control requested, but this is not supported on MS-Windows.\n");
    return(FALSE);
}

/* PsychSetGPUIdentityPassthrough() - Control identity passthrough of framebuffer 8 bpc pixel values to encoders/connectors:
 *
 * This function enables or disables bit depths truncation or dithering of digital display output ports of supported
 * graphics hardware, and optionally loads a identity LUT into the hardware and configures other parts of the GPU's
 * color management for untampered passthrough of framebuffer pixels.
 * Currently the ATI Radeon X1000/HD2000/HD3000/HD4000/HD5000/HD6000 and later cards should allow this.
 *
 * This needs support from the Psychtoolbox kernel level support driver for low-level register reads
 * and writes to the GPU registers.
 *
 *
 * 'windowRecord'	Is used to find the Id of the screen for which mode should be changed. If set to NULL then...
 * 'screenId'       ... is used to determine the screenId for the screen. Otherwise 'screenId' is ignored.
 * 'passthroughEnable' Zero = Disable passthrough: Currently only reenables dithering, otherwise a no-op.
 *                     1 = Enable passthrough, if possible.
 * 'changeDithering' FALSE = Don't touch dither control, TRUE = Control dithering enable/disable if possible.
 *
 * Returns:
 *
 * 0xffffffff if feature unsupported by given OS/Driver/GPU combo.
 * 0 = On failure to establish passthrough.
 * 1 = On partial success: Dithering disabled and identityt LUT loaded, but other GPU color transformation
 *                         features may not be configured optimally for passthrough.
 * 2 = On full success, as far as can be determined by software.
 *
 */
unsigned int PsychSetGPUIdentityPassthrough(PsychWindowRecordType* windowRecord, int screenId, psych_bool passthroughEnable, psych_bool changeDithering)
{
    // This cool stuff not supported on the uncool Windows OS:
    if(PsychPrefStateGet_Verbosity() > 4) printf("PTB-INFO: GPU framebuffer passthrough setup requested, but this is not supported on MS-Windows.\n");
    return(0xffffffff);
}

/* PsychEnableNative10BitFramebuffer()  - Enable/Disable native >= 10 bpc RGB framebuffer modes.
 *
 * This function enables or disables the native high bit depth framebuffer readout modes of supported
 * graphics hardware. Currently the ATI Radeon X1000, HD2000 and later cards do allow this.
 *
 * This needs support from the Psychtoolbox kernel level support driver for low-level register reads
 * and writes to the GPU registers.
 *
 * 'windowRecord'   Is used to find the Id of the screen for which mode should be changed, as well as enable
 *                  flags to see if a change is required at all, and the OpenGL context for some specific
 *                  fixups. A value of NULL will try to apply the operation to all heads, but may only work
 *                  for *disabling* 10 bpc mode, not for enabling it -- Mostly useful for a master reset to
 *                  system default, e.g., as part of error handling or Screen shutdown handling.
 * 'enable'   True = Enable high bit depth support, False = Disable high bit depth support, reenable ARGB8888 support.
 *
 */
psych_bool PsychEnableNative10BitFramebuffer(PsychWindowRecordType* windowRecord, psych_bool enable)
{
    // This cool stuff not supported on the uncool Windows OS:
    return(FALSE);
}

/* PsychFixupNative10BitFramebufferEnableAfterEndOfSceneMarker()
 *
 * Undo changes made by the graphics driver to the framebuffer pixel format control register
 * as part of an OpenGL/Graphics op that marks "End of Scene", e.g., a glClear() command, that
 * would revert the framebuffers opmode to standard 8bpc mode and thereby kill our >= 10 bpc mode
 * setting.
 *
 * This routine *must* be called after each such problematic "End of scene" marker command like
 * glClear(). The routine does nothing if 10bpc mode is not enabled/requested for the corresponding
 * display head associated with the given onscreen window. It rewrites the control register on
 * >= 10 bpc configured windows to basically undo the unwanted change of the gfx-driver *before*
 * a vertical retrace cycle starts, ie., before that changes take effect (The register is double-
 * buffered and latched to update only at VSYNC time, so we just have to be quick enough).
 *
 *
 * Expected Sequence of operations is:
 * 1. Some EOS command like glClear() issued.
 * 2. EOS command schedules ctrl register update to "bad" value at next VSYNC.
 * 3. This routine gets called, detects need for fixup, glGetError() waits for "2." to finish.
 * 4. This routine undos the "bad" value change request by overwriting the latch with our
 *    "good" value --> Scheduled for next VSYNC. Then it returns...
 * 5. At next VSYNC or old "good" value is overwritten/latched with our new old "good" value,
 *    --> "good value" persists, framebuffer stays in high bpc configuration --> All good.
 *
 * So far the theory, let's see if this really works in real world...
 *
 * This is not needed in Carbon+AGL windowed mode, as the driver doesnt' mess with the control
 * register there, but that mode has its own share of drawback, e.g., generally reduced performance
 * and less robust stimulus onset timing and timestamping... Life's full of tradeoffs...
 */
void PsychFixupNative10BitFramebufferEnableAfterEndOfSceneMarker(PsychWindowRecordType* windowRecord)
{
    return;
}

/* PsychGetCurrentGPUSurfaceAddresses() - Get current scanout surface addresses
 *
 * Tries to get current addresses of primary and secondary scanout buffers and
 * the pending status of pending pageflips if any.
 *
 * primarySurface - Pointer to 64-Bit target for frontBuffers address.
 * secondarySurface - Pointer to a potential secondary buffer, e.g., for frame-sequential stereo.
 * updatePending - TRUE if a flip has been queued and is still pending. FALSE otherwise or "don't know"
 *
 * Returns TRUE on success, FALSE if given GPU isn't supported for such queries.
 *
 */
psych_bool PsychGetCurrentGPUSurfaceAddresses(PsychWindowRecordType* windowRecord, psych_uint64* primarySurface, psych_uint64* secondarySurface, psych_bool* updatePending)
{

    // Not supported:
    return(FALSE);
}

/* Stores content of GPU's surface address registers of the surfaces that
 * correspond to the windowRecords frontBuffers. Only called inside
 * PsychExecuteBufferSwapPrefix() immediately before triggering a double-buffer
 * swap. The values are used as reference values: If another readout of
 * these registers shows values different from the ones stored preflip,
 * then that is a certain indicator that bufferswap via pageflip has happened
 * or will happen.
 */
void PsychStoreGPUSurfaceAddresses(PsychWindowRecordType* windowRecord)
{
    psych_bool updatePending;
    PsychGetCurrentGPUSurfaceAddresses(windowRecord, &(windowRecord->gpu_preflip_Surfaces[0]), &(windowRecord->gpu_preflip_Surfaces[1]), &updatePending);
    return;
}

/* PsychIsGPUPageflipUsed() - Is a pageflip used on the GPU for buffer swaps at this moment?
 *
 * This routine compares preflip scanout addresses, as gathered via a previous PsychStoreGPUSurfaceAddresses()
 * call prior to scheduling a swap, with the current addresses and update status. It should only be called
 * after we detected bufferswap completion to check if the swap happened via pageflip and therefore our
 * completion detection and timestamping is trustworthy, or if the swap happened by some other means like
 * compositor or copyswap and therefore our results wrt. completion or timestamping are not trustworthy -
 * at least not for conventional timestamping as used on OSX, or Linux without special OS support.
 *
 * The interesting scenario is if - after detection of swap completion by our conventional standard method
 * for use with proprietary graphics drivers - the surface scanout addresses have changed and the flip is
 * confirmed finished. In this case we can be somewhat certain that we triggered the pageflip and it completed,
 * ie. the results for timestamping are trustworthy. This is indicated by return value 2. If a flip is used
 * but still pending (value 1) although our code assumes swap has completed then a pageflip was likely queued
 * by the desktop compositor and is still pending -> Timestamping not trustworthy. A value of 0 could indicate
 * copyswap or a compositor to which we sent our updated composition source surface and posted damage, but which
 * hasn't yet picked up on it or at least hasn't performed the full composition pass + queueing a pageflip.
 *
 * Ergo: For checking the trustworthiness of swap completion timestamping, the only "good" result is a
 * return value of 2, a value of 0 or 1 is considered bad for timing, a value of -1 is non-diagnostic.
 *
 * As of beginning of March 2015, this routine can only be used with some confidence on Linux for conventional
 * timestamping with the proprietary drivers and X11, as we know how X11 compositors work on Linux and what
 * to expect. Use with FOSS graphics stack or on Wayland is not needed as we have much better facilities there.
 * Additionally the PsychGetCurrentGPUSurfaceAddresses() support code is limited to AMD gpu's, so the only
 * interesting/valid use cases are Linux/X11 + proprietary AMD Catalyst driver for some clever handling, and
 * OSX + PsychtoolboxKernelDriver + AMD gpu for purely diagnostic use for manual diagnostic, not automatic
 * problem solving!
 *
 * Return values:
 * -1  == Unknown / Query unsupported.
 *  0  == No.
 *  1  == Yes, and the flip has been queued but is still pending, not finished.
 *  2  == Yes, and the flip is finished.
 *
 */
int PsychIsGPUPageflipUsed(PsychWindowRecordType* windowRecord)
{
    psych_uint64 primarySurface, secondarySurface;
    psych_bool updatePending;

    if (!PsychGetCurrentGPUSurfaceAddresses(windowRecord, &primarySurface, &secondarySurface, &updatePending)) {
        // Query not possible/supported: Return "I don't know" value -1:
        return(-1);
    }

    // Scanout addresses changed since last PsychStoreGPUSurfaceAddresses() call?
    // That would mean a pageflip was either queued or already executed, in any
    // case, pageflip is used for bufferswap:
    if (primarySurface != windowRecord->gpu_preflip_Surfaces[0] || secondarySurface != windowRecord->gpu_preflip_Surfaces[1]) {
        // Pageflip in use. Still pending (queued but not completed) or already completed?
        if (updatePending) return(1);
        return(2);
    }

    // Nope, scanout hasn't changed: Assume copyswap/blit etc.
    return(0);
}

/*  PsychWaitForBufferswapPendingOrFinished()
 *  Waits until a bufferswap for window windowRecord has either already happened or
 *  bufferswap is certain.
 *  Input values:
 *  windowRecord struct of onscreen window to monitor.
 *  timestamp    = Deadline for abortion of flip detection at input.
 *
 *  Return values:
 *  timestamp    = System time at polling loop exit.
 *  beamposition = Beamposition at polling loop exit.
 *
 *  Return value: FALSE if swap happened already, TRUE if swap is imminent.
 */
psych_bool PsychWaitForBufferswapPendingOrFinished(PsychWindowRecordType* windowRecord, double* timestamp, int *beamposition)
{
    // On Windows, we always return "swap happened":
    return(FALSE);
}

/* PsychGetNVidiaGPUType()
 *
 * Decodes hw register of NVidia GPU into GPU core id / chip family:
 * Returns 0 for unknown card, otherwise xx for NV_xx:
 *
 * Reference Linux nouveau-kms driver implementation in:
 * nouveau/core/engine/device/base.c: nouveau_devobj_ctor()
 */
unsigned int PsychGetNVidiaGPUType(PsychWindowRecordType* windowRecord)
{
    return(0);
}

/* PsychScreenToHead() - Map PTB screenId to GPU output headId (aka pipeId):
 *
 * See explanations for PsychScreenToCrtcId() to understand what this is good for!
 *
 * screenId = PTB screen index.
 * rankId = Select which head in a multi-head config. rankId 0 == Primary output.
 * A return value of -1 for a given rankId means that no such output is assigned,
 * it terminates the array.
 */
int PsychScreenToHead(int screenId, int rankId)
{
    return(displayScreensToPipes[screenId][rankId]);
}

/* PsychSetScreenToHead() - Change mapping of a PTB screenId to GPU headId: */
void PsychSetScreenToHead(int screenId, int headId, int rankId)
{
    // Assign new mapping:
    displayScreensToPipes[screenId][rankId] = headId;
}

/* PsychScreenToCrtcId()
 *
 * Map PTB screenId and output head id to the index of the associated low-level
 * crtc scanout engine of the GPU: rankId selects which output head (0 = primary).
 *
 * PsychScreenToHead() returns the os-specific identifier of a specific
 * display output head, e.g., a display connector. On Windows and OS/X this is currently
 * simply a running number: 0 for the first display output, 1 for the second etc. On
 * Linux/X11 this is the X11 RandR extension protocol XID of the crtc associated
 * with a given display output, which allows to use the RandR extension to address
 * specific crtc's and do things like query and set video mode of a crtc (resolution,
 * video refresh rate), viewport of a crtc, rotation, mirroring state and other
 * geometric transforms, backlight and dithering settings etc. A XID of zero, which means
 * "invalid/not assigned" gets mapped to -1 for compatibility reasons in PTB.
 *
 * PsychScreenToCrtcId() returns the operating system independent, but gpu-specific index
 * of the low-level crtc display scanout engine associated with a display output. The
 * naming convention here is purely Psychtoolbox specific, as this index is used for
 * low-level direct access to GPU MMIO control registers via PTB's own magic. Values
 * are -1 for "not assigned/invalid" and then 0, 1, 2, ... for scanout engine zero, one,
 * two, ... These numbers are mapped in a gpu specific way to the addresses and offsets
 * of low-level control registers of the GPU hardware.
 *
 * Unfortunately, operating systems don't provide any well defined means to find out the
 * mapping between PsychScreenToHead() "high-level" output id's and PsychScreenToCrtcId()
 * low-level crtc id's, so the mapping gets determined at Screen() startup time via some more
 * or less clever heuristics which should do the right thing(tm) for common display and gpu
 * setups, but may fail on exotic configs. To cope with those, manual overrides are provided to
 * usercode, so the user can hopefully figure out correct mappings via trial and error.
 */
int PsychScreenToCrtcId(int screenId, int rankId)
{
    return(displayScreensToCrtcIds[screenId][rankId]);
}

void PsychSetScreenToCrtcId(int screenId, int crtcId, int rankId)
{
    // Assign new mapping:
    displayScreensToCrtcIds[screenId][rankId] = crtcId;

    // Mark mappings as user-defined instead of auto-detected/default-setup:
    displayScreensToCrtcIdsUserOverride = TRUE;
}

void PsychResetCrtcIdUserOverride(void)
{
    displayScreensToCrtcIdsUserOverride = FALSE;
}

/* PsychInitScreenToHeadMappings() - Setup initial mapping for 'numDisplays' displays:
 *
 * Called from end of InitCGDisplayIDList() during os-specific display initialization.
 *
 * 1. Starts with an identity mapping screen 0 -> (head 0 / crtcid 0), screen 1 -> (head 1 / crtcid 1) ...
 *
 * 2. Allows override of low-level crtc id mapping of the first output of a screen via
 *    environment variable "PSYCHTOOLBOX_PIPEMAPPINGS".
 *
 *    Format is: One character (a number between "0" and "9") for each screenid,
 *    e.g., "021" would map screenid 0 to crtcid 0, screenid 1 to crtcid 2 and screenid 2 to crtcid 1.
 *
 * 3. This mapping can be overriden via Screen('Preference', 'ScreenToHead') setting.
 *
 */
void PsychInitScreenToHeadMappings(int numDisplays)
{
    int i, j;
    char* ptbpipelines = NULL;
    (void) numDisplays;

    displayScreensToPipesAutoDetected = FALSE;

    // Setup default identity one-to-one mapping:
    for(i = 0; i < kPsychMaxPossibleDisplays; i++) {
        displayScreensToPipes[i][0] = i;
        displayScreensToCrtcIds[i][0] = i;

        for (j = 1; j < kPsychMaxPossibleCrtcs; j++) {
            displayScreensToPipes[i][j] = -1;
            displayScreensToCrtcIds[i][j] = -1;
        }

        // We also setup beamposition bias values to "neutral defaults":
        screenBeampositionBias[i] = 0;
        screenBeampositionVTotal[i] = 0;
    }

    // Did user provide an override for the screenid --> pipeline mapping?
    ptbpipelines = getenv("PSYCHTOOLBOX_PIPEMAPPINGS");
    if (ptbpipelines) {
        // The default is "012...", ie screen 0 = pipe 0, 1 = pipe 1, 2 =pipe 2, n = pipe n
        for (i = 0; (i < (int) strlen(ptbpipelines)) && (i < kPsychMaxPossibleDisplays); i++) {
            PsychSetScreenToCrtcId(i, (((ptbpipelines[i] - 0x30) >=0) && ((ptbpipelines[i] - 0x30) < 10)) ? (ptbpipelines[i] - 0x30) : -1, 0);
        }
    }
}

// Try to auto-detect screen to head mappings if possible and not yet overriden by usercode:
void PsychAutoDetectScreenToHeadMappings(int maxHeads)
{
    return;
}

/* PsychGetBeamposCorrection() -- Get corrective beamposition values.
 * Some GPU's and drivers don't return the true vertical scanout position on
 * query, but a value that is offset by a constant value (for a give display mode).
 * This function returns corrective values to apply to the GPU returned values
 * to get the "true scanoutposition" for timestamping etc.
 *
 * Proper values are setup via PsychSetBeamposCorrection() from high-level startup code
 * if needed. Otherwise they are set to (0,0), so the correction is an effective no-op.
 *
 * truebeampos = measuredbeampos - *vblbias;
 * if (truebeampos < 0) truebeampos = *vbltotal + truebeampos;
 *
 */
void PsychGetBeamposCorrection(int screenId, int *vblbias, int *vbltotal)
{
    *vblbias  = screenBeampositionBias[screenId];
    *vbltotal = screenBeampositionVTotal[screenId];
}

/* PsychSetBeamposCorrection() -- Set corrective beamposition values.
 * Called from high-level setup/calibration code at onscreen window open time.
 */
void PsychSetBeamposCorrection(int screenId, int vblbias, int vbltotal)
{
    int gpuMaintype;
    int crtcid = PsychScreenToCrtcId(screenId, 0);

    // Auto-Detection of correct values requested?
    if (((unsigned int) vblbias == 0xffffffff) && ((unsigned int) vbltotal == 0xffffffff)) {
        // First set'em to neutral safe values in case we fail our auto-detect:
        vblbias  = 0;
        vbltotal = 0;

        // Get model of display gpu, which provides beamposition:
        gpuMaintype = kPsychUnknown;
        PsychGetGPUSpecs(screenId, &gpuMaintype, NULL, NULL, NULL);

        // Can do this on NVidia GPU's >= NV-50 if low-level access (PTB kernel driver or equivalent) is enabled:
        if ((gpuMaintype == kPsychGeForce) && PsychOSIsKernelDriverAvailable(screenId)) {
            // Need to read different regs, depending on GPU generation:
            if ((PsychGetNVidiaGPUType(NULL) >= 0x140) || (PsychGetNVidiaGPUType(NULL) == 0x0)) {
                // Auto-Detection. Read values directly from NV-140 / NV-160 aka "Volta" / "Turing" class and later hardware:

                #if PSYCH_SYSTEM != PSYCH_WINDOWS
                // VBLANKE end line of vertical blank - smaller than VBLANKS. Subtract VBLANKE + 1 to normalize to "scanline zero is start of active scanout":
                vblbias = (int) ((PsychOSKDReadRegister(crtcid, 0x68206c + 0x8000 + (crtcid * 0x400), NULL) >> 16) & 0xFFFF) + 1;

                // DISPLAY_TOTAL: Encodes VTOTAL in high-word, HTOTAL in low-word. Get the VTOTAL in high word:
                vbltotal = (int) ((PsychOSKDReadRegister(crtcid, 0x682064 + 0x8000 + (crtcid * 0x400), NULL) >> 16) & 0xFFFF);

                // Decode VBL_START and VBL_END and VACTIVE for debug purposes:
                if (PsychPrefStateGet_Verbosity() > 5) {
                    unsigned int vbl_start, vbl_end;
                    vbl_start = (int) ((PsychOSKDReadRegister(crtcid, 0x682070 + 0x8000 + (crtcid * 0x400), NULL) >> 16) & 0xFFFF);
                    vbl_end   = (int) ((PsychOSKDReadRegister(crtcid, 0x68206c + 0x8000 + (crtcid * 0x400), NULL) >> 16) & 0xFFFF);
                    printf("PTB-DEBUG: Screen %i [head %i]: vbl_start = %i  vbl_end = %i.\n", screenId, crtcid, (int) vbl_start, (int) vbl_end);
                }
                #endif
            }
            else if (PsychGetNVidiaGPUType(NULL) >= 0x0d0) {
                // Auto-Detection. Read values directly from NV-D0 / E0-"Kepler" class and later hardware:
                //
                #if PSYCH_SYSTEM != PSYCH_WINDOWS
                // VBLANKE end line of vertical blank - smaller than VBLANKS. Subtract VBLANKE + 1 to normalize to "scanline zero is start of active scanout":
                vblbias = (int) ((PsychOSKDReadRegister(crtcid, 0x64041c + (crtcid * 0x300), NULL) >> 16) & 0xFFFF) + 1;

                // DISPLAY_TOTAL: Encodes VTOTAL in high-word, HTOTAL in low-word. Get the VTOTAL in high word:
                vbltotal = (int) ((PsychOSKDReadRegister(crtcid, 0x640414 + (crtcid * 0x300), NULL) >> 16) & 0xFFFF);

                // Decode VBL_START and VBL_END and VACTIVE for debug purposes:
                if (PsychPrefStateGet_Verbosity() > 5) {
                    unsigned int vbl_start, vbl_end;
                    vbl_start = (int) ((PsychOSKDReadRegister(crtcid, 0x640420 + (crtcid * 0x300), NULL) >> 16) & 0xFFFF);
                    vbl_end   = (int) ((PsychOSKDReadRegister(crtcid, 0x64041c + (crtcid * 0x300), NULL) >> 16) & 0xFFFF);
                    printf("PTB-DEBUG: Screen %i [head %i]: vbl_start = %i  vbl_end = %i.\n", screenId, crtcid, (int) vbl_start, (int) vbl_end);
                }
                #endif
            }
            else if (PsychGetNVidiaGPUType(NULL) >= 0x50) {
                // Auto-Detection. Read values directly from NV-50 class and later hardware:
                //
                // SYNC_START_TO_BLANK_END 16 bit high-word in CRTC_VAL block of NV50_PDISPLAY on NV-50 encodes
                // length of interval from vsync start line to vblank end line. This is the corrective offset we
                // need to subtract from read out scanline position to get true scanline position.
                // Hardware registers "scanline position" measures positive distance from vsync start line (== "scanline 0").
                // The low-word likely encodes hsyncstart to hblank end length in pixels, but we're not interested in that,
                // so we shift and mask it out:
                #if PSYCH_SYSTEM != PSYCH_WINDOWS
                vblbias = (int) ((PsychOSKDReadRegister(crtcid, 0x610000 + 0xa00 + 0xe8 + (crtcid * 0x540), NULL) >> 16) & 0xFFFF);

                // DISPLAY_TOTAL: Encodes VTOTAL in high-word, HTOTAL in low-word. Get the VTOTAL in high word:
                vbltotal = (int) ((PsychOSKDReadRegister(crtcid, 0x610000 + 0xa00 + 0xf8 + (crtcid * 0x540), NULL) >> 16) & 0xFFFF);

                // Decode VBL_START and VBL_END and VACTIVE for debug purposes:
                if (PsychPrefStateGet_Verbosity() > 5) {
                    unsigned int vbl_start, vbl_end, vactive;
                    vbl_start = (int) ((PsychOSKDReadRegister(crtcid, 0x610af4 + (crtcid * 0x540), NULL) >> 16) & 0xFFFF);
                    vbl_end   = (int) ((PsychOSKDReadRegister(crtcid, 0x610aec + (crtcid * 0x540), NULL) >> 16) & 0xFFFF);
                    vactive   = (int) ((PsychOSKDReadRegister(crtcid, 0x610afc + (crtcid * 0x540), NULL) >> 16) & 0xFFFF);
                    printf("PTB-DEBUG: Screen %i [head %i]: vbl_start = %i  vbl_end = %i  vactive = %i.\n", screenId, crtcid, (int) vbl_start, (int) vbl_end, (int) vactive);
                }
                #endif
            } else {
                // Auto-Detection. Read values directly from pre-NV-50 class hardware:
                // We only get VTOTAL and assume a bias value of zero, which seems to be
                // the case according to measurements on NV-40 and NV-30 gpu's:
                #if PSYCH_SYSTEM != PSYCH_WINDOWS
                vblbias = 0;

                // FP_TOTAL 0x804 relative to PRAMDAC base 0x680000 with stride 0x2000: Encodes VTOTAL in low-word:
                vbltotal = (int) ((PsychOSKDReadRegister(crtcid, 0x680000 + 0x804 + ((crtcid > 0) ? 0x2000 : 0), NULL)) & 0xFFFF) + 1;
                #endif
            }
        }

        if ((gpuMaintype == kPsychIntelIGP) && PsychOSIsKernelDriverAvailable(screenId)) {
            #if PSYCH_SYSTEM != PSYCH_WINDOWS
            vblbias = 0;

            // VTOTAL at 0x6000C with stride 0x1000: Encodes VTOTAL in upper 16 bit word masked with 0x1fff :
            vbltotal = (int) 1 + ((PsychOSKDReadRegister(crtcid, 0x6000c + (crtcid * 0x1000), NULL) >> 16) & 0x1FFF);

            // Decode VBL_START and VBL_END for debug purposes:
            if (PsychPrefStateGet_Verbosity() > 5) {
                unsigned int vbl_start, vbl_end, vbl;
                vbl = PsychOSKDReadRegister(crtcid, 0x60010 + (crtcid * 0x1000), NULL);
                vbl_start = vbl & 0x1fff;
                vbl_end   = (vbl >> 16) & 0x1FFF;
                printf("PTB-DEBUG: Screen %i [head %i]: vbl_start = %i  vbl_end = %i.\n", screenId, crtcid, (int) vbl_start, (int) vbl_end);
            }
            #endif
        }
    }

    // Feedback is good:
    if (((vblbias != 0) || (vbltotal != 0)) && (PsychPrefStateGet_Verbosity() > 3)) {
        printf("PTB-INFO: Screen %i [head %i]: Applying beamposition corrective offsets: vblbias = %i, vbltotal = %i.\n", screenId, crtcid, vblbias, vbltotal);
    }

    // Assign:
    screenBeampositionBias[screenId] = vblbias;
    screenBeampositionVTotal[screenId] = vbltotal;
}
