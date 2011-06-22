/* libvisio
 * Copyright (C) 2011 Fridrich Strba <fridrich.strba@bluewin.ch>
 * Copyright (C) 2011 Eilidh McAdam <tibbylickle@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02111-1301 USA
 */

#include <libwpd-stream/libwpd-stream.h>
#include <locale.h>
#include <sstream>
#include <string>
#include <cmath>
#include <set>
#include "libvisio_utils.h"
#include "VSD11Parser.h"
#include "VSDInternalStream.h"

#define DUMP_BITMAP 0

#if DUMP_BITMAP
static unsigned bitmapId = 0;
#include <sstream>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const ::WPXString libvisio::VSD11Parser::getColourString(const struct Colour &c) const
{
    ::WPXString sColour;
    sColour.sprintf("#%.2x%.2x%.2x", c.r, c.g, c.b);
    return sColour;
}

const struct libvisio::VSD11Parser::StreamHandler libvisio::VSD11Parser::streamHandlers[] =
{
  {0xa, "Name", 0},
  {0xb, "Name Idx", 0},
  {0x14, "Trailer, 0"},
  {0x15, "Page", &libvisio::VSD11Parser::handlePage},
  {0x16, "Colors", &libvisio::VSD11Parser::handleColours},
  {0x17, "??? seems to have no data", 0},
  {0x18, "FontFaces (ver.11)", 0},
  {0x1a, "Styles", 0},
  {0x1b, "??? saw 'Acrobat PDFWriter' string here", 0},
  {0x1c, "??? saw 'winspool.Acrobat PDFWriter.LPT1' string here", 0},
  {0x1d, "Stencils"},
  {0x1e, "Stencil Page (collection of Shapes, one collection per each stencil item)", 0},
  {0x20, "??? seems to have no data", 0},
  {0x21, "??? seems to have no data", 0},
  {0x23, "Icon (for stencil only?)", 0},
  {0x24, "??? seems to have no data", 0},
  {0x26, "??? some fractions, maybe PrintProps", 0},
  {0x27, "Pages", &libvisio::VSD11Parser::handlePages},
  {0x29, "Collection of Type 2a streams", 0},
  {0x2a, "???", 0},
  {0x31, "Document", 0},
  {0x32, "NameList", 0},
  {0x33, "Name", 0},
  {0x3d, "??? found in VSS, seems to have no data", 0},
  {0x3f, "List of name indexes collections", 0},
  {0x40, "Name Idx + ?? group (contains 0xb type streams)", 0},
  {0x44, "??? Collection of Type 0x45 streams", 0},
  {0x45, "??? match number of Pages or Stencil Pages in Pages/Stencils", 0},
  {0xc9, "some collection of 13 bytes structures related to 3f/40 streams", 0},
  {0xd7, "FontFace (ver.11)", 0},
  {0xd8, "FontFaces (ver.6)", 0},
  {0, 0, 0}
};

const struct libvisio::VSD11Parser::ChunkHandler libvisio::VSD11Parser::chunkHandlers[] =
{
  {0x47, "ShapeType=\"Group\"", &libvisio::VSD11Parser::groupChunk},
  {0x48, "ShapeType=\"Shape\"", &libvisio::VSD11Parser::shapeChunk},
  {0x4e, "ShapeType=\"Foreign\"", &libvisio::VSD11Parser::foreignChunk},
  {0, 0, 0}
};

libvisio::VSD11Parser::VSD11Parser(WPXInputStream *input)
  : VSDXParser(input)
{}

libvisio::VSD11Parser::~VSD11Parser()
{}

/** Parses VSD 2003 input stream content, making callbacks to functions provided
by WPGPaintInterface class implementation as needed.
\param iface A WPGPaintInterface implementation
\return A value indicating whether parsing was successful
*/
bool libvisio::VSD11Parser::parse(libwpg::WPGPaintInterface *painter)
{
  const unsigned int SHIFT = 4;
  if (!m_input)
  {
    return false;
  }
  // Seek to trailer stream pointer
  m_input->seek(0x24, WPX_SEEK_SET);

  m_input->seek(8, WPX_SEEK_CUR);
  unsigned int offset = readU32(m_input);
  unsigned int length = readU32(m_input);
  unsigned short format = readU16(m_input);
  bool compressed = ((format & 2) == 2);

  m_input->seek(offset, WPX_SEEK_SET);
  VSDInternalStream trailerStream(m_input, length, compressed);

  // Parse out pointers to other streams from trailer
  trailerStream.seek(SHIFT, WPX_SEEK_SET);
  offset = readU32(&trailerStream);
  trailerStream.seek(offset+SHIFT, WPX_SEEK_SET);
  unsigned int pointerCount = readU32(&trailerStream);
  trailerStream.seek(SHIFT, WPX_SEEK_CUR);

  unsigned int ptrType;
  unsigned int ptrOffset;
  unsigned int ptrLength;
  unsigned int ptrFormat;
  for (unsigned int i = 0; i < pointerCount; i++)
  {
    ptrType = readU32(&trailerStream);
    trailerStream.seek(4, WPX_SEEK_CUR); // Skip dword
    ptrOffset = readU32(&trailerStream);
    ptrLength = readU32(&trailerStream);
    ptrFormat = readU16(&trailerStream);

    int index = -1;
    for (int i = 0; (index < 0) && streamHandlers[i].type; i++)
    {
      if (streamHandlers[i].type == ptrType)
        index = i;
    }

    if (index < 0)
    {
      VSD_DEBUG_MSG(("Unknown stream pointer type 0x%02x found in trailer at %li\n",
                     ptrType, trailerStream.tell() - 18));
    }
    else
    {
      StreamMethod streamHandler = streamHandlers[index].handler;
      if (!streamHandler)
        VSD_DEBUG_MSG(("Stream '%s', type 0x%02x, format 0x%02x at %li ignored\n",
                       streamHandlers[index].name, streamHandlers[index].type, ptrFormat,
                       trailerStream.tell() - 18));
      else
      {
        VSD_DEBUG_MSG(("Stream '%s', type 0x%02x, format 0x%02x at %li handled\n",
                       streamHandlers[index].name, streamHandlers[index].type, ptrFormat,
                       trailerStream.tell() - 18));

        compressed = ((ptrFormat & 2) == 2);
        m_input->seek(ptrOffset, WPX_SEEK_SET);
        VSDInternalStream stream(m_input, ptrLength, compressed);
        (this->*streamHandler)(stream, painter);
      }
    }
  }

  // End page if one is started
  if (m_isPageStarted)
    painter->endGraphics();
  return true;
}

void libvisio::VSD11Parser::handlePages(VSDInternalStream &stream, libwpg::WPGPaintInterface *painter)
{
  unsigned int offset = readU32(&stream);
  stream.seek(offset, WPX_SEEK_SET);
  unsigned int pointerCount = readU32(&stream);
  stream.seek(4, WPX_SEEK_CUR); // Ignore 0x0 dword

  unsigned int ptrType;
  unsigned int ptrOffset;
  unsigned int ptrLength;
  unsigned int ptrFormat;
  for (unsigned int i = 0; i < pointerCount; i++)
  {
    ptrType = readU32(&stream);
    stream.seek(4, WPX_SEEK_CUR); // Skip dword
    ptrOffset = readU32(&stream);
    ptrLength = readU32(&stream);
    ptrFormat = readU16(&stream);

    int index = -1;
    for (int i = 0; (index < 0) && streamHandlers[i].type; i++)
    {
      if (streamHandlers[i].type == ptrType)
        index = i;
    }

    if (index < 0)
    {
      VSD_DEBUG_MSG(("Unknown stream pointer type 0x%02x found in pages at %li\n",
                     ptrType, stream.tell() - 18));
    }
    else
    {
      StreamMethod streamHandler = streamHandlers[index].handler;
      if (!streamHandler)
        VSD_DEBUG_MSG(("Stream '%s', type 0x%02x, format 0x%02x at %li ignored\n",
                       streamHandlers[index].name, streamHandlers[index].type, ptrFormat,
                       stream.tell() - 18));
      else
      {
        VSD_DEBUG_MSG(("Stream '%s', type 0x%02x, format 0x%02x at %li handled\n",
                       streamHandlers[index].name, streamHandlers[index].type, ptrFormat,
                       stream.tell() - 18));

        bool compressed = ((ptrFormat & 2) == 2);
        m_input->seek(ptrOffset, WPX_SEEK_SET);
        VSDInternalStream stream(m_input, ptrLength, compressed);
        (this->*streamHandler)(stream, painter);
      }
    }
  }
}

void libvisio::VSD11Parser::handlePage(VSDInternalStream &stream, libwpg::WPGPaintInterface *painter)
{
  ChunkHeader header = {0};
  m_groupXForms.clear();

  while (!stream.atEOS())
  {
    getChunkHeader(stream, header);
    int index = -1;
    for (int i = 0; (index < 0) && chunkHandlers[i].type; i++)
    {
      if (chunkHandlers[i].type == header.chunkType)
        index = i;
    }

    if (index >= 0)
    {
      // Skip rest of this chunk
      stream.seek(header.dataLength + header.trailer, WPX_SEEK_CUR);
      ChunkMethod chunkHandler = chunkHandlers[index].handler;
      if (chunkHandler)
      {
        m_currentShapeId = header.id;
        (this->*chunkHandler)(stream, painter);
      }
      continue;
    }

    VSD_DEBUG_MSG(("Parsing chunk type %02x with trailer (%d) and length %x\n",
                   header.chunkType, header.trailer, header.dataLength));

    if (header.chunkType == 0x92) // Page properties
    {
      // Skip bytes representing unit to *display* (value is always inches)
      stream.seek(1, WPX_SEEK_CUR);
      m_pageWidth = readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      m_pageHeight = readDouble(&stream);
      stream.seek(19, WPX_SEEK_CUR);
      /* m_scale = */ readDouble(&stream);

      WPXPropertyList pageProps;
      pageProps.insert("svg:width", m_scale*m_pageWidth);
      pageProps.insert("svg:height", m_scale*m_pageHeight);

      if (m_isPageStarted)
        painter->endGraphics();
      painter->startGraphics(pageProps);
      m_isPageStarted = true;

      stream.seek(header.dataLength+header.trailer-45, WPX_SEEK_CUR);
    }
    else // Skip chunk
    {
      header.dataLength += header.trailer;
      VSD_DEBUG_MSG(("Skipping chunk by %x (%d) bytes\n",
                     header.dataLength, header.dataLength));
      stream.seek(header.dataLength, WPX_SEEK_CUR);
    }
  }
}

void libvisio::VSD11Parser::handleColours(VSDInternalStream &stream, libwpg::WPGPaintInterface *painter)
{
  stream.seek(6, WPX_SEEK_SET);
  unsigned int numColours = readU8(&stream);
  Colour tmpColour = {0};

  stream.seek(1, WPX_SEEK_CUR);

  for (unsigned int i = 0; i < numColours; i++)
  {
    tmpColour.r = readU8(&stream);
    tmpColour.g = readU8(&stream);
    tmpColour.b = readU8(&stream);
    tmpColour.a = readU8(&stream);

    m_colours.push_back(tmpColour);
  }
}

void libvisio::VSD11Parser::groupChunk(VSDInternalStream &stream, libwpg::WPGPaintInterface *painter)
{
  WPXPropertyListVector path;
  WPXPropertyList styleProps;
  WPXPropertyListVector gradientProps;
  XForm xform = {0}; // Shape xform data
  std::set<unsigned int> shapeIDs;
  ChunkHeader header = {0};
  double x = 0; double y = 0;
  bool done = false;
  int geomCount = -1;
  unsigned long streamPos;

  // Reset style
  styleProps.clear();
  styleProps.insert("svg:stroke-width", m_scale*0.0138889);
  styleProps.insert("svg:stroke-color", "black");
  styleProps.insert("draw:fill", "none");

  while (!done && !stream.atEOS())
  {
    getChunkHeader(stream, header);
    streamPos = stream.tell();
    VSD_DEBUG_MSG(("Shape: parsing chunk type %x\n", header.chunkType));

    // Break once a chunk that is not nested in the group is found
    if (header.level < 2)
    {
      stream.seek(-19, WPX_SEEK_CUR);
      break;
    }

    switch (header.chunkType)
    {
    case 0x9b: // XForm data
      xform = _transformXForm(_parseXForm(&stream));
      break;
    case 0x83: // ShapeID
      m_groupXForms[readU32(&stream)] = xform;
      break;
    case 0x85: // Line properties
    {
      stream.seek(1, WPX_SEEK_CUR);
      styleProps.insert("svg:stroke-width", m_scale*readDouble(&stream));
      stream.seek(1, WPX_SEEK_CUR);
      Colour c = {0};
      c.r = readU8(&stream);
      c.g = readU8(&stream);
      c.b = readU8(&stream);
      c.a = readU8(&stream);
      styleProps.insert("svg:stroke-color", getColourString(c));
      unsigned int linePattern = readU8(&stream);
      const char* patterns[] = {
        /*  0 */  "solid",
        /*  1 */  "solid",
        /*  2 */  "6, 3",
        /*  3 */  "1, 3",
        /*  4 */  "6, 3, 1, 3",
        /*  5 */  "6, 3, 1, 3, 1, 3",
        /*  6 */  "6, 3, 6, 3, 1, 3",
        /*  7 */  "14, 2, 6, 2",
        /*  8 */  "14, 2, 6, 2, 6, 2",
        /*  9 */  "3, 1",
        /* 10 */  "1, 1",
        /* 11 */  "3, 1, 1, 1",
        /* 12 */  "3, 1, 1, 1, 1, 1",
        /* 13 */  "3, 1, 3, 1, 1, 1",
        /* 14 */  "7, 1, 3, 1",
        /* 15 */  "7, 1, 3, 1, 3, 1",
        /* 16 */  "11, 5",
        /* 17 */  "1, 5",
        /* 18 */  "11, 5, 1, 5",
        /* 19 */  "11, 5, 1, 5, 1, 5",
        /* 20 */  "11, 5, 11, 5, 1, 5",
        /* 21 */  "27, 5, 11, 5",
        /* 22 */  "27, 5, 11, 5, 11, 5",
        /* 23 */  "2, 1"
      };
      if (linePattern > 1 && linePattern < sizeof(patterns)/sizeof(patterns[0]))
        styleProps.insert("svg:stroke-dasharray", patterns[linePattern]);
      else
        styleProps.insert("svg:stroke-dasharray", "solid");
    }
      break;
    case 0x86: // Fill & Shadow properties
    {
      unsigned int colourIndexFG = readU8(&stream);
      stream.seek(9, WPX_SEEK_CUR);
      unsigned int fillPattern = readU8(&stream);
      if (fillPattern == 1)
      {
        styleProps.insert("draw:fill", "solid");
        styleProps.insert("draw:fill-color", getColourString(m_colours[colourIndexFG]));
      }
    }
      break;
    case 0x6c: // GeomList
    {
      _flushCurrentPath(painter);
      painter->setStyle(styleProps, gradientProps);
      uint32_t subHeaderLength = readU32(&stream);
      uint32_t childrenListLength = readU32(&stream);
      stream.seek(subHeaderLength, WPX_SEEK_CUR);
      for (unsigned i = 0; i < (childrenListLength / sizeof(uint32_t)); i++)
        m_currentGeometryOrder.push_back(readU32(&stream));
      geomCount = header.list;
      continue; // Avoid geomCount decrement below
    }
    case 0x8a: // MoveTo
    {
      WPXPropertyList end;
      stream.seek(1, WPX_SEEK_CUR);
      x = readDouble(&stream) + xform.x;
      stream.seek(1, WPX_SEEK_CUR);
      y = (xform.height - readDouble(&stream)) + xform.y;
      rotatePoint(x, y, xform);
      flipPoint(x, y, xform);

      end.insert("svg:x", m_scale*x);
      end.insert("svg:y", m_scale*y);
      end.insert("libwpg:path-action", "M");
      m_currentGeometry[header.id] = end;
    }
      break;
    case 0x8b: // LineTo
    {
      WPXPropertyList end;
      stream.seek(1, WPX_SEEK_CUR);
      x = readDouble(&stream) + xform.x;
      stream.seek(1, WPX_SEEK_CUR);
      y = (xform.height - readDouble(&stream)) + xform.y;
      rotatePoint(x, y, xform);
      flipPoint(x, y, xform);

      end.insert("svg:x", m_scale*x);
      end.insert("svg:y", m_scale*y);
      end.insert("libwpg:path-action", "L");
      m_currentGeometry[header.id] = end;
    }
    break;
    case 0x8c: // ArcTo
    {
      stream.seek(1, WPX_SEEK_CUR);
      double x2 = readDouble(&stream) + xform.x;
      stream.seek(1, WPX_SEEK_CUR);
      double y2 = (xform.height - readDouble(&stream)) + xform.y;
      stream.seek(1, WPX_SEEK_CUR);
      double bow = readDouble(&stream);

      rotatePoint(x2, y2, xform);
      flipPoint(x2, y2, xform);

      if (bow == 0)
      {
        x = x2; y = y2;
        WPXPropertyList end;
        end.insert("svg:x", m_scale*x);
        end.insert("svg:y", m_scale*y);
        end.insert("libwpg:path-action", "L");
        m_currentGeometry[header.id] = end;
      }
      else
      {
        WPXPropertyList arc;
        double chord = sqrt(pow((y2 - y),2) + pow((x2 - x),2));
        double radius = (4 * bow * bow + chord * chord) / (8 * fabs(bow));
        VSD_DEBUG_MSG(("ArcTo with bow %f radius %f and chord %f\n", bow, radius, chord));
        int largeArc = fabs(bow) > radius ? 1 : 0;
        int sweep = bow < 0 ? 1 : 0;
        x = x2; y = y2;
        arc.insert("svg:rx", m_scale*radius);
        arc.insert("svg:ry", m_scale*radius);
        arc.insert("libwpg:rotate", xform.angle * (180/M_PI));
        arc.insert("libwpg:large-arc", largeArc);
        arc.insert("libwpg:sweep", sweep);
        arc.insert("svg:x", m_scale*x);
        arc.insert("svg:y", m_scale*y);
        arc.insert("libwpg:path-action", "A");
        m_currentGeometry[header.id] = arc;
      }
    }
      break;
    case 0x8f: // Ellipse
    {
      WPXPropertyList ellipse;
      stream.seek(1, WPX_SEEK_CUR);
      double cx = readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      double cy = readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      double aa = readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      /* double bb = */ readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      /* double cc = */ readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      double dd = readDouble(&stream);
      ellipse.insert("svg:rx", m_scale*(aa-cx));
      ellipse.insert("svg:ry", m_scale*(dd-cy));
      ellipse.insert("svg:cx", m_scale*(xform.x+cx));
      ellipse.insert("svg:cy", m_scale*(xform.y+cy));
      ellipse.insert("libwpg:rotate", xform.angle * (180/M_PI));
      painter->drawEllipse(ellipse);
      stream.seek(header.dataLength+header.trailer-54, WPX_SEEK_CUR);
    }
    break;
    case 0x90: // Elliptical ArcTo
    {
      stream.seek(1, WPX_SEEK_CUR);
      double x3 = readDouble(&stream) + xform.x; // End x
      stream.seek(1, WPX_SEEK_CUR);
      double y3 = (xform.height - readDouble(&stream)) + xform.y; // End y
      stream.seek(1, WPX_SEEK_CUR);
      double x2 = readDouble(&stream) + xform.x; // Mid x
      stream.seek(1, WPX_SEEK_CUR);
      double y2 = (xform.height - readDouble(&stream)) + xform.y; // Mid y
      stream.seek(1, WPX_SEEK_CUR);
      double angle = readDouble(&stream); // Angle
      stream.seek(1, WPX_SEEK_CUR);
      double ecc = readDouble(&stream); // Eccentricity

      rotatePoint(x2, y2, xform);
      rotatePoint(x3, y3, xform);
      flipPoint(x2, y2, xform);
      flipPoint(x3, y3, xform);

      double x1 = x;
      double y1 = y;
      double x0 = ((x1-x2)*(x1+x2)*(y2-y3) - (x2-x3)*(x2+x3)*(y1-y2) +
                   ecc*ecc*(y1-y2)*(y2-y3)*(y1-y3)) /
                   (2*((x1-x2)*(y2-y3) - (x2-x3)*(y1-y2)));
      double y0 = ((x1-x2)*(x2-x3)*(x1-x3) + ecc*ecc*(x2-x3)*(y1-y2)*(y1+y2) -
                   ecc*ecc*(x1-x2)*(y2-y3)*(y2+y3)) /
                   (2*ecc*ecc*((x2-x3)*(y1-y2) - (x1-x2)*(y2-y3)));
      VSD_DEBUG_MSG(("Centre: (%f,%f), angle %f\n", x0, y0, angle));
      double rx = sqrt(pow(x1-x0, 2) + ecc*ecc*pow(y1-y0, 2));
      double ry = rx / ecc;

      x = x3; y = y3;
      WPXPropertyList arc;
      int largeArc = 0;
      int sweep = 1;

      // Calculate side of chord that ellipse centre and control point fall on
      double centreSide = (x3-x1)*(y0-y1) - (y3-y1)*(x0-x1);
      double midSide = (x3-x1)*(y2-y1) - (y3-y1)*(x2-x1);
      // Large arc if centre and control point are on the same side
      if ((centreSide > 0 && midSide > 0) || (centreSide < 0 && midSide < 0))
        largeArc = 1;
      // Change direction depending of side of control point
      if (midSide > 0)
        sweep = 0;

      arc.insert("svg:rx", m_scale*rx);
      arc.insert("svg:ry", m_scale*ry);
      arc.insert("libwpg:rotate", -(angle * (180 / M_PI) + xform.angle * (180 / M_PI)));
      arc.insert("libwpg:large-arc", largeArc);
      arc.insert("libwpg:sweep", sweep);
      arc.insert("svg:x", m_scale*x);
      arc.insert("svg:y", m_scale*y);
      arc.insert("libwpg:path-action", "A");
      m_currentGeometry[header.id] = arc;
    }
      break;
    }
    if (geomCount > 0) geomCount--;
    if (geomCount == 0) done = true;

    stream.seek(header.dataLength+header.trailer-(stream.tell()-streamPos), WPX_SEEK_CUR);
  }
  _flushCurrentPath(painter);
}

void libvisio::VSD11Parser::shapeChunk(VSDInternalStream &stream, libwpg::WPGPaintInterface *painter)
{
  WPXPropertyListVector path;
  WPXPropertyList styleProps;
  WPXPropertyListVector gradientProps;
  XForm xform = {0}; // Shape xform data
  ChunkHeader header = {0};
  unsigned long streamPos = 0;

  double x = 0; double y = 0;

  // Reset style
  styleProps.clear();
  styleProps.insert("svg:stroke-width", m_scale*0.0138889);
  styleProps.insert("svg:stroke-color", "black");
  styleProps.insert("draw:fill", "none");
  styleProps.insert("svg:stroke-dasharray", "solid");

  while (!stream.atEOS())
  {
    getChunkHeader(stream, header);
    streamPos = stream.tell();

    // Break once a chunk that is not nested in the shape is found
    if (header.level < 2)
    {
      stream.seek(-19, WPX_SEEK_CUR);
      break;
    }

    VSD_DEBUG_MSG(("Shape: parsing chunk type %x\n", header.chunkType));
    switch (header.chunkType)
    {
    case 0x9b: // XForm data
      xform = _transformXForm(_parseXForm(&stream));
      break;
    case 0x85: // Line properties
    {
      stream.seek(1, WPX_SEEK_CUR);
      styleProps.insert("svg:stroke-width", m_scale*readDouble(&stream));
      stream.seek(1, WPX_SEEK_CUR);
      Colour c = {0};
      c.r = readU8(&stream);
      c.g = readU8(&stream);
      c.b = readU8(&stream);
      c.a = readU8(&stream);
      styleProps.insert("svg:stroke-color", getColourString(c));
      unsigned int linePattern = readU8(&stream);
      const char* patterns[] = {
        /*  0 */  "solid",
        /*  1 */  "solid",
        /*  2 */  "6, 3",
        /*  3 */  "1, 3",
        /*  4 */  "6, 3, 1, 3",
        /*  5 */  "6, 3, 1, 3, 1, 3",
        /*  6 */  "6, 3, 6, 3, 1, 3",
        /*  7 */  "14, 2, 6, 2",
        /*  8 */  "14, 2, 6, 2, 6, 2",
        /*  9 */  "3, 1",
        /* 10 */  "1, 1",
        /* 11 */  "3, 1, 1, 1",
        /* 12 */  "3, 1, 1, 1, 1, 1",
        /* 13 */  "3, 1, 3, 1, 1, 1",
        /* 14 */  "7, 1, 3, 1",
        /* 15 */  "7, 1, 3, 1, 3, 1",
        /* 16 */  "11, 5",
        /* 17 */  "1, 5",
        /* 18 */  "11, 5, 1, 5",
        /* 19 */  "11, 5, 1, 5, 1, 5",
        /* 20 */  "11, 5, 11, 5, 1, 5",
        /* 21 */  "27, 5, 11, 5",
        /* 22 */  "27, 5, 11, 5, 11, 5",
        /* 23 */  "2, 1"
      };
      if (linePattern > 1 && linePattern < sizeof(patterns)/sizeof(patterns[0]))
        styleProps.insert("svg:stroke-dasharray", patterns[linePattern]);
      else
        styleProps.insert("svg:stroke-dasharray", "solid");
    }
      break;
    case 0x86: // Fill & Shadow properties
    {
      unsigned int colourIndexFG = readU8(&stream);
      stream.seek(4, WPX_SEEK_CUR);
      unsigned int colourIndexBG = readU8(&stream);
      stream.seek(4, WPX_SEEK_CUR);
      unsigned int fillPattern = readU8(&stream);
      if (fillPattern == 1)
      {
        styleProps.insert("draw:fill", "solid");
        styleProps.insert("draw:fill-color", getColourString(m_colours[colourIndexFG]));
      }
      else if (fillPattern >= 25 && fillPattern <= 34)
      {
        styleProps.insert("draw:fill", "gradient");
        WPXPropertyList startColour;
        startColour.insert("svg:stop-color",
                           getColourString(m_colours[colourIndexFG]));
        startColour.insert("svg:offset", 0, WPX_PERCENT);
        startColour.insert("svg:stop-opacity", 1, WPX_PERCENT);
        WPXPropertyList endColour;
        endColour.insert("svg:stop-color",
                         getColourString(m_colours[colourIndexBG]));
        endColour.insert("svg:offset", 1, WPX_PERCENT);
        endColour.insert("svg:stop-opacity", 1, WPX_PERCENT);

        switch(fillPattern)
        {
        case 25:
          styleProps.insert("draw:angle", -90);
          break;
        case 26:
          styleProps.insert("draw:angle", -90);
          endColour.insert("svg:offset", 0, WPX_PERCENT);
          gradientProps.append(endColour);
          endColour.insert("svg:offset", 1, WPX_PERCENT);
          startColour.insert("svg:offset", 0.5, WPX_PERCENT);
          break;
        case 27:
          styleProps.insert("draw:angle", 90);
          break;
        case 28:
          styleProps.insert("draw:angle", 0);
          break;
        case 29:
          styleProps.insert("draw:angle", 0);
          endColour.insert("svg:offset", 0, WPX_PERCENT);
          gradientProps.append(endColour);
          endColour.insert("svg:offset", 1, WPX_PERCENT);
          startColour.insert("svg:offset", 0.5, WPX_PERCENT);
          break;
        case 30:
          styleProps.insert("draw:angle", 180);
          break;
        case 31:
          styleProps.insert("draw:angle", -45);
          break;
        case 32:
          styleProps.insert("draw:angle", 45);
          break;
        case 33:
          styleProps.insert("draw:angle", 225);
          break;
        case 34:
          styleProps.insert("draw:angle", 135);
          break;
        }
        gradientProps.append(startColour);
        gradientProps.append(endColour);
      }
    }
      break;
    case 0x6c: // GeomList
    {
      _flushCurrentPath(painter);
      painter->setStyle(styleProps, gradientProps);
      uint32_t subHeaderLength = readU32(&stream);
      uint32_t childrenListLength = readU32(&stream);
      stream.seek(subHeaderLength, WPX_SEEK_CUR);
      for (unsigned i = 0; i < (childrenListLength / sizeof(uint32_t)); i++)
        m_currentGeometryOrder.push_back(readU32(&stream));
      break;
    }
    case 0x8a: // MoveTo
    {
      WPXPropertyList end;
      stream.seek(1, WPX_SEEK_CUR);
      x = readDouble(&stream) + xform.x;
      stream.seek(1, WPX_SEEK_CUR);
      y = (xform.height - readDouble(&stream)) + xform.y;
      rotatePoint(x, y, xform);
      flipPoint(x, y, xform);

      end.insert("svg:x", m_scale*x);
      end.insert("svg:y", m_scale*y);
      end.insert("libwpg:path-action", "M");
      m_currentGeometry[header.id] = end;
    }
      break;
    case 0x8b: // LineTo
    {
      WPXPropertyList end;
      stream.seek(1, WPX_SEEK_CUR);
      x = readDouble(&stream) + xform.x;
      stream.seek(1, WPX_SEEK_CUR);
      y = (xform.height - readDouble(&stream)) + xform.y;
      rotatePoint(x, y, xform);
      flipPoint(x, y, xform);

      end.insert("svg:x", m_scale*x);
      end.insert("svg:y", m_scale*y);
      end.insert("libwpg:path-action", "L");
      m_currentGeometry[header.id] = end;
    }
    break;
    case 0x8c: // ArcTo
    {
      stream.seek(1, WPX_SEEK_CUR);
      double x2 = readDouble(&stream) + xform.x;
      stream.seek(1, WPX_SEEK_CUR);
      double y2 = (xform.height - readDouble(&stream)) + xform.y;
      stream.seek(1, WPX_SEEK_CUR);
      double bow = readDouble(&stream);

      rotatePoint(x2, y2, xform);
      flipPoint(x2, y2, xform);

      if (bow == 0)
      {
        x = x2; y = y2;
        WPXPropertyList end;
        end.insert("svg:x", m_scale*x);
        end.insert("svg:y", m_scale*y);
        end.insert("libwpg:path-action", "L");
        m_currentGeometry[header.id] = end;
      }
      else
      {
        WPXPropertyList arc;
        double chord = sqrt(pow((y2 - y),2) + pow((x2 - x),2));
        double radius = (4 * bow * bow + chord * chord) / (8 * fabs(bow));
        VSD_DEBUG_MSG(("ArcTo with bow %f radius %f and chord %f\n", bow, radius, chord));
        int largeArc = fabs(bow) > radius ? 1 : 0;
        int sweep = bow < 0 ? 1 : 0;
        x = x2; y = y2;
        arc.insert("svg:rx", m_scale*radius);
        arc.insert("svg:ry", m_scale*radius);
        arc.insert("libwpg:rotate", xform.angle * (180/M_PI));
        arc.insert("libwpg:large-arc", largeArc);
        arc.insert("libwpg:sweep", sweep);
        arc.insert("svg:x", m_scale*x);
        arc.insert("svg:y", m_scale*y);
        arc.insert("libwpg:path-action", "A");
        m_currentGeometry[header.id] = arc;
      }
    }
      break;
    case 0x8f: // Ellipse
    {
      WPXPropertyList ellipse;
      stream.seek(1, WPX_SEEK_CUR);
      double cx = readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      double cy = readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      double aa = readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      /* double bb = */ readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      /* double cc = */ readDouble(&stream);
      stream.seek(1, WPX_SEEK_CUR);
      double dd = readDouble(&stream);
      ellipse.insert("svg:rx", m_scale*(aa-cx));
      ellipse.insert("svg:ry", m_scale*(dd-cy));
      ellipse.insert("svg:cx", m_scale*(xform.x+cx));
      ellipse.insert("svg:cy", m_scale*(xform.y+cy));
      ellipse.insert("libwpg:rotate", xform.angle * (180/M_PI));
      painter->drawEllipse(ellipse);
      stream.seek(header.dataLength+header.trailer-54, WPX_SEEK_CUR);
    }
    break;
    case 0x90: // Elliptical ArcTo
    {
      stream.seek(1, WPX_SEEK_CUR);
      double x3 = readDouble(&stream) + xform.x; // End x
      stream.seek(1, WPX_SEEK_CUR);
      double y3 = (xform.height - readDouble(&stream)) + xform.y; // End y
      stream.seek(1, WPX_SEEK_CUR);
      double x2 = readDouble(&stream) + xform.x; // Mid x
      stream.seek(1, WPX_SEEK_CUR);
      double y2 = (xform.height - readDouble(&stream)) + xform.y; // Mid y
      stream.seek(1, WPX_SEEK_CUR);
      double angle = readDouble(&stream); // Angle
      stream.seek(1, WPX_SEEK_CUR);
      double ecc = readDouble(&stream); // Eccentricity

      rotatePoint(x2, y2, xform);
      rotatePoint(x3, y3, xform);
      flipPoint(x2, y2, xform);
      flipPoint(x3, y3, xform);

      double x1 = x;
      double y1 = y;
      double x0 = ((x1-x2)*(x1+x2)*(y2-y3) - (x2-x3)*(x2+x3)*(y1-y2) +
                   ecc*ecc*(y1-y2)*(y2-y3)*(y1-y3)) /
                   (2*((x1-x2)*(y2-y3) - (x2-x3)*(y1-y2)));
      double y0 = ((x1-x2)*(x2-x3)*(x1-x3) + ecc*ecc*(x2-x3)*(y1-y2)*(y1+y2) -
                   ecc*ecc*(x1-x2)*(y2-y3)*(y2+y3)) /
                   (2*ecc*ecc*((x2-x3)*(y1-y2) - (x1-x2)*(y2-y3)));
      VSD_DEBUG_MSG(("Centre: (%f,%f), angle %f\n", x0, y0, angle));
      double rx = sqrt(pow(x1-x0, 2) + ecc*ecc*pow(y1-y0, 2));
      double ry = rx / ecc;

      x = x3; y = y3;
      WPXPropertyList arc;
      int largeArc = 0;
      int sweep = 1;

      // Calculate side of chord that ellipse centre and control point fall on
      double centreSide = (x3-x1)*(y0-y1) - (y3-y1)*(x0-x1);
      double midSide = (x3-x1)*(y2-y1) - (y3-y1)*(x2-x1);
      // Large arc if centre and control point are on the same side
      if ((centreSide > 0 && midSide > 0) || (centreSide < 0 && midSide < 0))
        largeArc = 1;
      // Change direction depending of side of control point
      if (midSide > 0)
        sweep = 0;

      arc.insert("svg:rx", m_scale*rx);
      arc.insert("svg:ry", m_scale*ry);
      arc.insert("libwpg:rotate", -(angle * (180 / M_PI) + xform.angle * (180 / M_PI)));
      arc.insert("libwpg:large-arc", largeArc);
      arc.insert("libwpg:sweep", sweep);
      arc.insert("svg:x", m_scale*x);
      arc.insert("svg:y", m_scale*y);
      arc.insert("libwpg:path-action", "A");
      m_currentGeometry[header.id] = arc;
    }
      break;
    }

    stream.seek(header.dataLength+header.trailer-(stream.tell()-streamPos), WPX_SEEK_CUR);
  }
  _flushCurrentPath(painter);
}

void libvisio::VSD11Parser::foreignChunk(VSDInternalStream &stream, libwpg::WPGPaintInterface *painter)
{
  XForm xform = {0}; // Shape xform data
  unsigned int foreignType = 0; // Tracks current foreign data type
  unsigned int foreignFormat = 0; // Tracks foreign data format
  ChunkHeader header = {0};
  unsigned long tmpBytesRead = 0;
  bool done = false;

  while (!done && !stream.atEOS())
  {
    getChunkHeader(stream, header);

    // Break once a chunk that is not nested in the foreign is found
    if (header.level < 2)
    {
      stream.seek(-19, WPX_SEEK_CUR);
      break;
    }

    switch(header.chunkType)
    {
    case 0x9b: // XForm data
      xform = _transformXForm(_parseXForm(&stream));
      stream.seek(header.dataLength+header.trailer-65, WPX_SEEK_CUR);
      break;
    case 0x98:
      stream.seek(0x24, WPX_SEEK_CUR);
      foreignType = readU16(&stream);
      stream.seek(0xb, WPX_SEEK_CUR);
      foreignFormat = readU32(&stream);

      stream.seek(header.dataLength+header.trailer-0x35, WPX_SEEK_CUR);
      VSD_DEBUG_MSG(("Found foreign data, type %d format %d\n", foreignType, foreignFormat));

      break;
    case 0x0c:
      if (foreignType == 1 || foreignType == 4) // Image
      {
        const unsigned char *buffer = stream.read(header.dataLength, tmpBytesRead);
        WPXBinaryData binaryData;
        // If bmp data found, reconstruct header
        if (foreignType == 1 && foreignFormat == 0)
        {
          binaryData.append(0x42);
          binaryData.append(0x4d);

          binaryData.append((unsigned char)((tmpBytesRead + 14) & 0x000000ff));
          binaryData.append((unsigned char)(((tmpBytesRead + 14) & 0x0000ff00) >> 8));
          binaryData.append((unsigned char)(((tmpBytesRead + 14) & 0x00ff0000) >> 16));
          binaryData.append((unsigned char)(((tmpBytesRead + 14) & 0xff000000) >> 24));

          binaryData.append(0x00);
          binaryData.append(0x00);
          binaryData.append(0x00);
          binaryData.append(0x00);

          binaryData.append(0x36);
          binaryData.append(0x00);
          binaryData.append(0x00);
          binaryData.append(0x00);
        }
        binaryData.append(buffer, tmpBytesRead);

#if DUMP_BITMAP
        if (foreignType == 1 || foreignType == 4)
        {
          std::ostringstream filename;
          switch(foreignFormat)
          {
          case 0:
            filename << "binarydump" << bitmapId++ << ".bmp"; break;
          case 1:
            filename << "binarydump" << bitmapId++ << ".jpeg"; break;
          case 2:
            filename << "binarydump" << bitmapId++ << ".gif"; break;
          case 3:
            filename << "binarydump" << bitmapId++ << ".tiff"; break;
          case 4:
            filename << "binarydump" << bitmapId++ << ".png"; break;
          default:
            filename << "binarydump" << bitmapId++ << ".bin"; break;
          }
          FILE *f = fopen(filename.str().c_str(), "wb");
          if (f)
          {
            const unsigned char *tmpBuffer = binaryData.getDataBuffer();
            for (unsigned long k = 0; k < binaryData.size(); k++)
              fprintf(f, "%c",tmpBuffer[k]);
            fclose(f);
          }
        }
#endif

        WPXPropertyList foreignProps;
        foreignProps.insert("svg:width", m_scale*xform.width);
        foreignProps.insert("svg:height", m_scale*xform.height);
        foreignProps.insert("svg:x", m_scale*(xform.pinX - xform.pinLocX));
        // Y axis starts at the bottom not top
        foreignProps.insert("svg:y", m_scale*(m_pageHeight -
                            xform.pinY + xform.pinLocY - xform.height));

        if (foreignType == 1)
        {
          switch(foreignFormat)
          {
          case 0:
            foreignProps.insert("libwpg:mime-type", "image/bmp"); break;
          case 1:
            foreignProps.insert("libwpg:mime-type", "image/jpeg"); break;
          case 2:
            foreignProps.insert("libwpg:mime-type", "image/gif"); break;
          case 3:
            foreignProps.insert("libwpg:mime-type", "image/tiff"); break;
          case 4:
            foreignProps.insert("libwpg:mime-type", "image/png"); break;
          }
        }
        else if (foreignType == 4)
        {
          const unsigned char *tmpBinData = binaryData.getDataBuffer();
          // Check for EMF signature
          if (tmpBinData[0x28] == 0x20 && tmpBinData[0x29] == 0x45 && tmpBinData[0x2A] == 0x4D && tmpBinData[0x2B] == 0x46)
          {
            foreignProps.insert("libwpg:mime-type", "image/emf");
          }
          else
          {
            foreignProps.insert("libwpg:mime-type", "image/wmf");
          }
        }

        painter->drawGraphicObject(foreignProps, binaryData);
      }
      else
      {
        stream.seek(header.dataLength+header.trailer, WPX_SEEK_CUR);
      }
      break;

    default:
      stream.seek(header.dataLength+header.trailer, WPX_SEEK_CUR);

    }
  }
}

void libvisio::VSD11Parser::getChunkHeader(VSDInternalStream &stream, libvisio::VSD11Parser::ChunkHeader &header)
{
  unsigned char tmpChar = 0;
  while (!stream.atEOS() && !tmpChar)
    tmpChar = readU8(&stream);

  if (stream.atEOS())
    return;
  else
    stream.seek(-1, WPX_SEEK_CUR);

  header.chunkType = readU32(&stream);
  header.id = readU32(&stream);
  header.list = readU32(&stream);

   // Certain chunk types seem to always have a trailer
  header.trailer = 0;
  if (header.list != 0 || header.chunkType == 0x71 || header.chunkType == 0x70 ||
      header.chunkType == 0x6b || header.chunkType == 0x6a || header.chunkType == 0x69 ||
      header.chunkType == 0x66 || header.chunkType == 0x65 || header.chunkType == 0x2c)
    header.trailer += 8; // 8 byte trailer

  header.dataLength = readU32(&stream);
  header.level = readU16(&stream);
  header.unknown = readU8(&stream);

  // Add word separator under certain circumstances for v11
  // Below are known conditions, may be more or a simpler pattern
  if (header.list != 0 || (header.level == 2 && header.unknown == 0x55) ||
      (header.level == 2 && header.unknown == 0x54 && header.chunkType == 0xaa)
      || (header.level == 3 && header.unknown != 0x50 && header.unknown != 0x54) ||
      header.chunkType == 0x69 || header.chunkType == 0x6a || header.chunkType == 0x6b ||
      header.chunkType == 0x71 || header.chunkType == 0xb6 || header.chunkType == 0xb9 ||
      header.chunkType == 0xa9 || header.chunkType == 0x92)
  {
    header.trailer += 4;
  }
  // 0x1f (OLE data) and 0xc9 (Name ID) never have trailer
  if (header.chunkType == 0x1f || header.chunkType == 0xc9)
  {
    header.trailer = 0;
  }
}

void libvisio::VSD11Parser::rotatePoint(double &x, double &y, const XForm &xform)
{
  if (xform.angle == 0.0) return;

  // Calculate co-ordinates using pin position as origin
  double tmpX = x - xform.pinX;
  double tmpY = (m_pageHeight - y) - xform.pinY; // Start from bottom left

  // Rotate around pin and move back to bottom left as origin
  x = (tmpX * cos(xform.angle)) - (tmpY * sin(xform.angle)) + xform.pinX;
  y = (tmpX * sin(xform.angle)) + (tmpY * cos(xform.angle)) + xform.pinY;
  y = m_pageHeight - y; // Flip Y for screen co-ordinate
}

void libvisio::VSD11Parser::flipPoint(double &x, double &y, const XForm &xform)
{
  if (!xform.flipX && !xform.flipY) return;

  double tmpX = x - xform.x;
  double tmpY = y - xform.y;

  if (xform.flipX)
    tmpX = xform.width - tmpX;
  if (xform.flipY)
    tmpY = xform.height - tmpY;
  x = tmpX + xform.x;
  y = tmpY + xform.y;
}

libvisio::VSDXParser::XForm libvisio::VSD11Parser::_parseXForm(WPXInputStream *input)
{
  libvisio::VSDXParser::XForm xform;
  input->seek(1, WPX_SEEK_CUR);
  xform.pinX = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.pinY = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.width = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.height = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.pinLocX = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.pinLocY = readDouble(input);
  input->seek(1, WPX_SEEK_CUR);
  xform.angle = readDouble(input);
  xform.flipX = (readU8(input) != 0);
  xform.flipY = (readU8(input) != 0);

  xform.x = xform.pinX - xform.pinLocX;
  xform.y = m_pageHeight - xform.pinY + xform.pinLocY - xform.height;

  return xform;
}

libvisio::VSDXParser::XForm libvisio::VSD11Parser::_transformXForm(const libvisio::VSDXParser::XForm &xform)
{
  libvisio::VSDXParser::XForm tmpXForm = xform;
  std::map<unsigned int, XForm>::iterator iter = m_groupXForms.find(m_currentShapeId);
  if  (iter != m_groupXForms.end()) {
    tmpXForm.pinX += iter->second.pinX;
    tmpXForm.pinY += iter->second.pinY;
    tmpXForm.pinLocX += iter->second.pinLocX;
    tmpXForm.pinLocY += iter->second.pinLocY;
  }
  tmpXForm.x = tmpXForm.pinX - tmpXForm.pinLocX;
  tmpXForm.y = m_pageHeight - tmpXForm.pinY + tmpXForm.pinLocY - tmpXForm.height;

  return tmpXForm;
}

void libvisio::VSD11Parser::_flushCurrentPath(libwpg::WPGPaintInterface *painter)
{
  double startX = 0; double startY = 0;
  double x = 0; double y = 0;
  bool broken = false;
  bool firstPoint = true;

  WPXPropertyListVector path;
  std::map<unsigned int, WPXPropertyList>::iterator iter;
  std::map<unsigned int, WPXPropertyListVector>::iterator itervec;
  if (m_currentGeometryOrder.size())
  {
    for (unsigned i = 0; i < m_currentGeometryOrder.size(); i++)
    {
      iter = m_currentGeometry.find(m_currentGeometryOrder[i]);
      if (iter != m_currentGeometry.end())
      {
        x = (iter->second)["svg:x"]->getDouble();
        y = (iter->second)["svg:y"]->getDouble();
        if (firstPoint)
        {
          startX = x;
          startY = y;
          firstPoint = false;
        }
        else if (!broken && ((iter->second)["libwpg:path-action"]->getStr() == "M"))
          broken = true;

        path.append(iter->second);
      }
      else
      {
        itervec = m_currentComplexGeometry.find(m_currentGeometryOrder[i]);
        if (itervec != m_currentComplexGeometry.end())
        {
          WPXPropertyListVector::Iter iter2(itervec->second);
          for (; iter2.next();)
          {
             path.append(iter2());

             // Only interested in last point of complex geometry
             x = (iter2())["svg:x"]->getDouble();
             y = (iter2())["svg:y"]->getDouble();
          }
        }
      }
    }

    // Last geom
    if (!broken && !(startX == x && startY == y))
    {
      broken = true;
    }
    if (!broken && path.count())
    {
      WPXPropertyList closedPath;
      closedPath.insert("libwpg:path-action", "Z");
      path.append(closedPath);
    }
  }
  else
  {
    for (iter=m_currentGeometry.begin(); iter != m_currentGeometry.end(); iter++)
      path.append(iter->second);
    for (itervec=m_currentComplexGeometry.begin(); itervec != m_currentComplexGeometry.end(); itervec++)
    {
      WPXPropertyListVector::Iter iter2(itervec->second);
      for (; iter2.next();)
        path.append(iter2());
    }
  }
  if (path.count())
    painter->drawPath(path);
  m_currentGeometry.clear();
  m_currentComplexGeometry.clear();
  m_currentGeometryOrder.clear();
}
