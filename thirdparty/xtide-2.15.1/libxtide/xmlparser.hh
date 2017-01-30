// $Id: xmlparser.hh 5748 2014-10-11 19:38:53Z flaterco $

// xmlparser.hh:  declarations for access to XML parser.

/*  Copyright (C) 2007  David Flater.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

struct xmlattribute {
  Dstr *name;
  Dstr *value;
  xmlattribute *next;
};

struct xmltag {
  Dstr *name;
  xmlattribute *attributes;
  xmltag *contents;
  xmltag *next;
};

extern xmltag *xmlparsetree;
extern Dstr xmlfilename;
extern FILE *yyin;
int yyparse();

// ECHO is defined by /usr/include/asm/termbits.h; we don't want that.
// (Messes up the XML parser.)
#undef ECHO
#define ECHO  /* Don't echo error text */
