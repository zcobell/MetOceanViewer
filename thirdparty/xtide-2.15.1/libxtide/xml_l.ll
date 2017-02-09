/*  $Id: xml_l.ll 5748 2014-10-11 19:38:53Z flaterco $

    xml_l.ll  Flex file for limited XML parser (just tags, no text).

    This is *NOT* a general-purpose XML parser.

    All things beginning with <! are treated as comments which are
    terminated by the next > character.  This means that you can't do
    the following (shown in the XML FAQ):

      <!DOCTYPE foo [
        <!ENTITY alephhb cdata "&#224;">
      ]>

    However, it should be possible to add a simple DOCTYPE declaration
    at such a time that I have a DTD:

      <!DOCTYPE xtide SYSTEM "xtide.dtd">

    Copyright (C) 1997  David Flater.

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

%{

#include "libxtide/libxtide.hh"
using namespace libxtide;
#include "libxtide/xmlparser.hh"
#include "xml_y.h"

/* Communicate autoconf result (HAVE_UNISTD_H) to flex/bison
   (YY_NO_UNISTD_H). */
#ifndef HAVE_UNISTD_H
#define YY_NO_UNISTD_H
#endif

unsigned lineno = 1;

%}

%%

\n	++lineno;

[ \t\r]+	;

"<?"[^\n]*"?>"	{ return XMLSTART; }

"<!"[^>]*">"	{ // Count lines consumed
                  for (unsigned a=0; a<strlen(yytext); ++a)
                    if (yytext[a] == '\n')
                      ++lineno; }

\"[^"\n]*\"	{ // Remove delimiting quotes
                  yylval.value = new Dstr (yytext+1);
                  (*yylval.value) -= (*yylval.value).length() - 1;
                  return ATTVALUE; }

"="	{ return '='; }

"/"	{ return '/'; }

">"	{ return '>'; }

"<"	{ return '<'; }

[a-zA-Z0-9:_]*	{ yylval.value = new Dstr (yytext);
                  return NAME; }

%%

int yywrap() {
  return 1;
}

void yyerror (constString s unusedParameter) {
  Dstr details ("Syntax error in XML file ");
  details += xmlfilename;
  details += " at line ";
  details += lineno;
  Global::barf (Error::XMLPARSE, details);
}
