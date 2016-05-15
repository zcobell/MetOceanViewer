// $Id: Dstr.cc 5062 2013-07-21 15:21:28Z flaterco $
// Dstr:  Dave's String class.

// Canonicalized 2005-05-05.
// Moved into libdstr 2007-02-06.

// This source is public domain.  As if you would want it.

// Perpetrator:  David Flater.

// See Dstr header file for documentation.


#ifndef require
#define require(expr) {       \
  bool require_expr ((expr)); \
  assert (require_expr);      \
}
#endif

#include "Dstr.hh"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


static char emptyString[] = {'\0'};


// ctype.h considered harmful.  I want Latin-1 to work regardless.

static char tolower (char c) {
  // A..Z and a..z are offset by 0x20.
  // 0xC0 - 0xDF and 0xE0 - 0xFF are similarly matched uppers and lowers,
  // EXCEPT:
  //   215 (0xD7) is ×; 247 (0xF7) is ÷.
  //   223 (0xDF) is ß; 255 (0xFF) is ÿ.
  // See:
  //   man ascii
  //   man iso_8859-1
  if ((c >= 'A' && c <= 'Z') || (c >= 'À' && c < 'ß' && c != '×'))
    return c + 0x20;
  return c;
}

// int is still used in several places because that's what getc returns.
static bool isspace (int c) {
  switch (c) {
  case ' ':
  case '\f':
  case '\n':
  case '\r':
  case '\t':
  case '\v':
    return true;

  // 0xA0, NBSP, non-breaking space, is not a space.  (Duh.)
  // Chars between 0x80 and 0x9F are never used.

  default:
    return false;
  }
}

static bool isligature (char c) {
  switch (c) {
  case '¼':
  case '½':
  case '¾':
  case 'Æ':
  case 'æ':
  case 'ß':
    return true;
  default:
    return false;
  }
}


// ***************************************************************************

// This array contains the Level 1 weights for our Latin-1 character
// set from the Default Unicode Collation Element Table, version
// 4.1.0, downloaded 2005-05-06 from
// http://www.unicode.org/Public/UCA/4.1.0/allkeys.txt.

// In Unicode 4.0.0, æ came between a and b, ğ came between d and e,
// and ø came between o and p.  This is no longer the case in Unicode
// 4.1.0.  Æ is now expanded similar to ß, and the others are treated
// as diacriticals.

// 2006-10-26:  In Unicode 5.0, the weights have changed but the
// relative ordering of our 256 characters is unaffected.  Keeping the
// 4.1.0 values.

// Values extracted as follows:
//   grep "^00" allkeys.txt | sort | cut -b11-14

// And then:

#if 0
#include <stdio.h>
int main () {
  int i;
  char buf[80];
  for (i=0; i<256; ++i) {
    gets (buf);
    if (i)
      printf (", ");
    printf ("0x%s", buf);
  }
  return 0;
}
#endif

static short unsigned int collation[256] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0201, 0x0202, 0x0203, 0x0204, 0x0205, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0209, 0x0253, 0x0295, 0x02F3, 0x0F43, 0x02F4, 0x02F0, 0x028E,
  0x029F, 0x02A0, 0x02E9, 0x0488, 0x0230, 0x0221, 0x0261, 0x02ED,
  0x0F62, 0x0F63, 0x0F64, 0x0F65, 0x0F66, 0x0F67, 0x0F68, 0x0F69,
  0x0F6A, 0x0F6B, 0x023E, 0x023B, 0x048C, 0x048D, 0x048E, 0x0257,
  0x02E8, 0x0F6C, 0x0F85, 0x0F9D, 0x0FAE, 0x0FC7, 0x0FF9, 0x1003,
  0x1026, 0x103C, 0x1054, 0x1069, 0x1077, 0x10A1, 0x10AC, 0x10CC,
  0x10EE, 0x10FE, 0x110A, 0x1137, 0x1157, 0x1179, 0x11A1, 0x11AF,
  0x11B8, 0x11BD, 0x11C9, 0x02A1, 0x02EF, 0x02A2, 0x020F, 0x021B,
  0x020C, 0x0F6C, 0x0F85, 0x0F9D, 0x0FAE, 0x0FC7, 0x0FF9, 0x1003,
  0x1026, 0x103C, 0x1054, 0x1069, 0x1077, 0x10A1, 0x10AC, 0x10CC,
  0x10EE, 0x10FE, 0x110A, 0x1137, 0x1157, 0x1179, 0x11A1, 0x11AF,
  0x11B8, 0x11BD, 0x11C9, 0x02A3, 0x0490, 0x02A4, 0x0493, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0206, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0209, 0x0254, 0x0F42, 0x0F44, 0x0F41, 0x0F45, 0x0491, 0x02E3,
  0x0214, 0x02E6, 0x0F6C, 0x029D, 0x048F, 0x0220, 0x02E7, 0x0210,
  0x03A5, 0x0489, 0x0F64, 0x0F65, 0x020D, 0x125E, 0x02E4, 0x026D,
  0x0219, 0x0F63, 0x10CC, 0x029E, 0x0F63, 0x0F63, 0x0F65, 0x0258,
  0x0F6C, 0x0F6C, 0x0F6C, 0x0F6C, 0x0F6C, 0x0F6C, 0x0F6C, 0x0F9D,
  0x0FC7, 0x0FC7, 0x0FC7, 0x0FC7, 0x103C, 0x103C, 0x103C, 0x103C,
  0x0FAE, 0x10AC, 0x10CC, 0x10CC, 0x10CC, 0x10CC, 0x10CC, 0x048B,
  0x10CC, 0x1179, 0x1179, 0x1179, 0x1179, 0x11BD, 0x11FA, 0x1137,
  0x0F6C, 0x0F6C, 0x0F6C, 0x0F6C, 0x0F6C, 0x0F6C, 0x0F6C, 0x0F9D,
  0x0FC7, 0x0FC7, 0x0FC7, 0x0FC7, 0x103C, 0x103C, 0x103C, 0x103C,
  0x0FAE, 0x10AC, 0x10CC, 0x10CC, 0x10CC, 0x10CC, 0x10CC, 0x048A,
  0x10CC, 0x1179, 0x1179, 0x1179, 0x1179, 0x11BD, 0x11FA, 0x11BD
};

// List of equivalence classes generated with this
#if 0
#include <stdio.h>
int main () {
  int i, j, isfirst;
  for (i=0; i<65536; ++i) {
    isfirst=1;
    for (j=0; j<256; ++j) {
      if (collation[j] == i) {
        if (isfirst) {
          isfirst = 0;
          printf ("%04X ", i);
        }
        printf ("%c", j);
      }
    }
    if (!isfirst)
      printf ("\n");
  }
  return 0;
}
#endif

/* Level 1 equivalence classes (without expanding ligatures):

0000 [a slew of ugly control characters]
0201 TAB (HT)
0202 LF (NL)
0203 VT
0204 FF (NP)
0205 CR
0206 NEL
0209 SP and NBSP
020C `
020D ´
020F ^
0210 ¯
0214 ¨
0219 ¸
021B _
0220 ­
0221 -
0230 ,
023B ;
023E :
0253 !
0254 ¡
0257 ?
0258 ¿
0261 .
026D ·
028E '
0295 "
029D «
029E »
029F (
02A0 )
02A1 [
02A2 ]
02A3 {
02A4 }
02E3 §
02E4 ¶
02E6 ©
02E7 ®
02E8 @
02E9 *
02ED /
02EF \
02F0 &
02F3 #
02F4 %
03A5 °
0488 +
0489 ±
048A ÷
048B ×
048C <
048D =
048E >
048F ¬
0490 |
0491 ¦
0493 ~
0F41 ¤
0F42 ¢
0F43 $
0F44 £
0F45 ¥
0F62 0
0F63 1¹¼½
0F64 2²
0F65 3³¾
0F66 4
0F67 5
0F68 6
0F69 7
0F6A 8
0F6B 9
0F6C AaªÀÁÂÃÄÅÆàáâãäåæ
0F85 Bb
0F9D CcÇç
0FAE DdĞğ
0FC7 EeÈÉÊËèéêë
0FF9 Ff
1003 Gg
1026 Hh
103C IiÌÍÎÏìíîï
1054 Jj
1069 Kk
1077 Ll
10A1 Mm
10AC NnÑñ
10CC OoºÒÓÔÕÖØòóôõöø
10EE Pp
10FE Qq
110A Rr
1137 Ssß
1157 Tt
1179 UuÙÚÛÜùúûü
11A1 Vv
11AF Ww
11B8 Xx
11BD Yyİıÿ
11C9 Zz
11FA Şş
125E µ

*/

// ***************************************************************************

// moascf stands for "mother of all string comparison functions."  It
// must be optimized to avoid killing search and sort performance.
// Generally this means put off calling expand_ligatures for as long
// as possible.

// If slack is true, behavior is %=; otherwise, it's dstrcasecmp.
// %= (slack):
//   Insensitive comparison that allows val2 to be shorter.
//   Analogous to strncasecmp (val1, val2, strlen(val2)).
//   Returns 0 if match, nonzero if not match.  No ordering.
// dstrcasecmp (!slack):
//   <0 means val1 < val2
//   >0 means val1 > val2
//    0 means val1 == val2

static int moascf (const char *val1, const char *val2, bool slack) {
  assert (val1);
  assert (val2);

  // Minor optimization opportunity:  avoid making these copies when
  // there are no ligatures.  (Deferred to copious free time.)
  Dstr val1x (val1), val2x (val2);

  // For slack = true:  There is no early out at the top when
  // val1x.length() < val2x.length() because we can't know that yet
  // unless we expand_ligatures.  Since all we care is whether they
  // match or not, it's just as good if we find a mismatch in the loop
  // below.

  unsigned n = val2x.length();
  if (val1x.length() < n)
    n = val1x.length();

  int c;
  for (unsigned i=0; i<n; ++i) {

    // expand_ligatures() is expensive.  Call it only when necessary.
    if (isligature(val1x[i]) || isligature (val2x[i])) {
      // There were no ligatures up to index i; ergo, there is no need
      // to start over from the beginning.  If we skipped over
      // ligatures in the case where val1x[i] == val2x[i] then we
      // would have to start over.
      val1x.expand_ligatures();
      val2x.expand_ligatures();
      n = val2x.length();
      if (val1x.length() < n) {
	if (slack)
	  return 1;
	n = val1x.length();
      }
    }

    if ((c = (int)collation[(unsigned char)val1x[i]] -
	     (int)collation[(unsigned char)val2x[i]]))
      return c;
  }

  // If we got this far without expanding ligatures, then the shorter
  // string has no ligatures in it and the longer string has no
  // ligatures out to the length of the shorter one.  So if we
  // expanded ligatures at this point, the only thing that could
  // happen would be that the longer string would get longer yet as a
  // result of replacements that would have no meaningful impact on
  // the result.

  c = (int)(val1x.length()) - (int)(val2x.length());
  if (slack)
    return (c < 0);
  return c;
}

int dstrcasecmp (const Dstr &val1, const Dstr &val2) {
  return moascf (val1.aschar(), val2.aschar(), false);
}

int dstrcasecmp (const Dstr &val1, const char *val2) {
  return moascf (val1.aschar(), val2, false);
}

int dstrcasecmp (const char *val1, const Dstr &val2) {
  return moascf (val1, val2.aschar(), false);
}

int dstrcasecmp (const char *val1, const char *val2) {
  return moascf (val1, val2, false);
}

bool operator%= (const Dstr &a, const Dstr &b) {
  return !(moascf (a.aschar(), b.aschar(), true));
}

bool operator%= (const Dstr &a, const char *b) {
  return !(moascf (a.aschar(), b, true));
}

bool operator%= (const char *a, const Dstr &b) {
  return !(moascf (a, b.aschar(), true));
}

// Contains is inherently an order of magnitude more complex than the
// preceding comparison functions, so deferring the expansion of
// ligatures might not make a significant difference.  Moreover, it is
// not immediately obvious whether less expansion would result from
// deferring it to moascf versus doing it here.  It is the case that
// some expansion must be done here in order to notice that ¼ contains
// '/'.  Lacking clear answers, the expansion is done up front to keep
// the function simple.
bool Dstr::contains (const Dstr &val) const {
  if (!theBuffer)
    return false;
  if (!val.theBuffer)
    return false;
  if (val.length() == 0)
    return true;
  Dstr xbuf (*this), xval (val);
  xbuf.expand_ligatures();
  xval.expand_ligatures();
  int i, l = (int)(xbuf.length())-(int)(xval.length())+1;
  for (i=0; i<l; ++i)
    if (!(moascf (xbuf.ascharfrom(i), xval.aschar(), true)))
      return true;
  return false;
}

Dstr::Dstr () {
  theBuffer = NULL;
}

Dstr::Dstr (const char *val) {
  if (val) {
    require (theBuffer = strdup (val));
    used = strlen (val);
    max = used + 1;
  } else
    theBuffer = NULL;
}

Dstr::Dstr (char val) {
  char t[2];
  t[0] = val;
  t[1] = '\0';
  require (theBuffer = strdup (t));
  used = 1;
  max = 2;
}

Dstr::Dstr (const Dstr &val) {
  if (!(val.isNull())) {
    theBuffer = val.asdupchar();
    used = val.length();
    max = used + 1;
  } else
    theBuffer = NULL;
}

Dstr::Dstr (int val) {
  char t[80];
  sprintf (t, "%d", val);
  require (theBuffer = strdup (t));
  used = strlen (theBuffer);
  max = used + 1;
}

Dstr::Dstr (unsigned int val) {
  char t[80];
  sprintf (t, "%u", val);
  require (theBuffer = strdup (t));
  used = strlen (theBuffer);
  max = used + 1;
}

Dstr::Dstr (long int val) {
  char t[80];
  sprintf (t, "%ld", val);
  require (theBuffer = strdup (t));
  used = strlen (theBuffer);
  max = used + 1;
}

Dstr::Dstr (long unsigned int val) {
  char t[80];
  sprintf (t, "%lu", val);
  require (theBuffer = strdup (t));
  used = strlen (theBuffer);
  max = used + 1;
}

Dstr::Dstr (long long int val) {
  char t[80];
  sprintf (t, "%lld", val);
  require (theBuffer = strdup (t));
  used = strlen (theBuffer);
  max = used + 1;
}

Dstr::Dstr (long long unsigned int val) {
  char t[80];
  sprintf (t, "%llu", val);
  require (theBuffer = strdup (t));
  used = strlen (theBuffer);
  max = used + 1;
}

Dstr::Dstr (double val) {
  char t[80];
  sprintf (t, "%f", val);
  require (theBuffer = strdup (t));
  used = strlen (theBuffer);
  max = used + 1;
}

Dstr::~Dstr () {
  if (theBuffer)
    free (theBuffer);
}

Dstr &Dstr::getline (FILE *fp) {
  char buf[82], *ret;
  if (!(ret = fgets (buf, 82, fp)))
    operator= ((char *)NULL);
  else {
    operator= (emptyString);
    while (ret) {
      operator+= (buf);
      if (used > 0)
        if (theBuffer[used-1] == '\n') {
          operator-= (used-1);
          break;
        }
      ret = fgets (buf, 82, fp);
    }
  }
  return (*this);
}

void Dstr::getline (Dstr &line_out) {
  line_out = (char *)NULL;
  while (length() > 0 && operator[](0) != '\n') {
    line_out += operator[](0);
    operator/= (1);
  }
  if (operator[](0) == '\n')
    operator/= (1);
}

Dstr& Dstr::pruser (const char *prompt, const char *deflt) {
  printf ("%s [default %s]: ", prompt, deflt);
  getline (stdin);
  if (!length())
    operator= (deflt);
  return (*this);
}

// Scan a string like fscanf (fp, "%s")
Dstr &Dstr::scan (FILE *fp) {
  int c;
  operator= ((char *)NULL);
  // Skip whitespace
  do {
    c = getc(fp);
    if (c == EOF)
      return (*this);
  } while (isspace (c));
  // Get the string
  operator= ((char)c);
  while (1) {
    c = getc(fp);
    if (c == EOF)
      return (*this);
    if (isspace (c))
      return (*this);
    operator+= ((char)c);
  }
}

Dstr &Dstr::pad (unsigned to_length) {
  while (length() < to_length)
    operator+= (" ");
  return (*this);
}

Dstr &Dstr::trim () {
  trim_head();
  trim_tail();
  return (*this);
}

Dstr &Dstr::trim_head () {
  while (isspace (operator[](0)))
    operator/= (1);
  return (*this);
}

Dstr &Dstr::trim_tail () {
  while (isspace (back()))
    operator-= (length()-1);
  return (*this);
}

Dstr &Dstr::operator-= (unsigned at_index) {
  if (theBuffer) {
    if (at_index < used) {
      theBuffer[at_index] = '\0';
      used = at_index;
    }
  }
  return (*this);
}

int Dstr::strchr (char val) const {
  if (!theBuffer)
    return -1;
  char *c = ::strchr (theBuffer, val);
  if (!c)
    return -1;
  return (c - theBuffer);
}

int Dstr::strrchr (char val) const {
  if (!theBuffer)
    return -1;
  char *c = ::strrchr (theBuffer, val);
  if (!c)
    return -1;
  return (c - theBuffer);
}

int Dstr::strstr (const Dstr &val) const {
  if (!theBuffer)
    return -1;
  if (!val.theBuffer)
    return -1;
  char *c = ::strstr (theBuffer, val.theBuffer);
  if (!c)
    return -1;
  return (c - theBuffer);
}

Dstr &Dstr::operator= (const char *val) {
  char *temp = NULL;
  // Indirection to make self-assignment (in whole or in part) safe.
  if (val)
    require (temp = strdup (val));
  if (theBuffer)
    free (theBuffer);
  theBuffer = temp;
  if (temp) {
    used = strlen (temp);
    max = used + 1;
  }
  return (*this);
}

Dstr &Dstr::operator= (char val) {
  char t[2];
  t[0] = val;
  t[1] = '\0';
  operator= (t);
  return (*this);
}

Dstr &Dstr::operator= (const Dstr &val) {
  operator= (val.theBuffer);
  return (*this);
}

Dstr &Dstr::operator= (int val) {
  char t[80];
  sprintf (t, "%d", val);
  operator= (t);
  return (*this);
}

Dstr &Dstr::operator= (unsigned int val) {
  char t[80];
  sprintf (t, "%u", val);
  operator= (t);
  return (*this);
}

Dstr &Dstr::operator= (long int val) {
  char t[80];
  sprintf (t, "%ld", val);
  operator= (t);
  return (*this);
}

Dstr &Dstr::operator= (long unsigned int val) {
  char t[80];
  sprintf (t, "%lu", val);
  operator= (t);
  return (*this);
}

Dstr &Dstr::operator= (long long int val) {
  char t[80];
  sprintf (t, "%lld", val);
  operator= (t);
  return (*this);
}

Dstr &Dstr::operator= (long long unsigned int val) {
  char t[80];
  sprintf (t, "%llu", val);
  operator= (t);
  return (*this);
}

Dstr &Dstr::operator= (double val) {
  char t[80];
  sprintf (t, "%f", val);
  operator= (t);
  return (*this);
}

Dstr &Dstr::operator+= (const char *val) {
  if (val) {
    if (!theBuffer)
      operator= (val);
    else {
      unsigned l;
      if ((l = strlen (val))) {
        while (l + used >= max) {  // Leave room for terminator
          // Expand
          max *= 2;
          require (theBuffer = (char *) realloc (theBuffer, max*sizeof(char)));
        }
        strcpy (theBuffer+used, val);
        used += l;
      }
    }
  }
  return (*this);
}

Dstr &Dstr::operator+= (char val) {
  char t[2];
  t[0] = val;
  t[1] = '\0';
  operator+= (t);
  return (*this);
}

Dstr &Dstr::operator+= (const Dstr &val) {
  operator+= (val.theBuffer);
  return (*this);
}

Dstr &Dstr::operator+= (int val) {
  char t[80];
  sprintf (t, "%d", val);
  operator+= (t);
  return (*this);
}

Dstr &Dstr::operator+= (unsigned int val) {
  char t[80];
  sprintf (t, "%u", val);
  operator+= (t);
  return (*this);
}

Dstr &Dstr::operator+= (long int val) {
  char t[80];
  sprintf (t, "%ld", val);
  operator+= (t);
  return (*this);
}

Dstr &Dstr::operator+= (long unsigned int val) {
  char t[80];
  sprintf (t, "%lu", val);
  operator+= (t);
  return (*this);
}

Dstr &Dstr::operator+= (long long int val) {
  char t[80];
  sprintf (t, "%lld", val);
  operator+= (t);
  return (*this);
}

Dstr &Dstr::operator+= (long long unsigned int val) {
  char t[80];
  sprintf (t, "%llu", val);
  operator+= (t);
  return (*this);
}

Dstr &Dstr::operator+= (double val) {
  char t[80];
  sprintf (t, "%f", val);
  operator+= (t);
  return (*this);
}

Dstr &Dstr::operator*= (const char *val) {
  Dstr temp (*this);
  operator= (val);
  operator+= (temp);
  return (*this);
}

Dstr &Dstr::operator*= (char val) {
  Dstr temp (*this);
  operator= (val);
  operator+= (temp);
  return (*this);
}

Dstr &Dstr::operator*= (const Dstr &val) {
  Dstr temp (*this);
  operator= (val);
  operator+= (temp);
  return (*this);
}

Dstr &Dstr::operator/= (unsigned at_index) {
  if (theBuffer)
    operator= (ascharfrom (at_index));
  return (*this);
}

Dstr &Dstr::operator/= (Dstr &val) {
  val = (char *)NULL;
  if (theBuffer) {
    // Eat whitespace
    while (used > 0 && isspace (operator[](0)))
      operator/= (1);
    // Anything left?
    if (used == 0) {
      // Nothing left.
      operator= ((char *)NULL);
    } else {
      if (operator[](0) == '"') {
        // Delimited argument
        val += operator[](0);
        operator/= (1);
        while (used > 0 && operator[](0) != '"') {
          val += operator[](0);
          operator/= (1);
        }
        // Grab the matching quote, if any.
        if (used > 0) {
          val += operator[](0);
          operator/= (1);
        }
      } else {
        // Undelimited argument
        while (used > 0 && (!(isspace (operator[](0))))) {
          val += operator[](0);
          operator/= (1);
        }
      }
    }
  }
  return (*this);
}

char Dstr::operator[] (unsigned at_index) const {
  if (!theBuffer)
    return '\0';
  if (at_index >= used)
    return '\0';
  return theBuffer[at_index];
}

char Dstr::back() const {
  if (length())
    return operator[] (length()-1);
  return '\0';
}

unsigned Dstr::repchar (char X, char Y) {
  unsigned i, l = length(), repcount=0;
  for (i=0; i<l; ++i)
    if (theBuffer[i] == X) {
      theBuffer[i] = Y;
      ++repcount;
    }
  return repcount;
}

unsigned Dstr::repstr (const char *X, const char *Y) {
  if (!theBuffer)
    return 0;
  assert (X);
  assert (Y);
  size_t Xlen = strlen(X);
  assert (Xlen > 0);
  if (Xlen > length())
    return 0;
  unsigned i, l = length() - Xlen + 1, repcount=0;
  Dstr temp;
  for (i=0; i<l;)
    if (!strncmp (theBuffer+i, X, Xlen)) {
      temp += Y;
      ++repcount;
      i += Xlen;
    } else
      temp += theBuffer[i++];
  temp += ascharfrom(i);
  operator= (temp);
  return repcount;
}

unsigned Dstr::length () const {
  if (!theBuffer)
    return 0;
  return used;
}

bool Dstr::isNull () const {
  return !theBuffer;
}

char *Dstr::aschar () const {
  if (theBuffer)
    return theBuffer;
  return emptyString;
}

char *Dstr::asdupchar() const {
  char *temp;
  require (temp = strdup (aschar()));
  return temp;
}

char *Dstr::ascharfrom(unsigned from_index) const {
  if (!theBuffer)
    return emptyString;
  if (from_index >= used)
    return emptyString;
  return theBuffer + from_index;
}

char *Dstr::asrawchar () const {
  return theBuffer;
}

bool operator== (const Dstr &val1, const char *val2) {
  if ((!val2) && (val1.isNull()))
    return true;
  if ((!val2) || (val1.isNull()))
    return false;
  return (!(strcmp (val1.aschar(), val2)));
}

bool operator== (const char *val1, const Dstr &val2) {
  return (val2 == val1);
}

bool operator== (const Dstr &val1, const Dstr &val2) {
  return (val1 == val2.asrawchar());
}

bool operator!= (const char *val1, const Dstr &val2) {
  return (!(val1 == val2));
}

bool operator!= (const Dstr &val1, const char *val2) {
  return (!(val1 == val2));
}

bool operator!= (const Dstr &val1, const Dstr &val2) {
  return (!(val1 == val2));
}

bool operator< (const Dstr &val1, const Dstr &val2) {
  if (val1.isNull())
    return (!(val2.isNull()));
  if (val2.isNull())
    return false;
  return (strcmp (val1.aschar(), val2.aschar()) < 0);
}

bool InsensitiveOrdering (const Dstr &a, const Dstr &b) {
  return dstrcasecmp (a, b) < 0;
}

Dstr &Dstr::lowercase() {
  unsigned i, l = length();
  for (i=0; i<l; ++i)
    theBuffer[i] = tolower(theBuffer[i]);
  return (*this);
}

Dstr &Dstr::rfc2445_mangle() {
  Dstr temp;
  unsigned i, l = length();
  for (i=0; i<l; ++i) {
    switch (theBuffer[i]) {
    case ';':
    case '\\':
    case ',':
      temp += '\\';
      temp += theBuffer[i];
      break;
    case '\n':
      temp += "\\n";
      break;
    default:
      temp += theBuffer[i];
    }
  }
  operator= (temp);
  return (*this);
}

// Mangle per LaTeX.
// This assumes that LaTeX is using Latin-1 input encoding and just needs
// the special characters to be escaped.  Also, it does not fix the spacing
// after abbreviations.
Dstr &Dstr::LaTeX_mangle() {
  Dstr temp;
  unsigned i, l = length();
  bool quotehack=false;
  for (i=0; i<l; ++i) {
    switch (theBuffer[i]) {
    case '&':
    case '$':
    case '%':
    case '#':
    case '_':
    case '{':
    case '}':
      temp += '\\';
      temp += theBuffer[i];
      break;
    case '~':
      temp += "$\\sim$";
      break;
    case '^':
      temp += "\\verb|^|";
      break;
    case '\\':
      temp += "$\\backslash$";
      break;
    case '"':
      if (quotehack) {
        temp += "''";
        quotehack = false;
      } else {
        temp += "``";
        quotehack = true;
      }
      break;
    default:
      temp += theBuffer[i];
    }
  }
  operator= (temp);
  return (*this);
}

Dstr &Dstr::expand_ligatures() {
  Dstr temp;
  unsigned i, l = length();
  for (i=0; i<l; ++i) {
    switch (theBuffer[i]) {
    case '¼':
      temp += "1/4";
      break;
    case '½':
      temp += "1/2";
      break;
    case '¾':
      temp += "3/4";
      break;
    case 'Æ':
      temp += "AE";
      break;
    case 'æ':
      temp += "ae";
      break;
    case 'ß':
      temp += "ss";
      break;
    default:
      temp += theBuffer[i];
    }
  }
  operator= (temp);
  return (*this);
}

Dstr &Dstr::utf8 () {
  Dstr temp;
  unsigned i, l = length();
  for (i=0; i<l; ++i) {
    if (theBuffer[i] & 0x80) {
      // The & 3 is to defend against possible sign bit propagation
      temp += (char)(0xC0 | ((theBuffer[i] >> 6) & 3));
      temp += (char)(0x80 | (theBuffer[i] & 0x3F));
    } else
      temp += theBuffer[i];
  }
  operator= (temp);
  return (*this);
}

Dstr &Dstr::unutf8 () {
  Dstr temp;
  unsigned i, l = length();
  for (i=0; i<l; ++i) {
    if (theBuffer[i] & 0x80) {
      if (((theBuffer[i] & 0xC2) != 0xC2) ||
          (theBuffer[i] & 0x3C) ||
          (i + 1 == l) ||
          ((theBuffer[i+1] & 0x80) != 0x80) ||
          (theBuffer[i+1] & 0x40)) {
        operator= ((char*)NULL);
        return (*this);
      }
      temp += (char)((theBuffer[i] << 6) | (theBuffer[i+1] & 0x3F));
      ++i;
    } else
      temp += theBuffer[i];
  }
  operator= (temp);
  return (*this);
}

static void mapchars (char *s, char const * const map) {
  if (s && map)
    while ((*s = map[(unsigned char)(*s)]))
      ++s;
}

Dstr &Dstr::CP437() {
  static const char cp437[257] =
	"\000\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037"
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~\177"
	"????????????????????????????????"
	"ÿ­\233\234?\235????¦®ª???øñı??æ?ú??§¯¬«?¨"
	"????\216\217\222\200?\220???????¥????\231?????\232??á"
	"\205 \203?\204\206\221\207\212\202\210\211\215¡\214\213?¤\225¢\223?\224ö?\227£\226\201??\230";
  mapchars (theBuffer, cp437);
  return (*this);
}

Dstr &Dstr::unCP437() {
  static const char uncp437[257] =
	"\000\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037"
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~\177"
	"ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥??"
	"áíóúñÑªº¿?¬½¼¡«»????????????????"
	"????????????????????????????????"
	"?ß????µ??????????±????÷?°?·??²? ";
  mapchars (theBuffer, uncp437);
  return (*this);
}

char DstrPresentCheck () {
  return '\0';
}

char DstrCompat20070215Check () {
  return '\0';
}
