// $Id: Dstr.hh 5054 2013-07-20 01:25:41Z flaterco $
// Dstr:  Dave's String class.

// Canonicalized 2005-05-05.
// Moved into libdstr 2007-02-06.

// This source is public domain.  As if you would want it.

// Perpetrator:  David Flater.


// Dstr will never become a subclass of std::string.  The two classes
// have irreconcilable differences.

// Dstr knows the difference between a null string and an empty
// string.

// All Dstr operations have safe and useful behaviors when operating
// on a null string or with out-of-range indices.  This eliminates the
// need for much defensive coding in typically messy text processing
// functions.  E.g., you can test whether the 10th character is 'F'
// without worrying about whether the string is null or contains less
// than 10 characters.

// For the operations where it matters, Dstr assumes a Latin-1
// character set and collation, regardless of the platform's support
// or lack thereof for relevant locales.

// Operations described as "insensitive" ignore case and diacriticals,
// and they expand the ligatures ¼, ½, ¾, æ, Æ, and ß prior to
// comparison.  Operations described as "sensitive" do a literal,
// byte-by-byte comparison.

// No multi-level comparison, a la Unicode Collation Algorithm, is
// ever done.  Strings that differ only in the placement of
// diacriticals or expansion of ligatures are completely equivalent to
// all insensitive operations.

// As of Unicode 4.1.0, the middle character in the expansions of the
// fractions (FRACTION SLASH) is not equivalent to '/' (SOLIDUS).
// Since FRACTION SLASH is not in Latin-1, SOLIDUS is substituted when
// expanding ligatures.


#ifndef __DSTR__
#define __DSTR__

// Forward declarations of FILE just don't work.
#include <stdio.h>

class Dstr {
public:

  // -------- Version defines  --------

  // Versions prior to 1.0 did not have these at all.
  #define DSTR_MAJOR_REV 1
  #define DSTR_MINOR_REV 0
  #define DSTR_PATCHLEVEL 0


  // -------- Constructors and destructors --------

  Dstr ();
  Dstr (const char *val);
  Dstr (char val);
  Dstr (const Dstr &val);
  Dstr (int val);
  Dstr (unsigned int val);
  Dstr (long int val);
  Dstr (long unsigned int val);
  Dstr (long long int val);
  Dstr (long long unsigned int val);
  Dstr (double val);
  ~Dstr ();


  // -------- General attributes --------

  unsigned length() const; // Returns 0 if null.
  bool isNull() const;


  // -------- Assign --------

  Dstr& operator= (const char *val);
  Dstr& operator= (char val);
  Dstr& operator= (const Dstr &val);
  Dstr& operator= (int val);
  Dstr& operator= (unsigned int val);
  Dstr& operator= (long int val);
  Dstr& operator= (long unsigned int val);
  Dstr& operator= (long long int val);
  Dstr& operator= (long long unsigned int val);
  Dstr& operator= (double val);


  // -------- Append --------

  Dstr& operator+= (const char *val);
  Dstr& operator+= (char val);
  Dstr& operator+= (const Dstr &val);
  Dstr& operator+= (int val);
  Dstr& operator+= (unsigned int val);
  Dstr& operator+= (long int val);
  Dstr& operator+= (long unsigned int val);
  Dstr& operator+= (long long int val);
  Dstr& operator+= (long long unsigned int val);
  Dstr& operator+= (double val);


  // -------- Prepend --------

  Dstr& operator*= (const char *val);
  Dstr& operator*= (char val);
  Dstr& operator*= (const Dstr &val);


  // -------- Truncate --------

  // Remove all text before the specified index.
  Dstr& operator/= (unsigned at_index);

  // Remove all text at and after the specified index.
  Dstr& operator-= (unsigned at_index);

  // See also, whitespace operations (trim).


  // -------- Get input --------

  // Read a line.  The trailing newline is stripped.  DOS/VMS
  // two-character line discipline is not supported.  On EOF, Dstr
  // becomes null.
  Dstr& getline (FILE *fp);

  // Scan a string like fscanf (fp, "%s").
  Dstr& scan (FILE *fp);

  // Prompt user for input.
  Dstr& pruser (const char *prompt, const char *deflt);


  // -------- Parse --------

  // Scan a line from a Dstr, stripping newline.
  void getline (Dstr &line_out);

  // Break off the first substring delimited by whitespace or double
  // quotes (no escaping) and assign it to val.  The double quotes are
  // NOT removed, and if the argument is terminated by the end-of-line
  // rather than a matching quote, you'll get the unbalanced quotes
  // back.
  Dstr& operator/= (Dstr &val);


  // -------- Char operations --------

  // Get character at index.  Returns '\0' if index is out of bounds.
  char operator[] (unsigned at_index) const;

  // Get last character.  Returns '\0' if string is null or empty.
  char back() const;

  // Retrieve value as character string.  This will actually be
  // theBuffer unless it's NULL, in which case an empty string will be
  // substituted.
  char *aschar() const;

  // Same thing, but strdup'd.
  char *asdupchar() const;

  // Same thing, but starting at index.  Returns empty string if index
  // is out of bounds.
  char *ascharfrom(unsigned from_index) const;

  // Retrieve value as a character string, no NULL masking.
  char *asrawchar() const;


  // -------- Search --------

  // Get index; returns -1 if not found.
  // These are all sensitive.
  int strchr (char val) const;
  int strrchr (char val) const;
  int strstr (const Dstr &val) const;

  // Returns true if val appears as a substring.
  // Insensitive.
  bool contains (const Dstr &val) const;


  // -------- Replace --------

  // Smash case.
  Dstr &lowercase();

  // We don't need no steenking uppercase operation.

  // Replace all instances of character X with character Y; returns
  // number of reps.  Sensitive.
  unsigned repchar (char X, char Y);

  // Replace all instances of string X with string Y; returns number
  // of reps.  The replacement is done in one pass; any additional
  // instances that appear after the first pass are left alone.
  // Sensitive.
  // Most invocations of this method involve two string constants, so
  // accepting const Dstr& would just create a bunch of unneeded temporaries.
  unsigned repstr (const char *X, const char *Y);

  // Mangle per RFC 2445 TEXT.  This is equivalent to
  //   repstr (";", "\\;")
  //   repstr ("\\", "\\\\")
  //   repstr (",", "\\,")
  //   repstr ("\n", "\\n")
  Dstr &rfc2445_mangle();

  // Mangle per LaTeX.  This assumes that LaTeX is using Latin-1 input
  // encoding and just needs the special characters to be escaped.
  // Also, it does not fix the spacing after abbreviations.
  Dstr &LaTeX_mangle();

  // expand_ligatures is equivalent to
  //   repstr ("¼", "1/4")
  //   repstr ("½", "1/2")
  //   repstr ("¾", "3/4")
  //   repstr ("Æ", "AE")
  //   repstr ("æ", "ae")
  //   repstr ("ß", "ss")
  Dstr &expand_ligatures();


  // -------- Whitespace operations --------

  // Pad to length with spaces.
  Dstr &pad (unsigned to_length);

  // Strip leading and trailing whitespace.
  Dstr &trim ();

  // Strip only one or the other.
  Dstr &trim_head ();
  Dstr &trim_tail ();


  // -------- Character set / encoding translations --------

  // Caution:  These translations are intended for use at I/O time to allow
  // an application that is based on Latin-1 to function minimally in newer
  // (UTF-8) and older (CP437) environments.  All other Dstr operations
  // assume that the character encoding is Latin-1.  For a more general
  // translation service, use iconv.

  // Translate Latin-1 to UTF-8 or vice-versa.  Translation to UTF-8 always
  // succeeds.  In the reverse direction, if any character doesn't translate
  // or if the string is not valid UTF-8, the string becomes null.  length()
  // will return the number of bytes, not the number of UTF-8 characters.
  Dstr &utf8();
  Dstr &unutf8();

  // Translate Latin-1 to CP437 or vice-versa.  Lossy in both directions.
  // Characters that don't translate become question marks.
  Dstr &CP437();
  Dstr &unCP437();


protected:
  char *theBuffer;
  unsigned max;   // Total max buffer size including \0
  unsigned used;  // Length not including \0
};


// -------- Comparison operators --------

// All == and != are sensitive.

bool operator== (const Dstr &val1, const char *val2);
bool operator== (const char *val1, const Dstr &val2);
bool operator== (const Dstr &val1, const Dstr &val2);

bool operator!= (const Dstr &val1, const char *val2);
bool operator!= (const char *val1, const Dstr &val2);
bool operator!= (const Dstr &val1, const Dstr &val2);

// This sensitive < operator is the default StrictWeakOrdering used by
// std::set and other templates.
bool operator< (const Dstr &val1, const Dstr &val2);

// This insensitive comparison is what you would use to sort a list
// alphabetically.  It is equivalent to dstrcasecmp(a,b) < 0.
bool InsensitiveOrdering (const Dstr &a, const Dstr &b);

// "Is kinda like" comparison operator.  It's insensitive and it
// accepts a prefix instead of the entire string.  Analogous to
// !strncasecmp (a, b, strlen(b)).
bool operator%= (const Dstr &a, const Dstr &b);
bool operator%= (const Dstr &a, const char *b);
bool operator%= (const char *a, const Dstr &b);

// These are insensitive.
//   <0 means val1 < val2
//   >0 means val1 > val2
//    0 means val1 == val2
int dstrcasecmp (const Dstr &val1, const Dstr &val2);
int dstrcasecmp (const Dstr &val1, const char *val2);
int dstrcasecmp (const char *val1, const Dstr &val2);
int dstrcasecmp (const char *val1, const char *val2);


// The following functions serve no purpose but to give Autoconf's
// dated AC_CHECK_LIB macro something it can latch onto.  It requires
// a simple C function.

// This function is present if libdstr is present.
extern "C" char DstrPresentCheck();

// This function is present if the installed libdstr is substitutable
// for the 2007-02-15 version.
extern "C" char DstrCompat20070215Check();

#endif
