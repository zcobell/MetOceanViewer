/*  $Id: xml_y.yy 5748 2014-10-11 19:38:53Z flaterco $

    xml_y.yy  Bison file for limited XML parser (just tags, no text).

    This is *NOT* a general-purpose XML parser.

    Note that all lists except for the top level one are returned in
    reverse order.

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

Dstr xmlfilename;
xmltag *xmlparsetree;

extern int yylex ();
extern void yyerror (constString s unusedParameter);

#define balance(lname,rname) {                            \
      if ((*(lname)) != (*(rname))) {                     \
        Dstr details ("Parse error in XML file ");        \
        details += xmlfilename;                           \
        details += "\n<";                                 \
        details += (*(lname));                            \
        details += "> ended by </";                       \
        details += (*(rname));                            \
        details += ">";                                   \
        Global::barf (Error::XMLPARSE, details);          \
      }                                                   \
      delete rname; }

%}

%union {
  xmltag *tag;
  xmlattribute *attribute;
  Dstr *value;
}

%token <value> NAME ATTVALUE
%token XMLSTART

%type <tag> tag xml_file taglist
%type <attribute> attribute avlist

%%

xml_file: XMLSTART taglist
    { // Un-reverse the top-level list
      xmltag *temp;
      $$ = NULL;
      while ($2) {
        temp = $2->next;
        $2->next = $$;
        $$ = $2;
        $2 = temp;
      }
      xmlparsetree = $$; }
;

taglist: tag
    { $$ = $1; }
     | taglist tag
    { $$ = $2;
      $$->next = $1; }
;

tag:   '<' NAME avlist '>' '<' '/' NAME '>'
    {
      balance ($2, $7);
      $$ = new xmltag;
      $$->name = $2;
      $$->attributes = $3;
      $$->contents = NULL;
      $$->next = NULL;
    }
     | '<' NAME avlist '>' taglist '<' '/' NAME '>'
    {
      balance ($2, $8);
      $$ = new xmltag;
      $$->name = $2;
      $$->attributes = $3;
      $$->contents = $5;
      $$->next = NULL;
    }
     | '<' NAME avlist '/' '>'
    {
      $$ = new xmltag;
      $$->name = $2;
      $$->attributes = $3;
      $$->contents = NULL;
      $$->next = NULL;
    }
     | '<' NAME '>' '<' '/' NAME '>'
    {
      balance ($2, $6);
      $$ = new xmltag;
      $$->name = $2;
      $$->attributes = NULL;
      $$->contents = NULL;
      $$->next = NULL;
    }
     | '<' NAME '>' taglist '<' '/' NAME '>'
    {
      balance ($2, $7);
      $$ = new xmltag;
      $$->name = $2;
      $$->attributes = NULL;
      $$->contents = $4;
      $$->next = NULL;
    }
     | '<' NAME '/' '>'
    {
      $$ = new xmltag;
      $$->name = $2;
      $$->attributes = NULL;
      $$->contents = NULL;
      $$->next = NULL;
    }
;

avlist: attribute
    { $$ = $1; }
     | avlist attribute
    { $$ = $2;
      $$->next = $1; }
;

attribute: NAME '=' ATTVALUE
    { $$ = new xmlattribute;
      $$->name = $1;
      $$->value = $3;
      $$->next = NULL; }
;

%%
