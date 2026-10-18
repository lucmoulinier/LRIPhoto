/*		LRIPhoto.c
 *
 * Xavier Garreau , xavier@larochelle-innovation.com, Sep. 7 2005
 *
 * XG: Sep. 9 2005
 *    - added scaleXYTkImg function
 *    - rotation dans le sens horaire plutôt que trigo
 *
 * 2026:
 *    - compatible Tcl/Tk 8.5+ et 9.x (stubs "8.5-", const, Tcl_Size)
 *    - imgtransform et imgcopy partagent l'analyse des options et le
 *      traitement ; erreurs explicites pour les valeurs invalides
 *    - gris / noir et blanc : offset[3] initialisé, diffusion d'erreur
 *      Floyd-Steinberg en flottant dans un tampon local
 *    - les codes retour de Tk_PhotoSetSize / Tk_PhotoPutBlock sont vérifiés
 */

#include <string.h>
#include <tcl.h>
#include <tk.h>

#if !defined(TCL_SIZE_MAX) && !defined(Tcl_Size)
typedef int Tcl_Size;
#endif

/*
 * For C++ compilers, use extern "C"
 */

#ifdef __cplusplus
extern "C" {
#endif

  /*
   * Only the _Init functions are exported. [load lib LRIPhoto] looks for
   * "Lriphoto_Init" in Tcl 8 (prefix converted to title case) but for
   * "LRIPhoto_Init" in Tcl 9 (explicit prefix used as is).
   */
  DLLEXPORT int Lriphoto_Init(Tcl_Interp * interp);
  DLLEXPORT int LRIPhoto_Init(Tcl_Interp * interp);

#ifdef __cplusplus
}
#endif


// Value of channel c (0..2) of pixel (x, y)
#define PIX(blk, x, y, c) \
  ((blk).pixelPtr[(size_t)(blk).pixelSize*(x)+(size_t)(blk).pitch*(y)+(blk).offset[c]])

// Alpha of pixel (x, y), 255 if the block has no alpha channel
static int LRI_alpha(const Tk_PhotoImageBlock *blk, int x, int y) {
  if (blk->offset[3] < 0 || blk->offset[3] >= blk->pixelSize)
    return 255;
  return blk->pixelPtr[(size_t)blk->pixelSize*x+(size_t)blk->pitch*y+blk->offset[3]];
}

// Store a new RGBA (or gray) block into the photo, resizing it
static int LRI_putTkImg(Tcl_Interp *interp, Tk_PhotoHandle destHandle,
			Tk_PhotoImageBlock *blk) {
  int err = Tk_PhotoSetSize(interp, destHandle, blk->width, blk->height);
  if (err == TCL_OK)
    err = Tk_PhotoPutBlock(interp, destHandle, blk, 0, 0, blk->width, blk->height,
			   TK_PHOTO_COMPOSITE_SET);
  ckfree((char *)blk->pixelPtr);
  return err;
}

// Prepare an empty RGBA block of the given size
static void LRI_newRGBABlock(Tk_PhotoImageBlock *blk, int width, int height) {
  blk->width = width;
  blk->height = height;
  blk->pixelSize = 4;
  blk->pitch = 4*width;
  blk->offset[0] = 0;
  blk->offset[1] = 1;
  blk->offset[2] = 2;
  blk->offset[3] = 3;
  blk->pixelPtr = (unsigned char*)ckalloc((size_t)width*height*4);
}

// Rotate a Tk Image by 90, 180 or 270 degrees (clockwise)
static int LRI_rotateTkImg(Tcl_Interp *interp, Tk_PhotoHandle destHandle, int angle) {
  int iX, iY, destX, destY;
  Tk_PhotoImageBlock srcBlockPtr, destBlockPtr;
  unsigned char *d;

  Tk_PhotoGetImage(destHandle, &srcBlockPtr);
  if (srcBlockPtr.width <= 0 || srcBlockPtr.height <= 0) return TCL_OK;

  if (angle == 90 || angle == 270)
    LRI_newRGBABlock(&destBlockPtr, srcBlockPtr.height, srcBlockPtr.width);
  else
    LRI_newRGBABlock(&destBlockPtr, srcBlockPtr.width, srcBlockPtr.height);

  for (iY=0; iY<srcBlockPtr.height; ++iY) {
    for (iX=0; iX<srcBlockPtr.width; ++iX) {
      switch (angle) {
      case 270:
	destX = iY;
	destY = srcBlockPtr.width-1 - iX;
	break;
      case 180:
	destX = srcBlockPtr.width-1 - iX;
	destY = srcBlockPtr.height-1 - iY;
	break;
      case 90:
	destX = srcBlockPtr.height-1 - iY;
	destY = iX;
	break;
      default:
	destX = iX;
	destY = iY;
	break;
      }
      d = destBlockPtr.pixelPtr + (size_t)destBlockPtr.pitch*destY + 4*(size_t)destX;
      d[0] = PIX(srcBlockPtr, iX, iY, 0);
      d[1] = PIX(srcBlockPtr, iX, iY, 1);
      d[2] = PIX(srcBlockPtr, iX, iY, 2);
      d[3] = LRI_alpha(&srcBlockPtr, iX, iY);
    }
  }
  return LRI_putTkImg(interp, destHandle, &destBlockPtr);
}

// Scale a Tk Image
// scalex and scaley are in per cents
// Each destination pixel is the mean of 4 source pixels
static int LRI_scaleXYTkImg(Tcl_Interp *interp, Tk_PhotoHandle destHandle,
			    int scalex, int scaley) {
  int iX, iY, y13, x34, sX, sY, c;
  int A1, A2, A3, A4;
  double scale1x, scale2x;
  double scale1y, scale2y;
  Tk_PhotoImageBlock srcBlockPtr, destBlockPtr;
  unsigned char *d;
  int width, height;

  scale1x = 100.0/scalex;
  scale2x = scale1x/2;
  scale1y = 100.0/scaley;
  scale2y = scale1y/2;

  Tk_PhotoGetImage(destHandle, &srcBlockPtr);
  if (srcBlockPtr.width <= 0 || srcBlockPtr.height <= 0) return TCL_OK;

  // Tk_PhotoSetSize treats 0 as "keep current size": never go below 1
  width = (int)(((Tcl_WideInt)srcBlockPtr.width*scalex)/100);
  height = (int)(((Tcl_WideInt)srcBlockPtr.height*scaley)/100);
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  LRI_newRGBABlock(&destBlockPtr, width, height);

  for (iY=0; iY<destBlockPtr.height; ++iY) {
    sY = (int)(iY*scale1y);
    if (sY>srcBlockPtr.height-1) sY=srcBlockPtr.height-1;
    y13 = (int)(sY+scale2y);
    if (y13>srcBlockPtr.height-1) y13=srcBlockPtr.height-1;
    for (iX=0; iX<destBlockPtr.width; ++iX) {
      sX = (int)(iX*scale1x);
      if (sX>srcBlockPtr.width-1) sX=srcBlockPtr.width-1;
      x34 = (int)(sX+scale2x);
      if (x34>srcBlockPtr.width-1) x34=srcBlockPtr.width-1;
      d = destBlockPtr.pixelPtr + (size_t)destBlockPtr.pitch*iY + 4*(size_t)iX;
      for (c=0; c<3; ++c) {
	d[c] = (PIX(srcBlockPtr, sX, y13, c) + PIX(srcBlockPtr, sX, sY, c)
		+ PIX(srcBlockPtr, x34, y13, c) + PIX(srcBlockPtr, x34, sY, c))/4;
      }
      A1 = LRI_alpha(&srcBlockPtr, sX, y13);
      A2 = LRI_alpha(&srcBlockPtr, sX, sY);
      A3 = LRI_alpha(&srcBlockPtr, x34, y13);
      A4 = LRI_alpha(&srcBlockPtr, x34, sY);
      d[3] = (A1+A2+A3+A4)/4;
    }
  }
  return LRI_putTkImg(interp, destHandle, &destBlockPtr);
}

// Gray level of pixel (x, y), composed over a white background
static int LRI_grayValue(const Tk_PhotoImageBlock *blk, int x, int y) {
  int R = PIX(*blk, x, y, 0);
  int V = PIX(*blk, x, y, 1);
  int B = PIX(*blk, x, y, 2);
  int A = LRI_alpha(blk, x, y);
  return 255-(A*(255-(R+V+B)/3))/255;
}

// Prepare an empty 1 byte per pixel (gray, no alpha) block
static void LRI_newGrayBlock(Tk_PhotoImageBlock *blk, int width, int height) {
  blk->width = width;
  blk->height = height;
  blk->pixelSize = 1;
  blk->pitch = width;
  blk->offset[0] = 0;
  blk->offset[1] = 0;
  blk->offset[2] = 0;
  blk->offset[3] = -1;	// no alpha channel
  blk->pixelPtr = (unsigned char*)ckalloc((size_t)width*height);
}

// Grays a Tk Image
static int LRI_grayTkImg(Tcl_Interp *interp, Tk_PhotoHandle destHandle) {
  int iX, iY;
  Tk_PhotoImageBlock srcBlockPtr, destBlockPtr;

  Tk_PhotoGetImage(destHandle, &srcBlockPtr);
  if (srcBlockPtr.width <= 0 || srcBlockPtr.height <= 0) return TCL_OK;

  LRI_newGrayBlock(&destBlockPtr, srcBlockPtr.width, srcBlockPtr.height);
  for (iY=0; iY<srcBlockPtr.height; ++iY) {
    for (iX=0; iX<srcBlockPtr.width; ++iX) {
      destBlockPtr.pixelPtr[(size_t)destBlockPtr.pitch*iY+iX] =
	LRI_grayValue(&srcBlockPtr, iX, iY);
    }
  }
  return LRI_putTkImg(interp, destHandle, &destBlockPtr);
}

// Convert a Tk Image to black and white
// Uses a Floyd Steinberg dithering method
static int LRI_BWTkImg(Tcl_Interp *interp, Tk_PhotoHandle destHandle) {
  int iX, iY, width, height;
  float gray, err, *buf, *p;
  Tk_PhotoImageBlock srcBlockPtr, destBlockPtr;

  Tk_PhotoGetImage(destHandle, &srcBlockPtr);
  width = srcBlockPtr.width;
  height = srcBlockPtr.height;
  if (width <= 0 || height <= 0) return TCL_OK;

  // Gray levels, with room for propagated errors
  buf = (float*)ckalloc((size_t)width*height*sizeof(float));
  for (iY=0; iY<height; ++iY)
    for (iX=0; iX<width; ++iX)
      buf[(size_t)width*iY+iX] = (float)LRI_grayValue(&srcBlockPtr, iX, iY);

  LRI_newGrayBlock(&destBlockPtr, width, height);
  for (iY=0; iY<height; ++iY) {
    for (iX=0; iX<width; ++iX) {
      p = buf + (size_t)width*iY + iX;
      gray = *p;
      if (gray > 127.0f) {
	destBlockPtr.pixelPtr[(size_t)width*iY+iX] = 255;
	err = (gray-255.0f)/16.0f;
      } else {
	destBlockPtr.pixelPtr[(size_t)width*iY+iX] = 0;
	err = gray/16.0f;
      }

      // Floyd Steinberg error propagation
      // Would give better results by changing direction after each row
      if (iX+1<width) p[1] += 7*err;
      if (iY+1<height) {
	if (iX>0) p[width-1] += 3*err;
	p[width] += 5*err;
	if (iX+1<width) p[width+1] += err;
      }
    }
  }
  ckfree((char *)buf);
  return LRI_putTkImg(interp, destHandle, &destBlockPtr);
}


// Options shared by LRI::imgtransform and LRI::imgcopy
typedef struct {
  int scale, scalex, scaley, angle, bw, gray;
} LRI_Options;

#define LRI_OPTIONS_USAGE "?-scale %? ?-scalex %? ?-scaley %? ?-rotate angle? ?-gray bool? ?-bw bool?"

static int LRI_parseOptions(Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const *objv, LRI_Options *opt) {
  static const char *const optNames[] = {
    "-scale", "-scalex", "-scaley", "-rotate", "-gray", "-bw", NULL
  };
  enum { OPT_SCALE, OPT_SCALEX, OPT_SCALEY, OPT_ROTATE, OPT_GRAY, OPT_BW };
  Tcl_Size i;
  int index, value;

  opt->scale = opt->scalex = opt->scaley = -1;
  opt->angle = 0;
  opt->bw = opt->gray = 0;

  for (i=0; i<objc; i+=2) {
    if (Tcl_GetIndexFromObj(interp, objv[i], optNames, "option", 0, &index) != TCL_OK)
      return TCL_ERROR;
    if (i+1 >= objc) {
      Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing value for option \"%s\"",
					     optNames[index]));
      return TCL_ERROR;
    }
    switch (index) {
    case OPT_GRAY:
      if (Tcl_GetBooleanFromObj(interp, objv[i+1], &opt->gray) != TCL_OK) return TCL_ERROR;
      break;
    case OPT_BW:
      if (Tcl_GetBooleanFromObj(interp, objv[i+1], &opt->bw) != TCL_OK) return TCL_ERROR;
      break;
    case OPT_ROTATE:
      if (Tcl_GetIntFromObj(interp, objv[i+1], &value) != TCL_OK) return TCL_ERROR;
      if (value % 90 != 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "bad angle \"%d\": must be a multiple of 90", value));
	return TCL_ERROR;
      }
      opt->angle = ((value % 360) + 360) % 360;
      break;
    default:
      if (Tcl_GetIntFromObj(interp, objv[i+1], &value) != TCL_OK) return TCL_ERROR;
      if (value <= 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "bad value \"%d\" for option \"%s\": must be a positive percentage",
	    value, optNames[index]));
	return TCL_ERROR;
      }
      if (index == OPT_SCALE) opt->scale = value;
      else if (index == OPT_SCALEX) opt->scalex = value;
      else opt->scaley = value;
      break;
    }
  }
  if (opt->scalex>0 && opt->scaley==-1) opt->scaley=100;
  if (opt->scaley>0 && opt->scalex==-1) opt->scalex=100;
  return TCL_OK;
}

// Apply the transformations in place.
// Down-scaling is done before the rotation and up-scaling after it, so the
// rotation always works on the smaller image.
static int LRI_transform(Tcl_Interp *interp, Tk_PhotoHandle img, const LRI_Options *opt) {
  if (opt->scalex>0) {
    if (opt->scalex!=100 || opt->scaley!=100) {
      if (TCL_OK != LRI_scaleXYTkImg(interp, img, opt->scalex, opt->scaley)) return TCL_ERROR;
    }
  } else if (opt->scale>0 && opt->scale<100) {
    if (TCL_OK != LRI_scaleXYTkImg(interp, img, opt->scale, opt->scale)) return TCL_ERROR;
  }
  if (opt->angle != 0) {
    if (TCL_OK != LRI_rotateTkImg(interp, img, opt->angle)) return TCL_ERROR;
  }
  if (opt->scalex<=0 && opt->scale>100) {
    if (TCL_OK != LRI_scaleXYTkImg(interp, img, opt->scale, opt->scale)) return TCL_ERROR;
  }
  if (opt->bw) {
    if (TCL_OK != LRI_BWTkImg(interp, img)) return TCL_ERROR;
  } else if (opt->gray) {
    if (TCL_OK != LRI_grayTkImg(interp, img)) return TCL_ERROR;
  }
  return TCL_OK;
}

static Tk_PhotoHandle LRI_findPhoto(Tcl_Interp *interp, Tcl_Obj *name) {
  Tk_PhotoHandle h = Tk_FindPhoto(interp, Tcl_GetString(name));
  if (!h) {
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	"image \"%s\" doesn't exist or is not a photo image", Tcl_GetString(name)));
  }
  return h;
}

// LRI::imgtransform image ?options?
static int imgtransformObjCmd(ClientData dummy,
			      Tcl_Interp *interp,
			      int objc,
			      Tcl_Obj *const *objv)
{
  Tk_PhotoHandle destimg;
  LRI_Options opt;

  if (objc < 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "image " LRI_OPTIONS_USAGE);
    return TCL_ERROR;
  }
  destimg = LRI_findPhoto(interp, objv[1]);
  if (!destimg) return TCL_ERROR;
  if (LRI_parseOptions(interp, objc-2, objv+2, &opt) != TCL_OK) return TCL_ERROR;

  return LRI_transform(interp, destimg, &opt);
}

// LRI::imgcopy src_image dest_image ?options?
static int imgcopyObjCmd(ClientData dummy,
			 Tcl_Interp *interp,
			 int objc,
			 Tcl_Obj *const *objv)
{
  Tk_PhotoHandle destimg, srcimg;
  Tk_PhotoImageBlock imgblock;
  LRI_Options opt;

  if (objc < 3) {
    Tcl_WrongNumArgs(interp, 1, objv, "src_image dest_image " LRI_OPTIONS_USAGE);
    return TCL_ERROR;
  }
  srcimg = LRI_findPhoto(interp, objv[1]);
  if (!srcimg) return TCL_ERROR;
  destimg = LRI_findPhoto(interp, objv[2]);
  if (!destimg) return TCL_ERROR;
  if (LRI_parseOptions(interp, objc-3, objv+3, &opt) != TCL_OK) return TCL_ERROR;

  /* Photo copy */
  if (srcimg != destimg) {
    Tk_PhotoGetImage(srcimg, &imgblock);
    if (Tk_PhotoSetSize(interp, destimg, imgblock.width, imgblock.height) != TCL_OK)
      return TCL_ERROR;
    if (imgblock.width > 0 && imgblock.height > 0
	&& Tk_PhotoPutBlock(interp, destimg, &imgblock, 0, 0, imgblock.width,
			    imgblock.height, TK_PHOTO_COMPOSITE_SET) != TCL_OK)
      return TCL_ERROR;
  }

  return LRI_transform(interp, destimg, &opt);
}


DLLEXPORT int Lriphoto_Init(Tcl_Interp *interp)
{
  /*
   * This requires Tcl/Tk version 8.5 or greater (including 9.x)
   */
  if (Tcl_InitStubs(interp, "8.5-", 0) == NULL) {
    return TCL_ERROR;
  }
  if (Tk_InitStubs(interp, "8.5-", 0) == NULL) {
    return TCL_ERROR;
  }

  Tcl_CreateObjCommand(interp, "LRI::imgtransform", imgtransformObjCmd, NULL, NULL);
  Tcl_CreateObjCommand(interp, "LRI::imgcopy", imgcopyObjCmd, NULL, NULL);

  return Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION);
}

DLLEXPORT int LRIPhoto_Init(Tcl_Interp *interp)
{
  return Lriphoto_Init(interp);
}
