/* libvisio
 * Copyright (C) 2011 Fridrich Strba <fridrich.strba@bluewin.ch>
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
 *
 * For further information visit http://libwpg.sourceforge.net
 */

#ifndef __VISIODOCUMENT_H__
#define __VISIODOCUMENT_H__

#include <libwpd/libwpd.h>
#include <libwpg/libwpg.h>

class WPXInputStream;

namespace libvisio
{

class VisioDocument
{
public:
	
	static bool isSupported(WPXInputStream* input);
	
	static bool parse(WPXInputStream* input, libwpg::WPGPaintInterface* painter);

	static bool generateSVG(WPXInputStream* input, WPXString& output);
};

} // namespace libvisio

#endif //  __VISIODOCUMENT_H__